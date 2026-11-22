import type { ChartView, Point } from "../view/trends.ts";

const PAD = { top: 16, right: 16, bottom: 34, left: 64 };
const DOT = 3.6;
const HIT = 14;
const FONT = '"Albert Sans", system-ui, sans-serif';
const ACC = "143,165,255";
const GROUP_HALF = 0.32;

/** A chart of nights: over time, against a number, or by group; hovering a dot reports its night. */
export class TrendChart {
  private canvas: HTMLCanvasElement;
  private context: CanvasRenderingContext2D;
  private view: ChartView;
  private onHover: (point: Point | null) => void;
  private width = 0;
  private height = 0;
  private scale = 1;
  private hovered: Point | null = null;
  private observer: ResizeObserver;
  private frame = 0;

  constructor(canvas: HTMLCanvasElement, view: ChartView, onHover: (point: Point | null) => void) {
    this.canvas = canvas;
    const context = canvas.getContext("2d");
    if (context === null) {
      throw new Error("no 2d canvas");
    }
    this.context = context;
    this.view = view;
    this.onHover = onHover;
    this.observer = new ResizeObserver(() => this.resize());
    this.observer.observe(canvas);
    canvas.addEventListener("pointermove", (event) => this.hover(event.clientX, event.clientY));
    canvas.addEventListener("pointerleave", () => this.setHovered(null));
  }

  destroy(): void {
    this.observer.disconnect();
    cancelAnimationFrame(this.frame);
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
    this.request();
  }

  private request(): void {
    cancelAnimationFrame(this.frame);
    this.frame = requestAnimationFrame(() => this.draw());
  }

  private xOf(value: number): number {
    const span = this.view.xMax - this.view.xMin;
    return PAD.left + ((value - this.view.xMin) / span) * (this.width - PAD.left - PAD.right);
  }

  private yOf(value: number): number {
    const span = this.view.yMax - this.view.yMin;
    return this.height - PAD.bottom - ((value - this.view.yMin) / span) * (this.height - PAD.top - PAD.bottom);
  }

  private hover(clientX: number, clientY: number): void {
    const box = this.canvas.getBoundingClientRect();
    const x = clientX - box.left;
    const y = clientY - box.top;
    let nearest: Point | null = null;
    let best = HIT * HIT;
    for (const point of this.view.points) {
      const distance = (this.dotX(point) - x) ** 2 + (this.yOf(point.y) - y) ** 2;
      if (distance < best) {
        best = distance;
        nearest = point;
      }
    }
    this.setHovered(nearest);
  }

  private setHovered(point: Point | null): void {
    if (point === this.hovered) {
      return;
    }
    this.hovered = point;
    this.onHover(point);
    this.request();
  }

  private dotX(point: Point): number {
    if (this.view.kind === "weekday" || this.view.kind === "free") {
      return this.xOf(point.x + this.jitter(point.nightOf));
    }
    return this.xOf(point.x);
  }

  private jitter(nightOf: string): number {
    let hash = 0;
    for (let index = 0; index < nightOf.length; index++) {
      hash = (hash * 31 + nightOf.charCodeAt(index)) % 997;
    }
    return ((hash / 997) * 2 - 1) * GROUP_HALF * 0.8;
  }

  private draw(): void {
    const context = this.context;
    const view = this.view;
    context.setTransform(this.scale, 0, 0, this.scale, 0, 0);
    context.clearRect(0, 0, this.width, this.height);
    const bottom = this.height - PAD.bottom;

    if (view.shadeFree) {
      context.fillStyle = "rgba(255,255,255,.03)";
      const day = this.xOf(1) - this.xOf(0);
      for (const point of view.points) {
        if (point.free) {
          context.fillRect(this.xOf(point.x) - day / 2, PAD.top, day, bottom - PAD.top);
        }
      }
    }

    context.font = `400 11px ${FONT}`;
    context.textBaseline = "middle";
    context.textAlign = "right";
    for (const tick of view.yTicks) {
      const y = Math.round(this.yOf(tick.y)) + 0.5;
      context.fillStyle = "rgba(255,255,255,.06)";
      context.fillRect(PAD.left, y, this.width - PAD.left - PAD.right, 1);
      context.fillStyle = "rgba(232,235,244,.52)";
      context.fillText(tick.label, PAD.left - 14, y);
    }
    context.textAlign = "center";
    context.textBaseline = "alphabetic";
    for (const tick of view.xTicks) {
      context.fillText(tick.label, this.xOf(tick.x), this.height - 10);
    }

    if (view.target !== null) {
      const y = this.yOf(view.target);
      context.save();
      context.setLineDash([4, 5]);
      context.strokeStyle = "rgba(232,235,244,.38)";
      context.lineWidth = 1;
      context.beginPath();
      context.moveTo(PAD.left, y);
      context.lineTo(this.width - PAD.right, y);
      context.stroke();
      context.restore();
    }

    for (const run of view.mean) {
      context.save();
      context.strokeStyle = `rgba(${ACC},.9)`;
      context.lineWidth = 2;
      context.lineJoin = "round";
      context.lineCap = "round";
      context.shadowColor = "rgba(95,123,240,.7)";
      context.shadowBlur = 10;
      context.beginPath();
      run.forEach((point, index) => {
        if (index === 0) {
          context.moveTo(this.xOf(point.x), this.yOf(point.y));
        } else {
          context.lineTo(this.xOf(point.x), this.yOf(point.y));
        }
      });
      if (run.length === 1) {
        context.lineTo(this.xOf(run[0].x) + 0.01, this.yOf(run[0].y));
      }
      context.stroke();
      context.restore();
    }

    if (view.fit !== null) {
      const fit = view.fit;
      context.save();
      context.strokeStyle = `rgba(${ACC},.9)`;
      context.lineWidth = 2;
      context.shadowColor = "rgba(95,123,240,.7)";
      context.shadowBlur = 10;
      context.beginPath();
      context.rect(PAD.left, PAD.top, this.width - PAD.left - PAD.right, bottom - PAD.top);
      context.clip();
      context.beginPath();
      context.moveTo(this.xOf(view.xMin), this.yOf(fit.intercept + fit.slope * view.xMin));
      context.lineTo(this.xOf(view.xMax), this.yOf(fit.intercept + fit.slope * view.xMax));
      context.stroke();
      context.restore();
    }

    const dotAlpha = view.mean.length > 0 ? 0.42 : 0.7;
    for (const point of view.points) {
      if (point === this.hovered) {
        continue;
      }
      context.fillStyle = `rgba(${ACC},${dotAlpha})`;
      context.beginPath();
      context.arc(this.dotX(point), this.yOf(point.y), DOT, 0, Math.PI * 2);
      context.fill();
    }

    for (const group of view.groups) {
      const x0 = this.xOf(group.x - GROUP_HALF);
      const x1 = this.xOf(group.x + GROUP_HALF);
      const y = this.yOf(group.y);
      context.save();
      context.strokeStyle = "#ffffff";
      context.lineWidth = 2;
      context.lineCap = "round";
      context.shadowColor = "rgba(95,123,240,.8)";
      context.shadowBlur = 10;
      context.beginPath();
      context.moveTo(x0, y);
      context.lineTo(x1, y);
      context.stroke();
      context.restore();
    }

    if (this.hovered !== null) {
      const x = this.dotX(this.hovered);
      const y = this.yOf(this.hovered.y);
      context.fillStyle = "#ffffff";
      context.beginPath();
      context.arc(x, y, DOT + 1.4, 0, Math.PI * 2);
      context.fill();
      context.strokeStyle = `rgba(${ACC},.6)`;
      context.lineWidth = 1.5;
      context.beginPath();
      context.arc(x, y, DOT + 5, 0, Math.PI * 2);
      context.stroke();
    }
  }
}
