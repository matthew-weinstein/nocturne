type Child = Node | string | null | undefined | false;

export function h<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  attributes: Record<string, string | boolean | number | null | undefined> = {},
  ...children: (Child | Child[])[]
): HTMLElementTagNameMap[K] {
  const element = document.createElement(tag);
  for (const [name, value] of Object.entries(attributes)) {
    if (value === null || value === undefined || value === false) {
      continue;
    }
    if (value === true) {
      element.setAttribute(name, "");
    } else {
      element.setAttribute(name, String(value));
    }
  }
  append(element, children);
  return element;
}

function append(parent: Element, children: (Child | Child[])[]): void {
  for (const child of children) {
    if (Array.isArray(child)) {
      append(parent, child);
    } else if (typeof child === "string") {
      parent.append(child);
    } else if (child) {
      parent.append(child);
    }
  }
}

export function svg(markup: string): Element {
  const template = document.createElement("template");
  template.innerHTML = markup.trim();
  const element = template.content.firstElementChild;
  if (element === null) {
    throw new Error("empty svg");
  }
  element.setAttribute("aria-hidden", "true");
  return element;
}

export const ICONS = {
  previous:
    '<svg width="15" height="15" viewBox="0 0 18 18"><path d="M11 4 6 9l5 5" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/></svg>',
  next: '<svg width="15" height="15" viewBox="0 0 18 18"><path d="m7 4 5 5-5 5" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round"/></svg>',
  info: '<svg width="18" height="18" viewBox="0 0 18 18"><circle cx="9" cy="9" r="7.2" fill="none" stroke="currentColor" stroke-width="1.3"/><path d="M9 8.1v4.6" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/><circle cx="9" cy="5.6" r=".95" fill="currentColor"/></svg>',
  close:
    '<svg width="14" height="14" viewBox="0 0 14 14"><path d="m3 3 8 8M11 3l-8 8" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round"/></svg>',
  chevron:
    '<svg class="chev-icon" width="16" height="16" viewBox="0 0 16 16"><path d="m4.5 6.5 3.5 3.5 3.5-3.5" fill="none" stroke="currentColor" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round"/></svg>',
  play: '<svg width="9" height="9" viewBox="0 0 12 12"><path d="M3 1.5v9l7.5-4.5z" fill="currentColor"/></svg>',
  stop: '<svg width="9" height="9" viewBox="0 0 12 12"><rect x="2.5" y="2.5" width="7" height="7" rx="1" fill="currentColor"/></svg>',
};

export function announce(message: string): void {
  const announcer = document.getElementById("announcer");
  if (announcer === null) {
    return;
  }
  announcer.textContent = "";
  window.setTimeout(() => {
    announcer.textContent = message;
  }, 30);
}

/** Fades a scrolling element's foot only while more lies below; returns a refresh for new content. */
export function fadeWhileMore(element: HTMLElement): () => void {
  const update = (): void => {
    const hidden =
      element.scrollHeight - element.clientHeight - element.scrollTop;
    element.classList.toggle("more", hidden > 1);
  };
  element.addEventListener("scroll", update, { passive: true });
  new ResizeObserver(update).observe(element);
  return update;
}
