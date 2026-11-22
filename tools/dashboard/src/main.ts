import "@fontsource/albert-sans/latin-300.css";
import "@fontsource/albert-sans/latin-400.css";
import "@fontsource/albert-sans/latin-500.css";
import "@fontsource/albert-sans/latin-600.css";
import "./tokens.css";
import "./components.css";
import "./night.css";
import { fetchNight, fetchNights, fetchStatus } from "./api.ts";
import { h } from "./dom.ts";
import { clockText } from "./view/format.ts";
import { nightPage } from "./pages/night.ts";
import { header, notBuilt, type Page } from "./pages/shell.ts";

const root = document.getElementById("app") as HTMLElement;
let current: Page | null = null;
let rendering = 0;

async function syncedText(): Promise<string | null> {
  try {
    const status = await fetchStatus();
    if (status.last_ingested_unix === null) {
      return null;
    }
    const zone = Intl.DateTimeFormat().resolvedOptions().timeZone;
    return `Synced ${clockText(status.last_ingested_unix, zone)}`;
  } catch {
    return null;
  }
}

function failure(message: string): Page {
  const element = h(
    "div",
    { class: "wrap" },
    header("night", null, []),
    h("p", { class: "page-note", role: "alert" }, message),
  );
  return { element, leave() {} };
}

async function pageFor(hash: string, synced: string | null): Promise<Page> {
  const parts = hash.replace(/^#\/?/, "").split("/");
  if (parts[0] === "30") {
    return notBuilt("month", "30 days", synced);
  }
  if (parts[0] === "trends") {
    return notBuilt("trends", "Trends", synced);
  }
  if (parts[0] === "label") {
    return notBuilt("label", "Label", synced);
  }

  let sessionId = parts[1];
  if (parts[0] !== "night" || !sessionId) {
    const nights = await fetchNights();
    if (nights.length === 0) {
      return failure("No nights yet. Run the pipeline, then reload.");
    }
    sessionId = nights[0].session_id;
  }
  return nightPage(await fetchNight(sessionId), synced);
}

async function render(): Promise<void> {
  const ticket = ++rendering;
  let page: Page;
  try {
    page = await pageFor(location.hash, await syncedText());
  } catch (error) {
    page = failure(`Couldn't load this page (${String(error)}).`);
  }
  if (ticket !== rendering) {
    page.leave();
    return;
  }
  const was = navIndex();
  current?.leave();
  current = page;
  root.replaceChildren(page.element);
  slideNav(was);
}

function navIndex(): string | null {
  const nav = root.querySelector<HTMLElement>(".segmented");
  if (nav === null) {
    return null;
  }
  return nav.style.getPropertyValue("--at");
}

function slideNav(from: string | null): void {
  const nav = root.querySelector<HTMLElement>(".segmented");
  const indicator = nav?.querySelector<HTMLElement>(".ind");
  if (nav === null || indicator === null || indicator === undefined || from === null) {
    return;
  }
  const to = nav.style.getPropertyValue("--at");
  if (from === to) {
    return;
  }
  indicator.style.transition = "none";
  nav.style.setProperty("--at", from);
  indicator.getBoundingClientRect();
  indicator.style.transition = "";
  nav.style.setProperty("--at", to);
}

window.addEventListener("hashchange", render);
render();
