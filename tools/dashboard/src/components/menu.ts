import { h, svg, ICONS } from "../dom.ts";

export interface MenuChoice {
  value: string;
  label: string;
  note?: string;
  meter?: number;
}

/** A button that opens a list of radio choices: arrows, Home and End move, Escape closes to the button. */
export function choiceMenu(
  label: string,
  choices: MenuChoice[],
  selected: string,
  onChoose: (value: string) => void,
): HTMLElement {
  const button = h(
    "button",
    {
      type: "button",
      class: "quiet chev",
      "aria-haspopup": "menu",
      "aria-expanded": "false",
    },
    h("span", {}, label),
    svg(ICONS.chevron),
  );
  const list = h("div", { class: "menu", role: "menu" });
  const items: HTMLButtonElement[] = [];
  for (const choice of choices) {
    const item = h(
      "button",
      {
        type: "button",
        role: "menuitemradio",
        "aria-checked": String(choice.value === selected),
        tabindex: "-1",
      },
      h("span", {}, choice.label),
      choice.meter === undefined
        ? null
        : h(
            "span",
            { class: "meter", "aria-hidden": "true" },
            h("i", { style: `width:${Math.round(Math.min(1, choice.meter) * 100)}%` }),
          ),
      choice.note ? h("span", { class: "n" }, choice.note) : null,
    );
    item.addEventListener("click", () => {
      close(true);
      onChoose(choice.value);
    });
    items.push(item);
    list.append(item);
  }

  const outside = (event: PointerEvent): void => {
    if (!anchor.contains(event.target as Node)) {
      close(false);
    }
  };

  function open(): void {
    list.classList.add("open");
    button.setAttribute("aria-expanded", "true");
    let current = items.findIndex(
      (item) => item.getAttribute("aria-checked") === "true",
    );
    if (current < 0) {
      current = 0;
    }
    items[current]?.focus();
    document.addEventListener("pointerdown", outside);
  }

  function close(refocus: boolean): void {
    list.classList.remove("open");
    button.setAttribute("aria-expanded", "false");
    document.removeEventListener("pointerdown", outside);
    if (refocus) {
      button.focus();
    }
  }

  button.addEventListener("click", () => {
    if (list.classList.contains("open")) {
      close(false);
    } else {
      open();
    }
  });

  list.addEventListener("keydown", (event) => {
    const at = items.indexOf(document.activeElement as HTMLButtonElement);
    let next = -1;
    if (event.key === "ArrowDown") {
      next = (at + 1) % items.length;
    } else if (event.key === "ArrowUp") {
      next = (at - 1 + items.length) % items.length;
    } else if (event.key === "Home") {
      next = 0;
    } else if (event.key === "End") {
      next = items.length - 1;
    } else if (event.key === "Escape" || event.key === "Tab") {
      event.preventDefault();
      close(true);
      return;
    } else {
      return;
    }
    event.preventDefault();
    items[next].focus();
  });

  const anchor = h("div", { class: "menu-anchor" }, button, list);
  return anchor;
}

export interface PillOption {
  value: string;
  children: Node[];
}

/** The Trends sentence's word button and its listbox, centred under it. */
export function pillMenu(
  label: string,
  name: string,
  options: PillOption[],
  selected: string,
  onChoose: (value: string) => void,
  heading: string | null = null,
): HTMLElement {
  const button = h(
    "button",
    { type: "button", class: "pill", "aria-haspopup": "listbox", "aria-expanded": "false" },
    label,
    svg(ICONS.chevron),
  );
  const list = h("div", { class: "menu pick", role: "listbox", "aria-label": name });
  if (heading !== null) {
    list.append(h("div", { class: "hd" }, heading));
  }
  const items: HTMLButtonElement[] = [];
  for (const option of options) {
    const item = h(
      "button",
      { type: "button", role: "option", "aria-selected": String(option.value === selected), tabindex: "-1" },
      option.children,
    );
    item.addEventListener("click", () => {
      close(true);
      onChoose(option.value);
    });
    items.push(item);
    list.append(item);
  }
  const anchor = h("span", { class: "slot" }, button, list);

  const outside = (event: PointerEvent): void => {
    if (!anchor.contains(event.target as Node)) {
      close(false);
    }
  };

  function open(): void {
    list.classList.add("open");
    button.setAttribute("aria-expanded", "true");
    let current = items.findIndex((item) => item.getAttribute("aria-selected") === "true");
    if (current < 0) {
      current = 0;
    }
    items[current]?.focus();
    document.addEventListener("pointerdown", outside);
  }

  function close(refocus: boolean): void {
    list.classList.remove("open");
    button.setAttribute("aria-expanded", "false");
    document.removeEventListener("pointerdown", outside);
    if (refocus) {
      button.focus();
    }
  }

  button.addEventListener("click", () => {
    if (list.classList.contains("open")) {
      close(false);
    } else {
      open();
    }
  });
  list.addEventListener("keydown", (event) => {
    const at = items.indexOf(document.activeElement as HTMLButtonElement);
    let next = -1;
    if (event.key === "ArrowDown") {
      next = (at + 1) % items.length;
    } else if (event.key === "ArrowUp") {
      next = (at - 1 + items.length) % items.length;
    } else if (event.key === "Home") {
      next = 0;
    } else if (event.key === "End") {
      next = items.length - 1;
    } else if (event.key === "Escape" || event.key === "Tab") {
      event.preventDefault();
      close(true);
      return;
    } else {
      return;
    }
    event.preventDefault();
    items[next].focus();
  });
  return anchor;
}
