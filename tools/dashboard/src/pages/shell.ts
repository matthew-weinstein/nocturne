import { h } from "../dom.ts";

export type PageName = "month" | "night" | "trends" | "label";

const PAGES: { name: PageName; label: string; href: string }[] = [
  { name: "month", label: "30 days", href: "#/30" },
  { name: "night", label: "Night", href: "#/night" },
  { name: "trends", label: "Trends", href: "#/trends" },
];

export interface Page {
  element: HTMLElement;
  leave(): void;
}

export function header(
  current: PageName,
  synced: string | null,
  right: Node[],
): HTMLElement {
  let at = 0;
  const links: HTMLElement[] = [];
  PAGES.forEach((page, index) => {
    if (page.name === current) {
      at = index;
    }
    links.push(
      h(
        "a",
        {
          href: page.href,
          "aria-current": page.name === current ? "page" : null,
        },
        page.label,
      ),
    );
  });
  const indicator = h("div", { class: "ind", "aria-hidden": "true" });
  const nav = h(
    "nav",
    { class: "well segmented", "aria-label": "Pages", style: `--at:${at}` },
    indicator,
    links,
  );

  let status: HTMLElement | null = null;
  if (synced !== null) {
    status = h("span", { class: "status" }, synced);
  }

  return h(
    "header",
    { class: "top" },
    h(
      "div",
      { class: "left" },
      h("a", { href: "#/label", class: "key" }, "Label"),
      status,
    ),
    nav,
    h("div", { class: "right" }, right),
  );
}

export function notBuilt(
  current: PageName,
  title: string,
  synced: string | null,
): Page {
  const element = h(
    "div",
    { class: "wrap" },
    h("h1", { class: "sr" }, title),
    header(current, synced, []),
    h("p", { class: "page-note" }, `${title} is not built yet.`),
  );
  return { element, leave() {} };
}
