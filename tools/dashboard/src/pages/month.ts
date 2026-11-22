import type { Month } from "../api.ts";
import { choiceMenu } from "../components/menu.ts";
import { ICONS, fadeWhileMore, h, svg } from "../dom.ts";
import type { Part } from "../view/format.ts";
import { CHART_H, CHART_W, dayAt, monthView, type MonthDay } from "../view/month.ts";
import type { Insight } from "../view/night.ts";
import { header, type Page } from "./shell.ts";

const SVG_NS = "http://www.w3.org/2000/svg";

type Filter = "all" | "work" | "free";

function values(parts: Part[]): HTMLElement[] {
  const nodes: HTMLElement[] = [];
  for (const part of parts) {
    nodes.push(h("b", { class: "v" }, part.n));
    nodes.push(h("b", { class: "u" }, part.u));
  }
  return nodes;
}

function metric(insight: Insight, index: number): { element: HTMLElement; value: HTMLElement; label: HTMLElement } {
  const value = h("div", {}, values(insight.parts));
  const label = h("div", { class: "l" }, insight.label);
  const element = h(
    "div",
    { class: "metric", style: `animation-delay:${(0.5 + index * 0.09).toFixed(2)}s` },
    value,
    label,
  );
  return { element, value, label };
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

function dot(x: number, y: number, radius: number): string {
  return `M${(x - radius).toFixed(1)} ${y.toFixed(1)}a${radius} ${radius} 0 1 0 ${2 * radius} 0a${radius} ${radius} 0 1 0 ${-2 * radius} 0`;
}

function nightRow(day: MonthDay): HTMLElement {
  const night = day.night;
  if (night === null) {
    return h(
      "div",
      { class: "row missing", role: "listitem", "aria-label": `${day.longText}, not recorded` },
      h("div", { class: "tm" }, day.shortText),
      h("div", {}, h("b", { class: "kd" }, "Not recorded")),
      h("div", {}),
    );
  }
  return h(
    "a",
    {
      class: "row",
      role: "listitem",
      href: `#/night/${night.sessionId}`,
      "aria-label": `${day.longText}, ${night.sleepText} asleep, bed ${night.bedtime}`,
    },
    h("div", { class: "tm" }, day.shortText),
    h("div", {}, h("b", { class: "kd" }, night.sleepText), h("b", { class: "du" }, night.bedtime)),
    h("div", { class: "pl", "aria-hidden": "true" }, svg(ICONS.next)),
  );
}

export function monthPage(month: Month, synced: string | null): Page {
  const view = monthView(month);
  const insights = view.insights.map((insight, index) => metric(insight, index));
  const first = insights[0];

  const chartSvg = node("svg", { viewBox: `0 0 ${CHART_W} ${CHART_H}`, role: "img", "aria-label": view.summary });
  const defs = node("defs", {});
  const filter = node("filter", { id: "month-blur", x: "-10%", y: "-20%", width: "120%", height: "140%" });
  filter.append(node("feGaussianBlur", { stdDeviation: 6 }));
  defs.append(filter);

  const face = node("g", { class: "face" });
  for (const shade of view.shades) {
    face.append(
      node("rect", {
        x: shade.x,
        y: shade.y,
        width: shade.width,
        height: shade.height,
        rx: shade.radius,
        fill: "rgba(255,255,255,.03)",
      }),
    );
  }
  for (const line of view.grid) {
    face.append(
      node("path", {
        d: line.path,
        stroke: line.major ? "rgba(232,235,244,.1)" : "rgba(232,235,244,.04)",
        "stroke-width": 1,
      }),
    );
  }

  const bars = node("g", { class: "bars" });
  bars.append(node("path", { class: "bloom", d: view.glow, fill: "#5F7BF0", filter: "url(#month-blur)" }));
  const groups: (SVGElement | null)[] = [];
  const sleepPaths: (SVGElement | null)[] = [];
  for (const day of view.days) {
    if (day.night === null) {
      groups.push(null);
      sleepPaths.push(null);
      continue;
    }
    const group = node("g", { class: "nt" });
    group.append(node("path", { d: day.night.awake, fill: "rgba(143,165,255,.32)" }));
    const asleep = node("path", { d: day.night.asleep, fill: "#97ABFF" });
    group.append(asleep);
    bars.append(group);
    groups.push(group);
    sleepPaths.push(asleep);
  }
  bars.append(
    node("path", {
      d: view.sunline,
      fill: "none",
      stroke: "rgba(233,179,126,.6)",
      "stroke-width": 1.2,
      "stroke-linejoin": "round",
    }),
  );
  const sunDot = node("path", { d: "", fill: "#E9B37E" });
  bars.append(sunDot);
  chartSvg.append(defs, face, bars);

  const dates: HTMLElement[] = [];
  for (const day of view.days) {
    dates.push(h("div", { class: "dl", style: at(83, day.y) }, day.longText));
  }
  const hours: HTMLElement[] = [];
  for (const hour of view.hours) {
    hours.push(h("div", { class: "hl", style: at(hour.x, hour.y) }, hour.text));
  }
  const labelParts = (): { box: HTMLElement; key: HTMLElement; value: HTMLElement } => {
    const key = h("div", { class: "k" });
    const value = h("div", { class: "t" });
    return { box: h("div", { class: "lb", "aria-hidden": "true" }, key, value), key, value };
  };
  const bedLabel = labelParts();
  const upLabel = labelParts();
  const sunLabel = labelParts();
  sunLabel.value.classList.add("sun");

  const chart = h("div", { class: "chart" }, chartSvg, dates, hours, bedLabel.box, upLabel.box, sunLabel.box);

  const rows: HTMLElement[] = [];
  const rowDays: number[] = [];
  for (let index = view.days.length - 1; index >= 0; index--) {
    rows.push(nightRow(view.days[index]));
    rowDays.push(index);
  }
  const list = h("div", { class: "list", role: "list", "aria-label": "Nights, latest first" }, rows);
  const refreshFade = fadeWhileMore(list);
  requestAnimationFrame(refreshFade);

  let shown: number | null = null;

  function place(
    label: { box: HTMLElement; key: HTMLElement; value: HTMLElement },
    key: string,
    value: string | null,
    x: number,
    before: boolean,
  ): void {
    label.key.textContent = key;
    label.value.textContent = value ?? "";
    label.box.style.cssText = `${at(x, view.labelY)};transform:translate(${before ? "-100%" : "0"},-100%);text-align:${before ? "right" : "left"}`;
  }

  function show(day: number | null): void {
    if (day !== null && view.days[day].night === null) {
      day = null;
    }
    if (day === shown) {
      return;
    }
    shown = day;
    chart.classList.toggle("picked", day !== null);
    groups.forEach((group, index) => {
      group?.classList.toggle("on", index === day);
    });
    sleepPaths.forEach((path, index) => {
      path?.setAttribute("fill", index === day ? "#F2F4FF" : "#97ABFF");
    });
    dates.forEach((date, index) => date.classList.toggle("dim", day !== null && index !== day));
    rows.forEach((row, index) => row.classList.toggle("on", rowDays[index] === day));

    const night = day === null ? null : view.days[day].night;
    if (night === null || day === null) {
      sunDot.setAttribute("d", "");
      first.value.replaceChildren(...values(view.insights[0].parts));
      first.label.textContent = view.insights[0].label;
      return;
    }
    const y = view.days[day].y;
    sunDot.setAttribute("d", night.sunriseX === null ? "" : dot(night.sunriseX, y, 2.6));
    const upFirst = night.sunriseX === null || night.upX < night.sunriseX;
    place(bedLabel, "Bed", night.bed, night.bedX, false);
    place(upLabel, "Up", night.up, night.upX, upFirst);
    if (night.sunriseX === null) {
      sunLabel.box.style.display = "none";
    } else {
      place(sunLabel, "Sunrise", night.sunrise, night.sunriseX, !upFirst);
    }
    first.value.replaceChildren(...values(night.sleepParts));
    first.label.textContent = `sleep time · ${view.days[day].longText}`;
  }

  rows.forEach((row, index) => {
    const day = rowDays[index];
    row.addEventListener("pointerenter", () => show(day));
    row.addEventListener("pointerleave", () => show(null));
    row.addEventListener("focus", () => show(day));
    row.addEventListener("blur", () => show(null));
  });

  function dayUnder(event: MouseEvent): number | null {
    const box = chart.getBoundingClientRect();
    const x = ((event.clientX - box.left) / box.width) * CHART_W;
    const y = ((event.clientY - box.top) / box.height) * CHART_H;
    if (x < 0) {
      return null;
    }
    const day = dayAt(y);
    if (day === null || view.days[day].night === null) {
      return null;
    }
    return day;
  }
  chart.addEventListener("pointermove", (event) => {
    const day = dayUnder(event);
    show(day);
    chart.style.cursor = day === null ? "" : "pointer";
  });
  chart.addEventListener("pointerleave", () => show(null));
  chart.addEventListener("click", (event) => {
    const day = dayUnder(event);
    if (day === null) {
      return;
    }
    const night = view.days[day].night;
    if (night !== null) {
      location.hash = `#/night/${night.sessionId}`;
    }
  });

  function applyFilter(filter: Filter): void {
    rows.forEach((row, index) => {
      const day = view.days[rowDays[index]];
      let keep = true;
      if (filter === "work") {
        keep = !day.free;
      } else if (filter === "free") {
        keep = day.free;
      }
      row.hidden = !keep;
    });
    refreshFade();
  }
  const filterMenu = choiceMenu(
    "Every night",
    [
      { value: "all", label: "Every night" },
      { value: "work", label: "Work nights" },
      { value: "free", label: "Free nights" },
    ],
    "all",
    (value) => applyFilter(value as Filter),
  );
  filterMenu.classList.add("filter-menu");

  const previous = h(
    "a",
    {
      class: "icon",
      href: view.previousLast ? `#/30/${view.previousLast}` : null,
      "aria-label": "Back a week",
      "aria-disabled": view.previousLast ? null : "true",
    },
    svg(ICONS.previous),
  );
  const next = h(
    "a",
    {
      class: "icon",
      href: view.nextLast ? `#/30/${view.nextLast}` : null,
      "aria-label": view.nextLast ? "Forward a week" : "Forward a week, none yet",
      "aria-disabled": view.nextLast ? null : "true",
    },
    svg(ICONS.next),
  );

  const element = h(
    "div",
    { class: "wrap" },
    h("h1", { class: "sr" }, `30 nights, ${view.rangeText}`),
    header("month", synced, [
      h("div", { class: "well date" }, previous, h("div", { class: "d wide" }, view.rangeText), next),
    ]),
    h(
      "div",
      { class: "night month" },
      h(
        "section",
        { class: "insights", "aria-label": "Last 30 nights" },
        insights.map((item) => item.element),
      ),
      h("div", { class: "dial-col" }, chart),
      h(
        "section",
        { class: "sounds", "aria-label": "Nights" },
        h("div", { class: "sounds-head" }, filterMenu, h("span", { class: "count" }, String(view.count))),
        list,
      ),
    ),
  );

  return {
    element,
    leave() {},
  };
}
