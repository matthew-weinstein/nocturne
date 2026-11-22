import type { DialView } from "../view/night.ts";
import { HALF_WIDTH, END_WIDTH, STEP_MIN } from "../view/ribbon.ts";
import { Spring } from "./spring.ts";

export const WIDTH = 820;
export const HEIGHT = 720;
export const CX = 410;
export const CY = 360;
export const RING = 262;

const BLOOM_REACH = RING + 40;
const PROFILE_R = RING + HALF_WIDTH + 2;
export const SWEEP_DELAY_MS = 60;
export const SWEEP_MS = 950;
const BREATHE_MS = 7000;
const BREATHE_START = Math.acos(-1 / 3) / Math.PI;
const HAND_FADE_MS = 350;

export interface Span {
  startMin: number;
  endMin: number;
}

export function polar(clockMin: number, radius: number): [number, number] {
  const angle = (clockMin / 1440) * Math.PI * 2;
  return [CX + radius * Math.sin(angle), CY - radius * Math.cos(angle)];
}

/** Minutes from bed under a point in dial units, or null off the ring or outside the night. */
export function minuteUnder(
  x: number,
  y: number,
  bedMin: number,
  spanMin: number,
): number | null {
  const dx = x - CX;
  const dy = y - CY;
  const distance = Math.hypot(dx, dy);
  if (distance <= 200 || distance >= RING + 50) {
    return null;
  }
  const clockMin = ((Math.atan2(dx, -dy) / (Math.PI * 2)) * 1440 + 1440) % 1440;
  const minute = (clockMin - bedMin + 1440) % 1440;
  if (minute > spanMin) {
    return null;
  }
  return Math.round(minute * 4) / 4;
}

function easeOut(t: number): number {
  return 1 - Math.pow(1 - t, 3);
}

function restart(element: HTMLElement, name: string): void {
  element.classList.remove(name);
  void element.offsetWidth;
  element.classList.add(name);
}

/** Seconds after the dial appears at which the sweep reaches this minute from bed. */
export function sweepReaches(minuteFromBed: number, spanMin: number): number {
  const fraction = clamp01(minuteFromBed / Math.max(1, spanMin));
  const t = 1 - Math.cbrt(1 - fraction);
  return (SWEEP_DELAY_MS + t * SWEEP_MS) / 1000;
}

function clamp01(value: number): number {
  return Math.min(1, Math.max(0, value));
}

function layer(
  width: number,
  height: number,
): [HTMLCanvasElement, CanvasRenderingContext2D] {
  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  const context = canvas.getContext("2d");
  if (context === null) {
    throw new Error("no 2d canvas");
  }
  return [canvas, context];
}

function ribbonPath(view: DialView): Path2D {
  const path = new Path2D();
  const outer: [number, number][] = [];
  const inner: [number, number][] = [];
  view.widths.forEach((width, index) => {
    const minute = Math.min(index * STEP_MIN, view.spanMin);
    outer.push(polar(view.bedMin + minute, RING + width));
    inner.push(polar(view.bedMin + minute, RING - width));
  });
  if (outer.length === 0) {
    return path;
  }
  path.moveTo(outer[0][0], outer[0][1]);
  for (const [x, y] of outer) {
    path.lineTo(x, y);
  }
  const endAngle =
    ((view.bedMin + view.spanMin) / 1440) * Math.PI * 2 - Math.PI / 2;
  const [endX, endY] = polar(view.bedMin + view.spanMin, RING);
  path.arc(endX, endY, END_WIDTH, endAngle, endAngle + Math.PI);
  for (let index = inner.length - 1; index >= 0; index--) {
    path.lineTo(inner[index][0], inner[index][1]);
  }
  const startAngle = (view.bedMin / 1440) * Math.PI * 2 - Math.PI / 2;
  const [startX, startY] = polar(view.bedMin, RING);
  path.arc(
    startX,
    startY,
    END_WIDTH,
    startAngle + Math.PI,
    startAngle + Math.PI * 2,
  );
  path.closePath();
  return path;
}

function profile(
  context: CanvasRenderingContext2D,
  edge: string,
  core: string,
  flank: number,
): CanvasGradient {
  const gradient = context.createRadialGradient(CX, CY, 0, CX, CY, PROFILE_R);
  const at = (radius: number): number => radius / PROFILE_R;
  gradient.addColorStop(at(RING - HALF_WIDTH - 2), `rgba(${edge},0)`);
  gradient.addColorStop(at(RING - HALF_WIDTH * 0.45), `rgba(${edge},${flank})`);
  gradient.addColorStop(at(RING), `rgba(${core},1)`);
  gradient.addColorStop(at(RING + HALF_WIDTH * 0.45), `rgba(${edge},${flank})`);
  gradient.addColorStop(1, `rgba(${edge},0)`);
  return gradient;
}

function wedge(
  path: Path2D,
  fromMin: number,
  toMin: number,
  radius: number,
): void {
  const start = (fromMin / 1440) * Math.PI * 2 - Math.PI / 2;
  const end = (toMin / 1440) * Math.PI * 2 - Math.PI / 2;
  path.moveTo(CX, CY);
  path.arc(CX, CY, radius, start, end);
  path.closePath();
}

export class Dial {
  private canvas: HTMLCanvasElement;
  private context: CanvasRenderingContext2D;
  private base: HTMLCanvasElement;
  private bloom: HTMLCanvasElement;
  private ribbon: HTMLCanvasElement;
  private sweep: HTMLElement;
  private view: DialView | null = null;
  private stir: Span[] = [];
  private scale = 1;
  private frame = 0;
  private cursor: number | null = null;
  private highlight: Span | null = null;
  private hand = new Spring(420, 25);
  private handOpacity = 0;
  private lastTick = 0;
  private reducedMotion = window.matchMedia("(prefers-reduced-motion: reduce)");
  private resizer: ResizeObserver;

  constructor(canvas: HTMLCanvasElement) {
    this.canvas = canvas;
    const context = canvas.getContext("2d");
    if (context === null) {
      throw new Error("no 2d canvas");
    }
    this.context = context;
    this.base = document.createElement("canvas");
    this.base.className = "dial-base";
    this.bloom = document.createElement("canvas");
    this.bloom.className = "dial-bloom";
    this.bloom.style.animationDelay = `${-Math.round(BREATHE_START * BREATHE_MS)}ms`;
    this.ribbon = document.createElement("canvas");
    this.sweep = document.createElement("div");
    this.sweep.className = "dial-sweep";
    this.sweep.append(this.bloom, this.ribbon);
    this.sweep.addEventListener("animationend", (event) => {
      if (event.target === this.sweep) {
        this.sweep.classList.add("done");
      }
    });
    canvas.before(this.base, this.sweep);
    this.resizer = new ResizeObserver(() => this.resize());
    this.resizer.observe(canvas);
  }

  destroy(): void {
    cancelAnimationFrame(this.frame);
    this.frame = 0;
    this.resizer.disconnect();
  }

  setNight(view: DialView, stir: Span[]): void {
    this.view = view;
    this.stir = stir;
    this.cursor = null;
    this.highlight = null;
    this.handOpacity = 0;
    this.hand.jump(view.bedMin + view.spanMin / 2);
    this.sweep.style.setProperty("--from", `${((view.bedMin / 1440) * 360).toFixed(2)}deg`);
    this.sweep.style.setProperty("--to", `${(((view.spanMin + 2) / 1440) * 360).toFixed(2)}deg`);
    this.sweep.classList.remove("done");
    restart(this.base, "enter");
    restart(this.sweep, "enter");
    this.renderLayers();
    this.request();
  }

  setCursor(minute: number | null, highlight: Span | null): void {
    if (this.view === null) {
      return;
    }
    this.cursor = minute;
    this.highlight = highlight;
    if (minute !== null) {
      const target = this.view.bedMin + minute;
      if (this.handOpacity === 0) {
        this.hand.jump(target);
      } else {
        this.hand.target = target;
      }
    }
    this.request();
  }

  pointToMinute(clientX: number, clientY: number): number | null {
    if (this.view === null) {
      return null;
    }
    const box = this.canvas.getBoundingClientRect();
    const x = ((clientX - box.left) / box.width) * WIDTH;
    const y = ((clientY - box.top) / box.height) * HEIGHT;
    return minuteUnder(x, y, this.view.bedMin, this.view.spanMin);
  }

  private resize(): void {
    const box = this.canvas.getBoundingClientRect();
    const ratio = window.devicePixelRatio || 1;
    const width = Math.max(1, Math.round(box.width * ratio));
    const height = Math.max(1, Math.round(box.height * ratio));
    if (width === this.canvas.width && height === this.canvas.height) {
      return;
    }
    for (const target of [this.canvas, this.base, this.bloom, this.ribbon]) {
      target.width = width;
      target.height = height;
    }
    this.scale = width / WIDTH;
    this.renderLayers();
    this.request();
  }

  private prepare(target: HTMLCanvasElement): CanvasRenderingContext2D {
    const context = target.getContext("2d");
    if (context === null) {
      throw new Error("no 2d canvas");
    }
    context.setTransform(1, 0, 0, 1, 0, 0);
    context.clearRect(0, 0, target.width, target.height);
    context.scale(this.scale, this.scale);
    return context;
  }

  private renderLayers(): void {
    if (this.view === null || this.canvas.width <= 1) {
      return;
    }
    const view = this.view;

    const face = this.prepare(this.base);
    face.strokeStyle = "rgba(255,255,255,.075)";
    face.lineWidth = 1;
    face.beginPath();
    face.arc(CX, CY, RING, 0, Math.PI * 2);
    face.stroke();
    face.lineCap = "round";
    for (let hour = 0; hour < 24; hour++) {
      const major = hour % 6 === 0;
      let inner = RING - 8;
      face.strokeStyle = "rgba(232,235,244,.13)";
      if (major) {
        inner = RING - 12;
        face.strokeStyle = "rgba(232,235,244,.42)";
      }
      const [x1, y1] = polar(hour * 60, inner);
      const [x2, y2] = polar(hour * 60, RING - 4);
      face.beginPath();
      face.moveTo(x1, y1);
      face.lineTo(x2, y2);
      face.stroke();
    }

    if (view.sunriseMin !== null && view.sunsetMin !== null && view.sunsetMin > view.sunriseMin) {
      const gradient = face.createConicGradient(-Math.PI / 2, CX, CY);
      const steps = 96;
      for (let index = 0; index <= steps; index++) {
        const x = index / steps;
        const minute = view.sunriseMin + (view.sunsetMin - view.sunriseMin) * x;
        const alpha = 0.6 * Math.pow(Math.sin(Math.PI * x), 0.35);
        gradient.addColorStop(minute / 1440, `rgba(233,179,126,${alpha.toFixed(3)})`);
      }
      face.strokeStyle = gradient;
      face.lineWidth = 2;
      face.beginPath();
      face.arc(
        CX,
        CY,
        RING,
        (view.sunriseMin / 1440) * Math.PI * 2 - Math.PI / 2,
        (view.sunsetMin / 1440) * Math.PI * 2 - Math.PI / 2,
      );
      face.stroke();
    }

    const path = ribbonPath(view);

    const bloom = this.prepare(this.bloom);
    bloom.filter = `blur(${(9 * this.scale).toFixed(1)}px)`;
    bloom.fillStyle = "#5F7BF0";
    bloom.fill(path);
    bloom.filter = "none";

    const ribbon = this.prepare(this.ribbon);
    ribbon.fillStyle = profile(ribbon, "90,117,242", "196,208,255", 0.55);
    ribbon.fill(path);

    if (this.stir.length > 0) {
      const [stir, stirContext] = layer(this.ribbon.width, this.ribbon.height);
      stirContext.scale(this.scale, this.scale);
      stirContext.fillStyle = profile(stirContext, "242,244,255", "255,255,255", 0.8);
      stirContext.fill(path);
      const mask = new Path2D();
      for (const span of this.stir) {
        const from = Math.max(0, span.startMin - 0.4);
        const to = Math.min(view.spanMin, Math.max(span.endMin, span.startMin + 0.3) + 0.4);
        wedge(mask, view.bedMin + from, view.bedMin + to, BLOOM_REACH);
      }
      const [maskCanvas, maskContext] = layer(this.ribbon.width, this.ribbon.height);
      maskContext.scale(this.scale, this.scale);
      maskContext.filter = `blur(${(2.2 * this.scale).toFixed(1)}px)`;
      maskContext.fillStyle = "#fff";
      maskContext.fill(mask);
      stirContext.setTransform(1, 0, 0, 1, 0, 0);
      stirContext.globalCompositeOperation = "destination-in";
      stirContext.drawImage(maskCanvas, 0, 0);
      ribbon.setTransform(1, 0, 0, 1, 0, 0);
      ribbon.drawImage(stir, 0, 0);
    }
  }

  private request(): void {
    if (this.frame !== 0) {
      return;
    }
    this.lastTick = 0;
    this.frame = requestAnimationFrame((time) => this.tick(time));
  }

  private tick(time: number): void {
    this.frame = 0;
    let elapsed = 0.016;
    if (this.lastTick > 0) {
      elapsed = Math.min(0.05, (time - this.lastTick) / 1000);
    }
    this.lastTick = time;
    if (this.drawHand(elapsed)) {
      this.frame = requestAnimationFrame((next) => this.tick(next));
    }
  }

  /** Draws the hand and the highlighted sound; true while the hand is still moving. */
  private drawHand(elapsed: number): boolean {
    const context = this.context;
    context.setTransform(1, 0, 0, 1, 0, 0);
    context.clearRect(0, 0, this.canvas.width, this.canvas.height);
    if (this.view === null) {
      return false;
    }
    let target = 0;
    if (this.cursor !== null) {
      target = 1;
    }
    if (this.reducedMotion.matches) {
      this.handOpacity = target;
      this.hand.jump(this.hand.target);
    } else {
      const step = (elapsed * 1000) / HAND_FADE_MS;
      if (this.handOpacity < target) {
        this.handOpacity = Math.min(target, this.handOpacity + step);
      } else {
        this.handOpacity = Math.max(target, this.handOpacity - step);
      }
      this.hand.step(elapsed);
      if (this.hand.settled()) {
        this.hand.jump(this.hand.target);
      }
    }

    context.save();
    context.scale(this.scale, this.scale);
    if (this.highlight !== null) {
      const from = this.view.bedMin + this.highlight.startMin;
      const to = this.view.bedMin + Math.max(this.highlight.endMin, this.highlight.startMin + 0.6);
      context.strokeStyle = "rgba(255,255,255,.9)";
      context.lineWidth = 3;
      context.beginPath();
      context.arc(
        CX,
        CY,
        RING + HALF_WIDTH + 6,
        (from / 1440) * Math.PI * 2 - Math.PI / 2,
        (to / 1440) * Math.PI * 2 - Math.PI / 2,
      );
      context.stroke();
    }
    if (this.handOpacity > 0) {
      const [x1, y1] = polar(this.hand.value, RING - 16);
      const [x2, y2] = polar(this.hand.value, RING + 12);
      context.globalAlpha = easeOut(this.handOpacity);
      context.strokeStyle = "#fff";
      context.lineWidth = 1.2;
      context.lineCap = "round";
      context.beginPath();
      context.moveTo(x1, y1);
      context.lineTo(x2, y2);
      context.stroke();
    }
    context.restore();
    return this.handOpacity !== target || !this.hand.settled();
  }
}
