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
const BREATHE_DELAY_MS = SWEEP_DELAY_MS + SWEEP_MS;
const BREATHE_MS = 7000;
const BREATHE_START = Math.acos(-1 / 3) / Math.PI;
const FACE_MS = 450;
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
  private view: DialView | null = null;
  private stir: Span[] = [];
  private layers: {
    face: HTMLCanvasElement;
    day: HTMLCanvasElement;
    bloom: HTMLCanvasElement;
    ribbon: HTMLCanvasElement;
  } | null = null;
  private scale = 1;
  private shownAt = 0;
  private frame = 0;
  private cursor: number | null = null;
  private highlight: Span | null = null;
  private hand = new Spring(420, 25);
  private handOpacity = 0;
  private handTarget = 0;
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
    this.resizer = new ResizeObserver(() => this.resize());
    this.resizer.observe(canvas);
    this.frame = requestAnimationFrame((time) => this.tick(time));
  }

  destroy(): void {
    cancelAnimationFrame(this.frame);
    this.resizer.disconnect();
  }

  setNight(view: DialView, stir: Span[]): void {
    this.view = view;
    this.stir = stir;
    this.shownAt = performance.now();
    this.cursor = null;
    this.highlight = null;
    this.handOpacity = 0;
    this.hand.jump(view.bedMin + view.spanMin / 2);
    this.renderLayers();
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
    this.canvas.width = width;
    this.canvas.height = height;
    this.scale = width / WIDTH;
    this.renderLayers();
  }

  private renderLayers(): void {
    if (this.view === null) {
      return;
    }
    const width = this.canvas.width;
    const height = this.canvas.height;
    const scale = this.scale;
    const view = this.view;

    const [face, faceContext] = layer(width, height);
    faceContext.scale(scale, scale);
    faceContext.strokeStyle = "rgba(255,255,255,.075)";
    faceContext.lineWidth = 1;
    faceContext.beginPath();
    faceContext.arc(CX, CY, RING, 0, Math.PI * 2);
    faceContext.stroke();
    faceContext.lineCap = "round";
    for (let hour = 0; hour < 24; hour++) {
      const major = hour % 6 === 0;
      let inner = RING - 8;
      faceContext.strokeStyle = "rgba(232,235,244,.13)";
      if (major) {
        inner = RING - 12;
        faceContext.strokeStyle = "rgba(232,235,244,.42)";
      }
      const [x1, y1] = polar(hour * 60, inner);
      const [x2, y2] = polar(hour * 60, RING - 4);
      faceContext.beginPath();
      faceContext.moveTo(x1, y1);
      faceContext.lineTo(x2, y2);
      faceContext.stroke();
    }

    const [day, dayContext] = layer(width, height);
    dayContext.scale(scale, scale);
    if (
      view.sunriseMin !== null &&
      view.sunsetMin !== null &&
      view.sunsetMin > view.sunriseMin
    ) {
      const gradient = dayContext.createConicGradient(-Math.PI / 2, CX, CY);
      const steps = 96;
      for (let index = 0; index <= steps; index++) {
        const x = index / steps;
        const minute = view.sunriseMin + (view.sunsetMin - view.sunriseMin) * x;
        const alpha = 0.6 * Math.pow(Math.sin(Math.PI * x), 0.35);
        gradient.addColorStop(
          minute / 1440,
          `rgba(233,179,126,${alpha.toFixed(3)})`,
        );
      }
      dayContext.strokeStyle = gradient;
      dayContext.lineWidth = 2;
      dayContext.beginPath();
      dayContext.arc(
        CX,
        CY,
        RING,
        (view.sunriseMin / 1440) * Math.PI * 2 - Math.PI / 2,
        (view.sunsetMin / 1440) * Math.PI * 2 - Math.PI / 2,
      );
      dayContext.stroke();
    }

    const path = ribbonPath(view);

    const [bloom, bloomContext] = layer(width, height);
    bloomContext.scale(scale, scale);
    bloomContext.filter = `blur(${(9 * scale).toFixed(1)}px)`;
    bloomContext.fillStyle = "#5F7BF0";
    bloomContext.fill(path);

    const [ribbon, ribbonContext] = layer(width, height);
    ribbonContext.scale(scale, scale);
    ribbonContext.fillStyle = profile(
      ribbonContext,
      "90,117,242",
      "196,208,255",
      0.55,
    );
    ribbonContext.fill(path);

    if (this.stir.length > 0) {
      const [stir, stirContext] = layer(width, height);
      stirContext.scale(scale, scale);
      stirContext.fillStyle = profile(
        stirContext,
        "242,244,255",
        "255,255,255",
        0.8,
      );
      stirContext.fill(path);
      const mask = new Path2D();
      for (const span of this.stir) {
        const from = Math.max(0, span.startMin - 0.4);
        const to = Math.min(
          view.spanMin,
          Math.max(span.endMin, span.startMin + 0.3) + 0.4,
        );
        wedge(mask, view.bedMin + from, view.bedMin + to, BLOOM_REACH);
      }
      const [maskCanvas, maskContext] = layer(width, height);
      maskContext.scale(scale, scale);
      maskContext.filter = `blur(${(2.2 * scale).toFixed(1)}px)`;
      maskContext.fillStyle = "#fff";
      maskContext.fill(mask);
      stirContext.setTransform(1, 0, 0, 1, 0, 0);
      stirContext.globalCompositeOperation = "destination-in";
      stirContext.drawImage(maskCanvas, 0, 0);
      ribbonContext.setTransform(1, 0, 0, 1, 0, 0);
      ribbonContext.drawImage(stir, 0, 0);
    }

    this.layers = { face, day, bloom, ribbon };
  }

  private tick(time: number): void {
    this.frame = requestAnimationFrame((next) => this.tick(next));
    let elapsed = 0.016;
    if (this.lastTick > 0) {
      elapsed = Math.min(0.05, (time - this.lastTick) / 1000);
    }
    this.lastTick = time;
    this.draw(time, elapsed);
  }

  private draw(time: number, elapsed: number): void {
    const context = this.context;
    context.setTransform(1, 0, 0, 1, 0, 0);
    context.clearRect(0, 0, this.canvas.width, this.canvas.height);
    if (this.view === null || this.layers === null) {
      return;
    }
    const view = this.view;
    const still = this.reducedMotion.matches;
    const since = time - this.shownAt;

    let faceAlpha = 1;
    let sweep = 1;
    if (!still) {
      faceAlpha = easeOut(clamp01(since / FACE_MS));
      sweep = easeOut(clamp01((since - SWEEP_DELAY_MS) / SWEEP_MS));
    }
    let breathe = 0.75;
    if (since > BREATHE_DELAY_MS) {
      const phase = ((since - BREATHE_DELAY_MS) / BREATHE_MS + BREATHE_START) % 2;
      let wave = phase;
      if (phase > 1) {
        wave = 2 - phase;
      }
      breathe = 0.55 + 0.3 * (0.5 - 0.5 * Math.cos(Math.PI * wave));
    }

    context.globalAlpha = faceAlpha;
    context.drawImage(this.layers.face, 0, 0);
    context.drawImage(this.layers.day, 0, 0);

    context.save();
    if (sweep < 1) {
      const clip = new Path2D();
      context.scale(this.scale, this.scale);
      wedge(
        clip,
        view.bedMin,
        view.bedMin + Math.max(0.01, sweep * (view.spanMin + 2)),
        BLOOM_REACH + 40,
      );
      context.clip(clip);
      context.setTransform(1, 0, 0, 1, 0, 0);
    }
    context.globalAlpha = breathe;
    context.drawImage(this.layers.bloom, 0, 0);
    context.globalAlpha = 1;
    context.drawImage(this.layers.ribbon, 0, 0);
    context.restore();

    this.drawHand(elapsed, still);
  }

  private drawHand(elapsed: number, still: boolean): void {
    const context = this.context;
    let target = 0;
    if (this.cursor !== null) {
      target = 1;
    }
    this.handTarget = target;
    if (still) {
      this.handOpacity = target;
      this.hand.jump(this.hand.target);
    } else {
      const step = (elapsed * 1000) / HAND_FADE_MS;
      if (this.handOpacity < this.handTarget) {
        this.handOpacity = Math.min(this.handTarget, this.handOpacity + step);
      } else {
        this.handOpacity = Math.max(this.handTarget, this.handOpacity - step);
      }
      this.hand.step(elapsed);
    }

    context.save();
    context.scale(this.scale, this.scale);
    if (this.highlight !== null && this.view !== null) {
      const from = this.view.bedMin + this.highlight.startMin;
      const to =
        this.view.bedMin +
        Math.max(this.highlight.endMin, this.highlight.startMin + 0.6);
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
  }
}
