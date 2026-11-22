import assert from "node:assert/strict";
import { test } from "node:test";
import type { TrendNight, Trends } from "../src/api.ts";
import { addDays } from "../src/view/month.ts";
import {
  METRICS,
  centredMean,
  changeText,
  eta,
  factorsFor,
  linearFit,
  metricOf,
  rankedFactors,
  strengthWord,
  tickText,
  trendsView,
  withNightBefore,
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

test("a least-squares fit finds the slope and r", () => {
  const fit = linearFit([{ x: 0, y: 1 }, { x: 1, y: 3 }, { x: 2, y: 5 }]);

  assert.ok(fit !== null);
  assert.equal(fit.slope, 2);
  assert.equal(fit.intercept, 1);
  assert.equal(fit.r, 1);
  assert.equal(linearFit([{ x: 1, y: 1 }, { x: 1, y: 2 }]), null);
});

test("eta is how much the groups explain", () => {
  assert.equal(eta([{ x: 0, y: 1 }, { x: 0, y: 1 }, { x: 1, y: 5 }, { x: 1, y: 5 }]), 1);
  assert.equal(eta([{ x: 0, y: 1 }, { x: 1, y: 2 }, { x: 0, y: 2 }, { x: 1, y: 1 }]), 0);
  assert.equal(eta([{ x: 0, y: 1 }, { x: 0, y: 2 }]), null);
});

test("strength has words and needs fourteen nights", () => {
  assert.equal(strengthWord(0.62, 30), "Strong");
  assert.equal(strengthWord(-0.31, 30), "Moderate");
  assert.equal(strengthWord(0.12, 30), "Weak");
  assert.equal(strengthWord(0.05, 30), "None");
  assert.equal(strengthWord(0.9, 13), "Too few nights");
});

test("changes print signed in the metric's units", () => {
  assert.equal(changeText("duration", -10.4), "−10m");
  assert.equal(changeText("duration", 75), "+1h 15m");
  assert.equal(changeText("percent", 0.031), "+3%");
  assert.equal(changeText("minutes", 0.2), "0m");
});

test("ticks drop zero minutes", () => {
  assert.equal(tickText("10h 0m"), "10h");
  assert.equal(tickText("7h 30m"), "7h 30m");
  assert.equal(tickText("11:00 PM"), "11 PM");
  assert.equal(tickText("11:30 PM"), "11:30 PM");
});

test("the night before is the previous calendar night, if recorded", () => {
  const list = withNightBefore([
    nightOn("2026-11-01", { sleep_min: 400 }),
    nightOn("2026-11-02", { sleep_min: 420 }),
    nightOn("2026-11-04", { sleep_min: 440 }),
  ]);

  assert.equal(list[0].before_sleep_min, null);
  assert.equal(list[1].before_sleep_min, 400);
  assert.equal(list[2].before_sleep_min, null);
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

test("factors rank by strength", () => {
  const list = nights(30, "2026-11-20", (index) => ({
    sleep_min: 480 - index * 2,
    bedtime_clock_min: 660 + index * 2,
    temperature_c: (index * 7) % 5,
  }));

  const ranked = rankedFactors(withNightBefore(list), metricOf("sleep"));

  assert.equal(ranked[0].name, "bedtime");
  assert.equal(ranked[0].strength.word, "Strong");
});

test("the centred mean breaks at a missing night and is raw for a week", () => {
  const points = [
    { x: 1, y: 1, nightOf: "", free: false },
    { x: 2, y: 3, nightOf: "", free: false },
    { x: 4, y: 5, nightOf: "", free: false },
  ];

  assert.deepEqual(centredMean(points, true), [[{ x: 1, y: 1 }, { x: 2, y: 3 }], [{ x: 4, y: 5 }]]);
  assert.equal(centredMean(points, false)[0][0].y, 3);
});

test("over time shows the average and the change on the previous period", () => {
  const list = nights(60, "2026-11-20", (index) => {
    if (index < 30) {
      return { sleep_min: 400 };
    }
    return { sleep_min: 430 };
  });

  const view = trendsView(data(list), { range: "month", metric: "sleep", factor: "time" });

  assert.equal(view.figure, "7h 10m");
  assert.equal(view.line, "+30m on the previous month");
  assert.equal(view.chart.kind, "time");
  assert.equal(view.chart.points.length, 30);
  assert.equal(view.chart.target, 480);
  assert.equal(view.connective, "over");
});

test("against a number, the figure is the slope per readable step", () => {
  const list = nights(30, "2026-11-20", (index) => ({ sleep_min: 480 - index, bedtime_clock_min: 660 + index * 6 }));

  const view = trendsView(data(list), { range: "month", metric: "sleep", factor: "bedtime" });

  assert.equal(view.figure, "−10m");
  assert.equal(view.line, "with each hour later to bed · strong link, 30 nights");
  assert.equal(view.connective, "against");
  assert.ok(view.chart.fit !== null);
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

  assert.equal(view.figure, "+1h 0m");
  assert.equal(view.line, "before a free day · strong link, 20 nights");
});

test("against day of week, the figure is the best day", () => {
  const list = nights(28, "2026-11-22", () => ({ onset_latency_min: 20 }));
  for (const night of list) {
    if (night.weekday === 3) {
      night.onset_latency_min = 5;
    }
  }

  const view = trendsView(data(list), { range: "month", metric: "latency", factor: "weekday" });

  assert.equal(view.figure, "Wednesday");
  assert.ok(view.line.startsWith("Most 20m on "));
  assert.equal(view.chart.groups.length, 7);
});

test("an unknown metric or factor falls back to sleep over time", () => {
  const list = nights(5, "2026-11-20", () => ({ sleep_min: 420 }));

  const view = trendsView(data(list), { range: "week", metric: "nonsense", factor: "nonsense" });

  assert.equal(view.metric, METRICS[0]);
  assert.equal(view.factor, null);
});
