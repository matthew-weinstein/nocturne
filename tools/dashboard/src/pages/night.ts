import { clipUrl, type NightDetail } from "../api.ts";
import {
  CX,
  CY,
  Dial,
  HEIGHT,
  RING,
  WIDTH,
  polar,
  sweepReaches,
  type Span,
} from "../components/dial.ts";
import { choiceMenu, type MenuChoice } from "../components/menu.ts";
import { ICONS, announce, fadeWhileMore, h, svg } from "../dom.ts";
import { clockPartsOfMinute, type Part } from "../view/format.ts";
import {
  eventAt,
  nightView,
  type EventRow,
  type NightView,
} from "../view/night.ts";
import { header, type Page } from "./shell.ts";

const EVERYTHING = "";

function percent(value: number, total: number): string {
  return `${((value / total) * 100).toFixed(2)}%`;
}

function at(x: number, y: number): string {
  return `left:${percent(x, WIDTH)};top:${percent(y, HEIGHT)}`;
}

function boxStyle(radius: number): string {
  return `${at(CX - radius, CY - radius)};width:${percent(radius * 2, WIDTH)};height:${percent(radius * 2, HEIGHT)}`;
}

function parts(list: Part[], unitClass: string): HTMLElement[] {
  const nodes: HTMLElement[] = [];
  for (const part of list) {
    nodes.push(h("b", { style: "font-weight:300" }, part.n));
    nodes.push(h("b", { class: unitClass }, part.u));
  }
  return nodes;
}

function mark(
  clockMin: number,
  key: string,
  value: string,
  delay: number,
  sun: boolean,
): HTMLElement {
  const [x, y] = polar(clockMin, RING + 22);
  const across = Math.max(0, Math.min(1, (x - (CX - RING)) / (RING * 2)));
  const down = Math.max(0, Math.min(1, (y - (CY - RING)) / (RING * 2)));
  const shiftX = -100 * (1 - Math.round(across));
  const shiftY = -100 * (1 - down);
  let align = "left";
  if (shiftX < 0) {
    align = "right";
  }
  return h(
    "div",
    {
      class: sun ? "mark sun" : "mark",
      style: `${at(x, y)};transform:translate(${shiftX}%,${shiftY.toFixed(0)}%);text-align:${align};animation-delay:${delay.toFixed(3)}s`,
    },
    h("div", { class: "k" }, key),
    h("div", { class: "t" }, value),
  );
}

function hourLabels(): HTMLElement[] {
  const labels: HTMLElement[] = [];
  for (let hour = 0; hour < 24; hour += 6) {
    const [x, y] = polar(hour * 60, RING - 30);
    let hour12 = hour % 12;
    if (hour12 === 0) {
      hour12 = 12;
    }
    let suffix = "PM";
    if (hour < 12) {
      suffix = "AM";
    }
    labels.push(
      h(
        "div",
        { class: "hour", style: at(x, y), "aria-hidden": "true" },
        `${hour12} ${suffix}`,
      ),
    );
  }
  return labels;
}

function infoDialog(
  view: NightView,
  opener: () => HTMLElement,
): HTMLDialogElement {
  const close = h(
    "button",
    { type: "button", class: "icon", "aria-label": "Close" },
    svg(ICONS.close),
  );
  const sections: HTMLElement[] = [];
  for (const section of view.info) {
    const rows: HTMLElement[] = [];
    for (const row of section.rows) {
      rows.push(
        h(
          "div",
          { class: "pair" },
          h("dt", {}, row.k),
          h(
            "dd",
            {},
            row.v,
            h("span", { class: "est" }, row.est ? "est." : ""),
          ),
        ),
      );
    }
    sections.push(
      h("section", {}, h("h3", {}, section.title), h("dl", {}, rows)),
    );
  }
  const body = h(
    "div",
    {
      class: "db",
      tabindex: "0",
      role: "region",
      "aria-label": "Night details",
    },
    h("div", { class: "columns" }, sections),
  );
  fadeWhileMore(body);
  const dialog = h(
    "dialog",
    { class: "dlg info", "aria-labelledby": "info-title" },
    h(
      "div",
      { class: "dh" },
      h(
        "div",
        {},
        h("h2", { id: "info-title" }, view.dateText),
        h("div", { class: "ds" }, view.infoRange),
      ),
      close,
    ),
    body,
  );

  close.addEventListener("click", () => dialog.close());
  dialog.addEventListener("click", (event) => {
    if (event.target === dialog) {
      dialog.close();
    }
  });
  dialog.addEventListener("close", () => opener().focus());
  return dialog;
}

export function nightPage(detail: NightDetail, synced: string | null): Page {
  const view = nightView(detail);
  const sunDelay = (clockMin: number): number => {
    const fromBed = (clockMin - view.dial.bedMin + 1440) % 1440;
    return sweepReaches(Math.min(fromBed, view.dial.spanMin), view.dial.spanMin);
  };
  let filter = EVERYTHING;
  let cursor: number | null = null;
  let playing: { row: EventRow; audio: HTMLAudioElement } | null = null;

  const canvas = h("canvas", {
    role: "img",
    "aria-label": `24-hour clock, midnight at the top. The night from bed at ${view.bed} to up at ${view.up} swells where the room was louder, white where something stirred.`,
  });
  const big = h("div", { class: "big" }, parts(view.big, "u"));
  const sub = h("div", { class: "sub" }, view.bigLabel);
  const span = view.dial.spanMin;
  const marks: HTMLElement[] = [
    mark(view.dial.bedMin, "Bed", view.bed, sweepReaches(0, span), false),
    mark(view.dial.bedMin + span, "Up", view.up, sweepReaches(span, span), false),
  ];
  if (view.sunrise !== null && view.dial.sunriseMin !== null) {
    marks.push(mark(view.dial.sunriseMin, "Sunrise", view.sunrise, sunDelay(view.dial.sunriseMin), true));
  }
  if (view.sunset !== null && view.dial.sunsetMin !== null) {
    marks.push(mark(view.dial.sunsetMin, "Sunset", view.sunset, sunDelay(view.dial.sunsetMin), true));
  }
  const dialBox = h(
    "div",
    { class: "dial" },
    h("div", {
      class: "plate",
      style: boxStyle(RING - 30),
      "aria-hidden": "true",
    }),
    canvas,
    hourLabels(),
    marks,
    h("div", { class: "centre" }, big, sub),
  );

  const dial = new Dial(canvas);
  const stir: Span[] = [];
  for (const row of view.events) {
    if (row.stir) {
      stir.push({ startMin: row.startMin, endMin: row.endMin });
    }
  }
  dial.setNight(view.dial, stir);

  const rows = new Map<number, HTMLButtonElement>();
  const list = h("div", {
    class: "list",
    role: "group",
    "aria-label": "Sounds, earliest first",
  });
  const count = h("div", { class: "count" });
  const refreshFade = fadeWhileMore(list);

  function showCursor(minute: number | null): void {
    cursor = minute;
    let active: EventRow | null = null;
    if (minute !== null) {
      const index = eventAt(view.events, minute);
      if (index !== null) {
        active = view.events[index];
      }
    }
    for (const [index, button] of rows) {
      button.classList.toggle("on", active !== null && active.index === index);
    }
    let highlight: Span | null = null;
    if (active !== null) {
      highlight = { startMin: active.startMin, endMin: active.endMin };
    }
    dial.setCursor(minute, highlight);
    if (minute === null) {
      big.replaceChildren(...parts(view.big, "u"));
      sub.textContent = view.bigLabel;
      return;
    }
    big.replaceChildren(
      ...parts([clockPartsOfMinute(view.dial.bedMin + minute)], "u ap"),
    );
    if (active !== null) {
      sub.textContent = `${active.kind} · ${active.length}`;
    } else {
      sub.textContent = "Quiet";
    }
  }

  function stopPlaying(): void {
    if (playing === null) {
      return;
    }
    playing.audio.pause();
    const button = rows.get(playing.row.index);
    if (button !== undefined) {
      button.classList.remove("playing");
      button.querySelector(".pl")?.replaceChildren(svg(ICONS.play));
    }
    playing = null;
  }

  function play(row: EventRow, button: HTMLButtonElement): void {
    const same = playing !== null && playing.row.index === row.index;
    stopPlaying();
    if (same) {
      return;
    }
    const audio = new Audio(
      clipUrl(
        view.sessionId,
        row.clip.segment,
        row.clip.frame,
        row.clip.frames,
      ),
    );
    playing = { row, audio };
    button.classList.add("playing");
    button.querySelector(".pl")?.replaceChildren(svg(ICONS.stop));
    audio.addEventListener("ended", stopPlaying);
    audio.play().catch(() => {
      stopPlaying();
      announce("Audio is not available");
    });
  }

  function fillList(): void {
    rows.clear();
    const shown: HTMLElement[] = [];
    for (const row of view.events) {
      if (filter !== EVERYTHING && row.className !== filter) {
        continue;
      }
      const button = h(
        "button",
        {
          type: "button",
          class: "row",
          "aria-label": `${row.time} ${row.kind} ${row.length}, play`,
        },
        h("div", { class: "tm" }, row.time),
        h(
          "div",
          {},
          h("b", { class: "kd" }, row.kind),
          h("b", { class: "du" }, row.length),
        ),
        h("div", { class: "pl", "aria-hidden": "true" }, svg(ICONS.play)),
      );
      button.addEventListener("pointerenter", () => showCursor(row.startMin));
      button.addEventListener("pointerleave", () => showCursor(null));
      button.addEventListener("focus", () => showCursor(row.startMin));
      button.addEventListener("blur", () => showCursor(null));
      button.addEventListener("click", () => play(row, button));
      rows.set(row.index, button);
      shown.push(button);
    }
    if (shown.length === 0) {
      list.replaceChildren(h("p", { class: "empty" }, "No sounds detected."));
    } else {
      list.replaceChildren(...shown);
    }
    count.textContent = String(shown.length);
    list.scrollTop = 0;
    requestAnimationFrame(refreshFade);
  }

  const choices: MenuChoice[] = [
    {
      value: EVERYTHING,
      label: "Everything",
      note: String(view.events.length),
    },
  ];
  for (const item of view.classes) {
    choices.push({
      value: item.className,
      label: item.kind,
      note: String(item.count),
    });
  }
  const filterSlot = h("div", {});
  function fillFilter(): void {
    let label = "Everything";
    for (const choice of choices) {
      if (choice.value === filter) {
        label = choice.label;
      }
    }
    filterSlot.replaceChildren(
      choiceMenu(label, choices, filter, (value) => {
        filter = value;
        fillFilter();
        fillList();
        filterSlot.querySelector("button")?.focus();
      }),
    );
  }
  fillFilter();
  fillList();

  dialBox.addEventListener("pointermove", (event) => {
    const minute = dial.pointToMinute(event.clientX, event.clientY);
    if (minute !== cursor) {
      showCursor(minute);
    }
  });
  dialBox.addEventListener("pointerleave", () => showCursor(null));

  const infoButton = h(
    "button",
    {
      type: "button",
      class: "icon",
      "aria-label": "All details for this night",
      "aria-haspopup": "dialog",
    },
    svg(ICONS.info),
  );
  const dialog = infoDialog(view, () => infoButton);
  infoButton.addEventListener("click", () => dialog.showModal());

  const previous = h(
    "a",
    {
      class: "icon",
      href: view.previous ? `#/night/${view.previous}` : null,
      "aria-label": "Previous night",
      "aria-disabled": view.previous ? null : "true",
    },
    svg(ICONS.previous),
  );
  const next = h(
    "a",
    {
      class: "icon",
      href: view.next ? `#/night/${view.next}` : null,
      "aria-label": view.next ? "Next night" : "Next night, none yet",
      "aria-disabled": view.next ? null : "true",
    },
    svg(ICONS.next),
  );

  const insights: HTMLElement[] = [];
  view.insights.forEach((insight, index) => {
    const values: HTMLElement[] = [];
    for (const part of insight.parts) {
      values.push(h("b", { class: "v" }, part.n));
      values.push(h("b", { class: "u" }, part.u));
    }
    insights.push(
      h(
        "div",
        {
          class: "metric",
          style: `animation-delay:${(index * 0.035).toFixed(3)}s`,
        },
        h("div", {}, values),
        h("div", { class: "l" }, insight.label),
      ),
    );
  });

  const element = h(
    "div",
    { class: "wrap" },
    h("h1", { class: "sr" }, `Night of ${view.dateText}`),
    header("night", synced, [
      infoButton,
      h(
        "div",
        { class: "well date" },
        previous,
        h("div", { class: "d" }, view.dateText),
        next,
      ),
    ]),
    h(
      "div",
      { class: "night" },
      h("section", { class: "insights", "aria-label": "Tonight" }, insights),
      h("div", { class: "dial-col" }, dialBox),
      h(
        "section",
        { class: "sounds", "aria-label": "Through the night" },
        h("div", { class: "sounds-head" }, filterSlot, count),
        list,
      ),
    ),
    dialog,
  );

  return {
    element,
    leave() {
      stopPlaying();
      dial.destroy();
    },
  };
}
