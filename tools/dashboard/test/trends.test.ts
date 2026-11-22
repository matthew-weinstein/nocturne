import assert from "node:assert/strict";
import { test } from "node:test";
import type { TrendNight, Trends } from "../src/api.ts";
import { addDays } from "../src/view/month.ts";
import {
  CHART_H,
  CHART_W,
  METRICS,
  eta,
  factorsFor,
  hoverAt,
  hoverView,
  metricOf,
  niceAxis,
  pearson,
  seriesOf,
  signedParts,
  strengthWord,
  tickText,
  trendsView,
  valueParts,
} from "../src/view/trends.ts";

function nightOn(nightOf: string, values: Record<string, number | null>, free = false): TrendNight {
  const [year, month, day] = nightOf.split("-").map(Number);
  let weekday = new Date(Date.UTC(year, month - 1, day)).getUTCDay();
  if (weekday === 0) {
    weekday = 7;
  }
  return { night_of: nightOf, session_id: nightOf, free_day_after: free, weekday, ...values } as TrendNight;
}

function nights(count: number, last: string, make: (index: number) => Record<string, number | null>): TrendNight[] {
  const list: TrendNight[] = [];
  for (let back = count - 1; back >= 0; back--) {
    list.push(nightOn(addDays(last, -back), make(count - 1 - back), back % 7 < 2));
  }
  return list;
}

function data(list: TrendNight[]): Trends {
  return { sleep_target_min: 480, earliest: list[0].night_of, latest: list[list.length - 1].night_of, nights: list };
}

test("pearson finds the slope, intercept and r", () => {
  const fit = pearson([0, 1, 2], [1, 3, 5]);

  assert.equal(fit.slope, 2);
  assert.equal(fit.intercept, 1);
  assert.equal(fit.r, 1);
  assert.equal(pearson([1, 1], [1, 2]).r, 0);
});

test("eta is how much the groups explain", () => {
  const pair = (x: number, y: number) => ({ day: 0, x, y });
  assert.equal(eta([pair(0, 1), pair(0, 1), pair(1, 5), pair(1, 5)], 2), 1);
  assert.equal(eta([pair(0, 1), pair(1, 2), pair(0, 2), pair(1, 1)], 2), 0);
});

test("strength has words, and none under fourteen nights", () => {
  assert.equal(strengthWord(0.62), "Strong");
  assert.equal(strengthWord(0.31), "Moderate");
  assert.equal(strengthWord(0.12), "Weak");
  assert.equal(strengthWord(0.05), "None");
  assert.equal(strengthWord(null), "Too few nights");
});

test("values and changes print in the metric's units", () => {
  assert.deepEqual(valueParts("dur", 430), [{ n: "7", u: "h" }, { n: "10", u: "m" }]);
  assert.deepEqual(valueParts("dur", 45), [{ n: "45", u: "m" }]);
  assert.deepEqual(valueParts("clock", 690), [{ n: "11:30", u: "PM" }]);
  assert.deepEqual(valueParts("pct", 91.6), [{ n: "92", u: "%" }]);
  assert.deepEqual(signedParts("dur", -10.4), [{ n: "−10", u: "m" }]);
  assert.deepEqual(signedParts("pct", 3.14), [{ n: "+3.1", u: "%" }]);
});

test("axes take a nice step of at most four and ticks drop zero minutes", () => {
  assert.deepEqual(niceAxis(400, 500, "dur"), { min: 390, max: 510, ticks: [390, 420, 450, 480, 510] });
  assert.deepEqual(niceAxis(5, 5, "min").ticks, [5, 5.5]);
  assert.equal(tickText("dur", 600), "10h");
  assert.equal(tickText("dur", 450), "7h 30m");
  assert.equal(tickText("clock", 660), "11 PM");
  assert.equal(tickText("clock", 690), "11:30 PM");
  assert.equal(tickText("num", 5, "°C"), "5°C");
});

test("the series has a slot per calendar day and the night before's sleep", () => {
  const series = seriesOf(
    data([
      nightOn("2026-11-01", { sleep_min: 400, efficiency: 0.9 }),
      nightOn("2026-11-02", { sleep_min: 420 }),
      nightOn("2026-11-04", { sleep_min: 440 }),
    ]),
  );

  assert.equal(series.count, 4);
  assert.deepEqual(series.values.get("before_sleep_min"), [null, 400, 420, null]);
  assert.equal(series.values.get("efficiency")?.[0], 90);
  assert.equal(series.values.get("weekday")?.[0], 6);
});

test("a metric never stands against itself, nor the midpoint against its ends", () => {
  const names = (metric: string): string[] => {
    const list: string[] = [];
    for (const factor of factorsFor(metricOf(metric))) {
      list.push(factor.name);
    }
    return list;
  };

  assert.ok(!names("bedtime").includes("bedtime"));
  assert.ok(!names("midpoint").includes("bedtime"));
  assert.ok(!names("midpoint").includes("wake"));
  assert.ok(names("sleep").includes("bedtime"));
  assert.ok(!names("snoring").includes("snoring"));
});

test("factors rank by strength after over time", () => {
  const list = nights(30, "2026-11-20", (index) => ({
    sleep_min: 480 - index * 2,
    bedtime_clock_min: 660 + index * 2,
    temperature_c: (index * 7) % 5,
  }));

  const view = trendsView(data(list), { range: "month", metric: "sleep", factor: "time" });

  assert.equal(view.factorItems[0].label, "Over time");
  assert.equal(view.factorItems[0].meter, false);
  assert.equal(view.factorItems[1].name, "bedtime");
  assert.equal(view.factorItems[1].word, "Strong");
});

test("over time shows the average and the change on the period before", () => {
  const list = nights(60, "2026-11-20", (index) => {
    if (index < 30) {
      return { sleep_min: 400 };
    }
    return { sleep_min: 430 };
  });

  const view = trendsView(data(list), { range: "month", metric: "sleep", factor: "time" });

  assert.deepEqual(view.figure, [{ n: "7", u: "h" }, { n: "10", u: "m" }]);
  assert.equal(view.sub, "average of 30 nights · 30m more than the 30 before");
  assert.equal(view.joiner, "over");
  assert.equal(view.factorText, "time");
  assert.equal(view.hits.length, 30);
  assert.ok(view.target.startsWith("M64 "));
  assert.equal(view.tags[0].text, "8h target");
  assert.deepEqual(
    view.xLabels.map((label) => label.text),
    ["23 Oct", "30 Oct", "6 Nov", "13 Nov", "20 Nov"],
  );
});

test("hovering a column picks its night and shows it", () => {
  const trends = data(nights(30, "2026-11-20", () => ({ sleep_min: 420 })));
  const view = trendsView(trends, { range: "month", metric: "sleep", factor: "time" });

  assert.equal(hoverAt(view, CHART_W - 17, CHART_H / 2), 29);
  assert.equal(hoverAt(view, 10, CHART_H / 2), null);

  const hover = hoverView(view, trends, 29);
  assert.equal(hover?.sub, "Fri 20 Nov");
  assert.deepEqual(hover?.figure, [{ n: "7", u: "h" }, { n: "0", u: "m" }]);
  assert.ok(hover?.hair.startsWith("M"));
});

test("against a number, the figure is the slope per readable step", () => {
  const list = nights(30, "2026-11-20", (index) => ({ sleep_min: 480 - index, bedtime_clock_min: 660 + index * 6 }));

  const view = trendsView(data(list), { range: "month", metric: "sleep", factor: "bedtime" });

  assert.deepEqual(view.figure, [{ n: "−10", u: "m" }]);
  assert.equal(view.sub, "with each hour later to bed · strong link, 30 nights");
  assert.equal(view.joiner, "against");
  assert.equal(view.factorText, "bedtime");
  assert.ok(view.line.startsWith("M"));
});

test("against a number with too few nights, the figure counts them", () => {
  const list = nights(5, "2026-11-20", (index) => ({ sleep_min: 480 - index, bedtime_clock_min: 660 + index }));

  const view = trendsView(data(list), { range: "month", metric: "sleep", factor: "bedtime" });

  assert.deepEqual(view.figure, [{ n: "5", u: "" }]);
  assert.equal(view.sub, "nights so far; a link needs 14");
  assert.equal(view.line, "");
});

test("against free day, the figure is the difference before one", () => {
  const list: TrendNight[] = [];
  for (let back = 19; back >= 0; back--) {
    const free = back % 2 === 0;
    let sleep = 440;
    if (free) {
      sleep = 500;
    }
    list.push(nightOn(addDays("2026-11-20", -back), { sleep_min: sleep }, free));
  }

  const view = trendsView(data(list), { range: "month", metric: "sleep", factor: "free" });

  assert.deepEqual(view.figure, [{ n: "+1", u: "h" }, { n: "0", u: "m" }]);
  assert.equal(view.sub, "more before a free day · 20 nights");
});

test("against day of week, the figure is the top day", () => {
  const list = nights(28, "2026-11-22", () => ({ onset_latency_min: 20 }));
  for (const night of list) {
    if (night.weekday === 3) {
      night.onset_latency_min = 50;
    }
  }

  const view = trendsView(data(list), { range: "month", metric: "latency", factor: "weekday" });

  assert.deepEqual(view.figure, [{ n: "Wednesday", u: "" }]);
  assert.equal(view.sub, "longest to fall asleep, 50m · shortest Mon, 20m");
  assert.deepEqual(
    view.xLabels.map((label) => label.text),
    ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"],
  );
});

test("an unknown metric or factor falls back to sleep over time", () => {
  const list = nights(5, "2026-11-20", () => ({ sleep_min: 420 }));

  const view = trendsView(data(list), { range: "week", metric: "nonsense", factor: "nonsense" });

  assert.equal(view.metric, METRICS[0]);
  assert.equal(view.factor, null);
  assert.equal(view.radius, 4);
  assert.equal(view.dotFill, "#97ABFF");
});
