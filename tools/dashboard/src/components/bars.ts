import { PROFILE_STEP_MIN, type MonthDay, type MonthView } from "../view/month.ts";

const TOP = 30;
const LEFT = 58;
const RIGHT = 12;
const THIN = 1.4;
const REVEAL_MS = 900;
const ROW_DELAY_MS = 14;
const FONT = '"Albert Sans", system-ui, sans-serif';

function layer(width: number, height: number): [HTMLCanvasElement, CanvasRenderingContext2D] {
  const canvas = document.createElement("canvas");
  canvas.width = width;
  canvas.height = height;
  const context = canvas.getContext("2d");
  if (context === null) {
    throw new Error("no 2d canvas");
  }
  return [canvas, context];
}

function easeOut(t: number): number {
  return 1 - Math.pow(1 - t, 3);
}

export class MonthBars {
  private canvas: HTMLCanvasElement;
  private context: CanvasRenderingContext2D;
  private view: MonthView;
  private width = 0;
  private height = 0;
  private scale = 1;
  private base: HTMLCanvasElement | null = null;
  private bars: HTMLCanvasElement | null = null;
  private bright: HTMLCanvasElement | null = null;
  private hovered: number | null = null;
  private shownAt = 0;
  private frame = 0;
  private observer: ResizeObserver;
  private still = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  constructor(canvas: HTMLCanvasElement, view: MonthView) {
    this.canvas = canvas;
    const context = canvas.getContext("2d");
    if (context === null) {
      throw new Error("no 2d canvas");
    }
    this.context = context;
    this.view = view;
    this.observer = new ResizeObserver(() => this.resize());
    this.observer.observe(canvas);
  }

  destroy(): void {
    this.observer.disconnect();
    cancelAnimationFrame(this.frame);
  }

  setHover(day: number | null): void {
    if (day !== null && this.view.days[day].night === null) {
      day = null;
    }
    if (day === this.hovered) {
      return;
    }
    this.hovered = day;
    this.request();
  }

  dayAt(clientX: number, clientY: number): number | null {
    const box = this.canvas.getBoundingClientRect();
    const x = clientX - box.left;
    const y = clientY - box.top;
    if (x < 0 || x > box.width || y < TOP) {
      return null;
    }
    const day = Math.floor((y - TOP) / this.rowHeight());
    if (day < 0 || day >= this.view.days.length) {
      return null;
    }
    return day;
  }

  private rowHeight(): number {
    return (this.height - TOP) / this.view.days.length;
  }

  private rowCentre(day: number): number {
    return TOP + (day + 0.5) * this.rowHeight();
  }

  private xOf(clockMin: number): number {
    const span = this.view.endX - this.view.startX;
    return LEFT + ((clockMin - this.view.startX) / span) * (this.width - LEFT - RIGHT);
  }

  private resize(): void {
    const box = this.canvas.getBoundingClientRect();
    if (box.width === 0 || box.height === 0) {
      return;
    }
    this.width = box.width;
    this.height = box.height;
    this.scale = window.devicePixelRatio || 1;
    this.canvas.width = Math.round(this.width * this.scale);
    this.canvas.height = Math.round(this.height * this.scale);
    this.base = this.drawBase();
    this.bars = this.drawBars("90,117,242", "196,208,255", 0.55);
    this.bright = this.drawBars("170,186,255", "255,255,255", 0.8);
    if (this.shownAt === 0) {
      this.shownAt = performance.now();
    }
    this.request();
  }

  private drawBase(): HTMLCanvasElement {
    const [canvas, context] = layer(this.canvas.width, this.canvas.height);
    context.scale(this.scale, this.scale);
    const rowHeight = this.rowHeight();

    this.view.days.forEach((day: MonthDay, index: number) => {
      const top = TOP + index * rowHeight;
      if (day.free) {
        context.fillStyle = "rgba(255,255,255,.028)";
        context.beginPath();
        context.roundRect(LEFT - 6, top + 1, this.width - LEFT - RIGHT + 12, rowHeight - 2, 6);
        context.fill();
      }
      context.font = `400 11px ${FONT}`;
      context.textBaseline = "middle";
      context.textAlign = "left";
      context.fillStyle = day.night === null ? "rgba(232,235,244,.3)" : "rgba(232,235,244,.52)";
      context.fillText(day.dayText, 0, top + rowHeight / 2);
    });

    context.font = `400 11px ${FONT}`;
    context.textAlign = "center";
    context.textBaseline = "alphabetic";
    for (const tick of this.view.ticks) {
      const x = this.xOf(tick.x);
      context.fillStyle = "rgba(232,235,244,.52)";
      context.fillText(tick.label, x, 14);
      context.fillStyle = "rgba(255,255,255,.05)";
      context.fillRect(Math.round(x * this.scale) / this.scale, TOP - 6, 1, this.height - TOP + 6);
    }

    context.strokeStyle = "rgba(233,179,126,.75)";
    context.lineWidth = 1.25;
    context.lineJoin = "round";
    context.lineCap = "round";
    for (const run of this.view.sunrise) {
      context.beginPath();
      run.forEach((point, index) => {
        const x = this.xOf(point.x);
        const y = this.rowCentre(point.day);
        if (index === 0) {
          context.moveTo(x, y);
        } else {
          context.lineTo(x, y);
        }
      });
      if (run.length === 1) {
        context.moveTo(this.xOf(run[0].x), this.rowCentre(run[0].day) - 3);
        context.lineTo(this.xOf(run[0].x), this.rowCentre(run[0].day) + 3);
      }
      context.stroke();
    }
    return canvas;
  }

  private barPath(day: number): Path2D | null {
    const night = this.view.days[day].night;
    if (night === null) {
      return null;
    }
    const centre = this.rowCentre(day);
    const thick = Math.max(THIN + 1, this.rowHeight() * 0.36);
    const top: [number, number][] = [];
    const bottom: [number, number][] = [];
    const span = night.upX - night.bedX;
    night.profile.forEach((level, index) => {
      const minute = Math.min(span, index * PROFILE_STEP_MIN);
      const half = THIN + (thick - THIN) * level;
      const x = this.xOf(night.bedX + minute);
      top.push([x, centre - half]);
      bottom.push([x, centre + half]);
    });
    const path = new Path2D();
    if (top.length === 0) {
      return path;
    }
    path.moveTo(top[0][0], top[0][1]);
    for (const [x, y] of top) {
      path.lineTo(x, y);
    }
    path.arc(top[top.length - 1][0], centre, THIN, -Math.PI / 2, Math.PI / 2);
    for (let index = bottom.length - 1; index >= 0; index--) {
      path.lineTo(bottom[index][0], bottom[index][1]);
    }
    path.arc(top[0][0], centre, THIN, Math.PI / 2, (Math.PI * 3) / 2);
    path.closePath();
    return path;
  }

  private drawBars(edge: string, core: string, flank: number): HTMLCanvasElement {
    const [canvas, context] = layer(this.canvas.width, this.canvas.height);
    context.scale(this.scale, this.scale);
    const rowHeight = this.rowHeight();
    const thick = Math.max(THIN + 1, rowHeight * 0.36);
    for (let day = 0; day < this.view.days.length; day++) {
      const path = this.barPath(day);
      if (path === null) {
        continue;
      }
      const centre = this.rowCentre(day);
      context.save();
      context.filter = `blur(${(4 * this.scale).toFixed(1)}px)`;
      context.globalAlpha = 0.75;
      context.fillStyle = "#5F7BF0";
      context.fill(path);
      context.restore();
      const gradient = context.createLinearGradient(0, centre - thick, 0, centre + thick);
      gradient.addColorStop(0, `rgba(${edge},0)`);
      gradient.addColorStop(0.28, `rgba(${edge},${flank})`);
      gradient.addColorStop(0.5, `rgba(${core},1)`);
      gradient.addColorStop(0.72, `rgba(${edge},${flank})`);
      gradient.addColorStop(1, `rgba(${edge},0)`);
      context.fillStyle = gradient;
      context.fill(path);
    }
    return canvas;
  }

  private request(): void {
    cancelAnimationFrame(this.frame);
    this.frame = requestAnimationFrame(() => this.draw());
  }

  private draw(): void {
    const context = this.context;
    const base = this.base;
    const bars = this.bars;
    const bright = this.bright;
    if (base === null || bars === null || bright === null) {
      return;
    }
    const scale = this.scale;
    context.setTransform(1, 0, 0, 1, 0, 0);
    context.clearRect(0, 0, this.canvas.width, this.canvas.height);

    const elapsed = performance.now() - this.shownAt;
    let animating = false;
    let baseAlpha = 1;
    if (!this.still) {
      baseAlpha = Math.min(1, elapsed / 450);
      animating = baseAlpha < 1;
    }
    context.globalAlpha = baseAlpha;
    context.drawImage(base, 0, 0);
    context.globalAlpha = 1;

    const rowHeight = this.rowHeight();
    const plotLeft = LEFT - 8;
    const plotWidth = this.width - plotLeft;
    for (let day = 0; day < this.view.days.length; day++) {
      if (this.view.days[day].night === null) {
        continue;
      }
      let reveal = 1;
      if (!this.still) {
        reveal = easeOut(Math.max(0, Math.min(1, (elapsed - day * ROW_DELAY_MS) / REVEAL_MS)));
        if (reveal < 1) {
          animating = true;
        }
      }
      if (reveal <= 0) {
        continue;
      }
      const sy = (TOP + day * rowHeight - rowHeight * 0.5) * scale;
      const sh = rowHeight * 2 * scale;
      const sx = plotLeft * scale;
      const sw = plotWidth * reveal * scale;
      let source = bars;
      context.globalAlpha = 1;
      if (this.hovered !== null) {
        if (this.hovered === day) {
          source = bright;
        } else {
          context.globalAlpha = 0.5;
        }
      }
      context.drawImage(source, sx, sy, sw, sh, sx, sy, sw, sh);
    }
    context.globalAlpha = 1;

    if (this.hovered !== null) {
      this.drawLabels(this.hovered);
    }
    if (animating) {
      this.frame = requestAnimationFrame(() => this.draw());
    }
  }

  private drawLabels(day: number): void {
    const night = this.view.days[day].night;
    if (night === null) {
      return;
    }
    const context = this.context;
    context.setTransform(this.scale, 0, 0, this.scale, 0, 0);
    const centre = this.rowCentre(day);
    context.textBaseline = "middle";

    const label = (x: number, align: CanvasTextAlign, key: string, value: string, colour: string): void => {
      context.textAlign = align;
      context.font = `500 12px ${FONT}`;
      const valueWidth = context.measureText(value).width;
      context.font = `400 11px ${FONT}`;
      const keyWidth = context.measureText(`${key} `).width;
      const width = keyWidth + valueWidth;
      let left = x;
      if (align === "right") {
        left = x - width;
      }
      context.fillStyle = "rgba(6,7,11,.82)";
      context.beginPath();
      context.roundRect(left - 6, centre - 10, width + 12, 20, 6);
      context.fill();
      context.textAlign = "left";
      context.fillStyle = "rgba(232,235,244,.52)";
      context.font = `400 11px ${FONT}`;
      context.fillText(`${key} `, left, centre);
      context.fillStyle = colour;
      context.font = `500 12px ${FONT}`;
      context.fillText(value, left + keyWidth, centre);
    };

    label(this.xOf(night.bedX) - 10, "right", "Bed", night.bed, "#eef0f5");
    let upEnd = this.xOf(night.upX) + 10;
    label(upEnd, "left", "Up", night.up, "#eef0f5");
    if (night.sunrise !== null && night.sunriseX !== null) {
      context.font = `500 12px ${FONT}`;
      upEnd += context.measureText(`Up ${night.up}`).width + 22;
      const at = Math.max(upEnd, this.xOf(night.sunriseX) + 8);
      if (at < this.width - 60) {
        label(at, "left", "Sunrise", night.sunrise, "#e9b37e");
      }
    }
  }
}
