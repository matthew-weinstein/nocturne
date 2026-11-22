import type { Trends } from "../api.ts";
import { pillMenu, type PillOption } from "../components/menu.ts";
import { h } from "../dom.ts";
import type { Part } from "../view/format.ts";
import {
  CHART_H,
  CHART_W,
  OVER_TIME,
  RANGES,
  hoverAt,
  hoverView,
  trendsView,
  type Label,
  type TrendsChoice,
  type TrendsView,
} from "../view/trends.ts";
import { header, type Page } from "./shell.ts";

const SVG_NS = "http://www.w3.org/2000/svg";

export function trendsHash(choice: TrendsChoice): string {
  return `#/trends/${choice.range}/${choice.metric}/${choice.factor}`;
}

function node(tag: string, attributes: Record<string, string | number>): SVGElement {
  const element = document.createElementNS(SVG_NS, tag);
  for (const [name, value] of Object.entries(attributes)) {
    element.setAttribute(name, String(value));
  }
  return element;
}

function at(x: number, y: number): string {
  return `left:${((x / CHART_W) * 100).toFixed(2)}%;top:${((y / CHART_H) * 100).toFixed(2)}%`;
}

function labels(list: Label[], className: string): HTMLElement[] {
  const nodes: HTMLElement[] = [];
  for (const label of list) {
    nodes.push(h("div", { class: className, style: at(label.x, label.y) }, label.text));
  }
  return nodes;
}

function figureNodes(parts: Part[]): HTMLElement[] {
  const nodes: HTMLElement[] = [];
  for (const part of parts) {
    nodes.push(h("b", { class: "n" }, part.n));
    if (part.u !== "") {
      const clock = part.u === "AM" || part.u === "PM";
      nodes.push(h("b", { class: clock ? "u ap" : "u" }, part.u));
    }
  }
  return nodes;
}

function rangeControl(index: number, onChoose: (range: TrendsChoice["range"]) => void): HTMLElement {
  const buttons: HTMLElement[] = [];
  RANGES.forEach((range, position) => {
    const button = h("button", { type: "button", "aria-pressed": String(position === index) }, range.label);
    button.addEventListener("click", () => onChoose(range.name));
    buttons.push(button);
  });
  return h(
    "div",
    { class: "well segmented range", role: "group", "aria-label": "Range", style: `--at:${index}` },
    h("div", { class: "ind", "aria-hidden": "true" }),
    buttons,
  );
}

export function trendsPage(data: Trends, initial: TrendsChoice, synced: string | null): Page {
  const choice: TrendsChoice = { ...initial };
  let view: TrendsView = trendsView(data, choice);

  const control = rangeControl(view.rangeIndex, (range) => choose({ range }, "range"));
  const ask = h("div", { class: "ask" });
  const big = h("div", { class: "big" });
  const sub = h("div", { class: "sub" });
  const centre = h("div", { class: "ctr" }, ask, h("div", { "aria-live": "polite" }, big, sub));

  const chartSvg = node("svg", { viewBox: `0 0 ${CHART_W} ${CHART_H}`, role: "img" });
  const defs = node("defs", {});
  const blur = node("filter", { id: "trend-blur", x: "-10%", y: "-30%", width: "120%", height: "160%" });
  blur.append(node("feGaussianBlur", { stdDeviation: 5 }));
  defs.append(blur);
  const face = node("g", { class: "face" });
  const plot = node("g", { class: "plot" });
  const hair = node("path", { fill: "none", stroke: "rgba(255,255,255,.28)", "stroke-width": 1 });
  const pick = node("path", { fill: "#F2F4FF" });
  chartSvg.append(defs, face, plot);
  const marks = h("div", { class: "marks" });
  const chart = h("div", { class: "chart" }, centre, chartSvg, marks);

  let hovered: number | null = null;

  function choose(change: Partial<TrendsChoice>, focus: string | null): void {
    Object.assign(choice, change);
    history.replaceState(null, "", trendsHash(choice));
    render(focus);
  }

  function showFigure(day: number | null): void {
    hovered = day;
    const hover = day === null ? null : hoverView(view, data, day);
    if (hover === null) {
      big.replaceChildren(...figureNodes(view.figure));
      sub.textContent = view.sub;
      hair.setAttribute("d", "");
      pick.setAttribute("d", "");
      return;
    }
    big.replaceChildren(...figureNodes(hover.figure));
    sub.textContent = hover.sub;
    hair.setAttribute("d", hover.hair);
    pick.setAttribute("d", hover.pick);
  }

  function render(focus: string | null): void {
    view = trendsView(data, choice);
    if (view.factor === null) {
      choice.factor = OVER_TIME;
    }

    control.style.setProperty("--at", String(view.rangeIndex));
    control.querySelectorAll("button").forEach((button, index) => {
      button.setAttribute("aria-pressed", String(index === view.rangeIndex));
    });

    const metricOptions: PillOption[] = [];
    for (const item of view.metricItems) {
      metricOptions.push({
        value: item.name,
        children: [h("span", {}, item.title), h("span", { class: "av" }, item.average)],
      });
    }
    const factorOptions: PillOption[] = [];
    for (const item of view.factorItems) {
      const children: Node[] = [h("span", {}, item.label)];
      if (item.meter) {
        children.push(
          h(
            "span",
            { class: "meter" },
            h("span", {}, item.word),
            h("i", { "aria-hidden": "true" }, h("b", { style: `width:${Math.round((item.strength ?? 0) * 100)}%` })),
          ),
        );
      }
      factorOptions.push({ value: item.name, children });
    }
    const metricMenu = pillMenu(view.metric.title, "Show", metricOptions, view.metric.name, (metric) => {
      choose({ metric }, "metric");
    });
    const factorMenu = pillMenu(
      view.factorText,
      "Against",
      factorOptions,
      choice.factor,
      (factor) => choose({ factor }, "factor"),
      "Strongest link first",
    );
    ask.replaceChildren(metricMenu, h("span", {}, view.joiner), factorMenu);

    chartSvg.setAttribute("aria-label", view.summary);
    face.replaceChildren();
    for (const band of view.bands) {
      face.append(
        node("rect", {
          x: band.x.toFixed(1),
          y: band.y,
          width: band.width.toFixed(1),
          height: band.height,
          rx: band.radius.toFixed(1),
          fill: "rgba(255,255,255,.028)",
        }),
      );
    }
    face.append(node("path", { d: view.grid, fill: "none", stroke: "rgba(232,235,244,.06)", "stroke-width": 1 }));
    face.append(
      node("path", {
        d: view.target,
        fill: "none",
        stroke: "rgba(232,235,244,.32)",
        "stroke-width": 1,
        "stroke-dasharray": "3 5",
      }),
    );

    const stroke = { fill: "none", "stroke-linecap": "round", "stroke-linejoin": "round" };
    const fresh = node("g", { class: "plot" });
    fresh.append(
      node("path", { ...stroke, class: "bloom", d: view.line, stroke: "#5F7BF0", "stroke-width": 7, filter: "url(#trend-blur)" }),
      node("path", { d: view.dots, fill: view.dotFill }),
      node("path", { ...stroke, d: view.line, stroke: "#C4D0FF", "stroke-width": 1.8 }),
      node("path", { ...stroke, d: view.means, stroke: "#F2F4FF", "stroke-width": 2 }),
      hair,
      pick,
    );
    chartSvg.querySelector(".plot")?.replaceWith(fresh);

    marks.replaceChildren(
      ...labels(view.yLabels, "ya"),
      ...labels(view.xLabels, "xt"),
      ...labels(view.tags, "tg"),
    );

    showFigure(null);

    if (focus === "metric") {
      metricMenu.querySelector("button")?.focus();
    } else if (focus === "factor") {
      factorMenu.querySelector("button")?.focus();
    } else if (focus === "range") {
      control.querySelector<HTMLElement>('[aria-pressed="true"]')?.focus();
    }
  }

  chart.addEventListener("pointermove", (event) => {
    if (ask.contains(event.target as Node)) {
      return;
    }
    const box = chart.getBoundingClientRect();
    const x = ((event.clientX - box.left) / box.width) * CHART_W;
    const y = ((event.clientY - box.top) / box.height) * CHART_H;
    const day = hoverAt(view, x, y);
    if (day !== hovered) {
      showFigure(day);
    }
  });
  chart.addEventListener("pointerleave", () => showFigure(null));

  render(null);

  let note: HTMLElement | null = null;
  if (data.nights.length === 0) {
    note = h("p", { class: "page-note" }, "No nights yet. Run the pipeline, then reload.");
  }

  const element = h(
    "div",
    { class: "wrap" },
    h("h1", { class: "sr" }, "Trends"),
    header("trends", synced, [control]),
    note,
    h("div", { class: "trends" }, chart),
  );

  return {
    element,
    leave() {},
  };
}
