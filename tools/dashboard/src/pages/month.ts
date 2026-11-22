import type { Month } from "../api.ts";
import { MonthBars } from "../components/bars.ts";
import { ICONS, fadeWhileMore, h, svg } from "../dom.ts";
import type { Part } from "../view/format.ts";
import { monthView, type MonthDay } from "../view/month.ts";
import type { Insight } from "../view/night.ts";
import { header, type Page } from "./shell.ts";

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
    { class: "metric", style: `animation-delay:${(index * 0.035).toFixed(3)}s` },
    value,
    label,
  );
  return { element, value, label };
}

function nightRow(day: MonthDay): HTMLElement {
  const night = day.night;
  if (night === null) {
    return h(
      "div",
      { class: "row missing", role: "listitem" },
      h("div", { class: "tm" }, day.dayText),
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
      "aria-label": `${day.dateText}: ${night.sleep} asleep, to bed at ${night.bedtime}`,
    },
    h("div", { class: "tm" }, day.dayText),
    h("div", {}, h("b", { class: "kd" }, night.sleep), h("b", { class: "du" }, night.bedtime)),
    h("div", { class: "pl", "aria-hidden": "true" }, svg(ICONS.next)),
  );
}

export function monthPage(month: Month, synced: string | null): Page {
  const view = monthView(month);
  const recorded: number[] = [];
  view.days.forEach((day, index) => {
    if (day.night !== null) {
      recorded.push(index);
    }
  });

  const insights = view.insights.map((insight, index) => metric(insight, index));
  const first = insights[0];

  const canvas = h("canvas", {
    role: "img",
    "aria-label": `The last 30 nights, newest at the top, from bed to up on a shared clock; each swells where the room was louder while asleep. A gold line marks sunrise.`,
  });
  const barsBox = h("div", { class: "bars" }, canvas);
  const bars = new MonthBars(canvas, view);

  const rows: HTMLElement[] = [];
  view.days.forEach((day) => rows.push(nightRow(day)));
  const list = h("div", { class: "list", role: "list", "aria-label": "Nights, newest first" }, rows);
  const refreshFade = fadeWhileMore(list);
  requestAnimationFrame(refreshFade);

  function show(day: number | null): void {
    bars.setHover(day);
    rows.forEach((row, index) => row.classList.toggle("on", index === day));
    const night = day === null ? null : view.days[day].night;
    if (night === null || day === null) {
      first.value.replaceChildren(...values(view.insights[0].parts));
      first.label.textContent = view.insights[0].label;
      return;
    }
    first.value.replaceChildren(...values(night.sleepParts));
    first.label.textContent = `asleep, ${view.days[day].dateText}`;
  }

  rows.forEach((row, index) => {
    row.addEventListener("pointerenter", () => show(index));
    row.addEventListener("pointerleave", () => show(null));
    row.addEventListener("focus", () => show(index));
    row.addEventListener("blur", () => show(null));
  });
  barsBox.addEventListener("pointermove", (event) => {
    const day = bars.dayAt(event.clientX, event.clientY);
    show(day);
    barsBox.style.cursor = day !== null && view.days[day].night !== null ? "pointer" : "";
  });
  barsBox.addEventListener("pointerleave", () => show(null));
  barsBox.addEventListener("click", (event) => {
    const day = bars.dayAt(event.clientX, event.clientY);
    if (day === null) {
      return;
    }
    const night = view.days[day].night;
    if (night !== null) {
      location.hash = `#/night/${night.sessionId}`;
    }
  });

  const previous = h(
    "a",
    {
      class: "icon",
      href: view.previousLast ? `#/30/${view.previousLast}` : null,
      "aria-label": "A week earlier",
      "aria-disabled": view.previousLast ? null : "true",
    },
    svg(ICONS.previous),
  );
  const next = h(
    "a",
    {
      class: "icon",
      href: view.nextLast ? `#/30/${view.nextLast}` : null,
      "aria-label": view.nextLast ? "A week later" : "A week later, none yet",
      "aria-disabled": view.nextLast ? null : "true",
    },
    svg(ICONS.next),
  );

  const element = h(
    "div",
    { class: "wrap" },
    h("h1", { class: "sr" }, `30 days to ${view.rangeText}`),
    header("month", synced, [
      h("div", { class: "well date" }, previous, h("div", { class: "d wide" }, view.rangeText), next),
    ]),
    h(
      "div",
      { class: "night month" },
      h(
        "section",
        { class: "insights", "aria-label": "These 30 days" },
        insights.map((item) => item.element),
      ),
      h("div", { class: "dial-col" }, barsBox),
      h(
        "section",
        { class: "sounds", "aria-label": "Every night" },
        h("div", { class: "sounds-head" }, h("span", { class: "head" }, "Nights"), h("span", { class: "count" }, `${recorded.length} of 30`)),
        list,
      ),
    ),
  );

  return {
    element,
    leave() {
      bars.destroy();
    },
  };
}
