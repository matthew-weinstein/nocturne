import type { Trends } from "../api.ts";
import { TrendChart } from "../components/chart.ts";
import { choiceMenu, type MenuChoice } from "../components/menu.ts";
import { h } from "../dom.ts";
import {
  OVER_TIME,
  RANGES,
  nightFigure,
  trendsView,
  type Point,
  type TrendsChoice,
  type TrendsView,
} from "../view/trends.ts";
import { header, type Page } from "./shell.ts";

export function trendsHash(choice: TrendsChoice): string {
  return `#/trends/${choice.range}/${choice.metric}/${choice.factor}`;
}

function rangeControl(choice: TrendsChoice, onChoose: (range: TrendsChoice["range"]) => void): HTMLElement {
  let at = 0;
  const buttons: HTMLElement[] = [];
  RANGES.forEach((range, index) => {
    if (range.name === choice.range) {
      at = index;
    }
    const button = h(
      "button",
      { type: "button", "aria-pressed": String(range.name === choice.range) },
      range.label,
    );
    button.addEventListener("click", () => onChoose(range.name));
    buttons.push(button);
  });
  return h(
    "div",
    { class: "well segmented range", role: "group", "aria-label": "Range", style: `--at:${at}` },
    h("div", { class: "ind", "aria-hidden": "true" }),
    buttons,
  );
}

export function trendsPage(data: Trends, initial: TrendsChoice, synced: string | null): Page {
  const choice: TrendsChoice = { ...initial };
  let chart: TrendChart | null = null;

  const rangeSlot = h("div", {});
  const sentence = h("h2", { class: "sentence" });
  const figure = h("div", { class: "figure", "aria-live": "polite" });
  const line = h("p", { class: "line" });
  const chartBox = h("div", { class: "chart" });
  const answer = h("div", { class: "answer" }, figure, line);

  function choose(change: Partial<TrendsChoice>, focus: string | null): void {
    Object.assign(choice, change);
    history.replaceState(null, "", trendsHash(choice));
    render(focus);
  }

  function showFigure(view: TrendsView, point: Point | null): void {
    if (point === null) {
      figure.textContent = view.figure;
      line.textContent = view.line;
      return;
    }
    const shown = nightFigure(view, point);
    figure.textContent = shown.figure;
    line.textContent = shown.line;
  }

  function render(focus: string | null): void {
    const view = trendsView(data, choice);
    if (view.factor === null) {
      choice.factor = OVER_TIME;
    }

    const previousRange = rangeSlot.querySelector<HTMLElement>(".range");
    const control = rangeControl(choice, (range) => choose({ range }, "range"));
    if (previousRange !== null) {
      const from = previousRange.style.getPropertyValue("--at");
      const to = control.style.getPropertyValue("--at");
      control.style.setProperty("--at", from);
      requestAnimationFrame(() => control.style.setProperty("--at", to));
    }
    rangeSlot.replaceChildren(control);

    const metricChoices: MenuChoice[] = [];
    for (const item of view.metricChoices) {
      metricChoices.push({ value: item.name, label: item.label, note: item.note });
    }
    const factorChoices: MenuChoice[] = [{ value: OVER_TIME, label: "Over time" }];
    for (const item of view.factorChoices) {
      factorChoices.push({
        value: item.name,
        label: item.label,
        note: item.strength.word,
        meter: item.strength.word === "Too few nights" ? undefined : item.strength.value ?? 0,
      });
    }
    let factorLabel = "time";
    if (view.factor !== null) {
      factorLabel = view.factor.label.toLowerCase();
    }
    const metricMenu = choiceMenu(view.metric.label, metricChoices, view.metric.name, (metric) => {
      choose({ metric }, "metric");
    });
    metricMenu.classList.add("metric-menu");
    const factorMenu = choiceMenu(factorLabel, factorChoices, choice.factor, (factor) => {
      choose({ factor }, "factor");
    });
    factorMenu.classList.add("factor-menu");
    sentence.replaceChildren(metricMenu, h("span", { class: "word" }, view.connective), factorMenu);

    showFigure(view, null);

    chart?.destroy();
    const canvas = h("canvas", { role: "img", "aria-label": view.summary });
    chartBox.replaceChildren(canvas, h("p", { class: "sr" }, view.summary));
    chart = new TrendChart(canvas, view.chart, (point) => showFigure(view, point));

    if (focus === "metric") {
      metricMenu.querySelector("button")?.focus();
    } else if (focus === "factor") {
      factorMenu.querySelector("button")?.focus();
    } else if (focus === "range") {
      control.querySelector<HTMLElement>('[aria-pressed="true"]')?.focus();
    }
  }

  render(null);

  let note: HTMLElement | null = null;
  if (data.nights.length === 0) {
    note = h("p", { class: "page-note" }, "No nights yet. Run the pipeline, then reload.");
  }

  const element = h(
    "div",
    { class: "wrap" },
    h("h1", { class: "sr" }, "Trends"),
    header("trends", synced, [rangeSlot]),
    note,
    h("div", { class: "trends" }, sentence, answer, chartBox),
  );

  return {
    element,
    leave() {
      chart?.destroy();
    },
  };
}
