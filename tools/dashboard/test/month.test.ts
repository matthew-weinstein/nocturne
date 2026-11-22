import assert from "node:assert/strict";
import { test } from "node:test";
import type { Month, MonthNight } from "../src/api.ts";
import { addDays, barProfile, clockFromNoon, monthView, rangeText, sunriseRuns, type MonthDay } from "../src/view/month.ts";

function night(nightOf: string, bed: number, sleep: number | null, sunrise: number | null, free = false): MonthNight {
  return {
    session_id: `${nightOf.replaceAll("-", "")}_2330`,
    night_of: nightOf,
    weekday: 1,
    free_day_after: free,
    timezone: "America/Toronto",
    bedtime_clock_min: bed,
    onset_clock_min: bed + 15,
    wake_clock_min: bed + 15 + (sleep ?? 0),
    up_clock_min: bed + 30 + (sleep ?? 0),
    midpoint_clock_min: bed + 15 + (sleep ?? 0) / 2,
    sunrise_clock_min: sunrise,
    sleep_min: sleep,
    onset_latency_min: 15,
    efficiency: 0.9,
    bedtime_unix: 0,
    up_unix: (30 + (sleep ?? 0)) * 60,
    levels: { start_unix: 0, bin_s: 120, peak_db: [40, 41, 60, 40] },
  };
}

function month(nights: MonthNight[], last = "2026-11-20", earliest = "2026-09-01"): Month {
  return {
    first: addDays(last, -29),
    last,
    earliest,
    latest: "2026-11-20",
    sleep_target_min: 480,
    debt_14_min: 95,
    nights,
  };
}

test("dates step and print in the night's own calendar", () => {
  assert.equal(addDays("2026-11-01", -1), "2026-10-31");
  assert.equal(addDays("2026-12-31", 1), "2027-01-01");
  assert.equal(rangeText("2026-10-22", "2026-11-20"), "22 Oct – 20 Nov");
  assert.equal(clockFromNoon(690), "11:30 PM");
  assert.equal(clockFromNoon(1140), "7:00 AM");
});

test("thirty days run newest first, a missing night left empty", () => {
  const view = monthView(month([night("2026-11-20", 690, 420, 1150), night("2026-11-18", 700, 400, 1148)]));

  assert.equal(view.days.length, 30);
  assert.equal(view.days[0].nightOf, "2026-11-20");
  assert.equal(view.days[1].night, null);
  assert.equal(view.days[2].night?.bedtime, "11:40 PM");
  assert.equal(view.days[29].nightOf, "2026-10-22");
  assert.equal(view.rangeText, "22 Oct – 20 Nov");
});

test("the clock spans every bed, up and sunrise, padded to the hour", () => {
  const view = monthView(month([night("2026-11-20", 690, 420, 1150)]));

  assert.equal(view.startX, 660);
  assert.equal(view.endX, 1200);
  assert.equal(view.ticks[0].label, "12 AM");
});

test("the date control steps a week while older nights remain", () => {
  const latest = monthView(month([night("2026-11-20", 690, 420, null)]));
  assert.equal(latest.nextLast, null);
  assert.equal(latest.previousLast, "2026-11-13");

  const older = monthView(month([], "2026-11-16"));
  assert.equal(older.nextLast, "2026-11-20");

  const first = monthView(month([], "2026-11-20", "2026-11-01"));
  assert.equal(first.previousLast, null);
});

test("sunrise breaks at a missing night and at a clock change", () => {
  const bar = (sunriseX: number | null) => ({ sunriseX }) as NonNullable<MonthDay["night"]>;
  const days = [
    { night: bar(1150) },
    { night: bar(1151) },
    { night: null },
    { night: bar(1152) },
    { night: bar(1090) },
  ] as MonthDay[];

  const runs = sunriseRuns(days);

  assert.deepEqual(runs.map((run) => run.map((point) => point.day)), [[0, 1], [3], [4]]);
});

test("a bar is thin outside sleep and swells where the room was loud", () => {
  const peaks: number[] = [];
  for (let index = 0; index < 60; index++) {
    peaks.push(40);
  }
  peaks[30] = 60;

  const profile = barProfile(peaks, 60, 60, 10, 50);

  assert.equal(profile.length, 61);
  assert.equal(profile[0], 0);
  assert.equal(profile[60], 0);
  assert.ok(profile[30] > profile[22] + 0.2);
  assert.deepEqual(barProfile([null], 60, 10, 1, 9), new Array(11).fill(0));
});

test("the summary averages the nights and shows the weekend shift", () => {
  const nights = [
    night("2026-11-20", 750, 420, null, true),
    night("2026-11-19", 660, 480, null, false),
  ];
  const view = monthView(month(nights));

  assert.deepEqual(view.insights[0].parts, [{ n: "7", u: "h" }, { n: "30", u: "m" }]);
  assert.equal(view.insights[2].parts[0].n, "90");
  assert.deepEqual(view.insights[4].parts, [{ n: "1", u: "h" }, { n: "35", u: "m" }]);
  assert.deepEqual(view.insights[5].parts, [{ n: "+1", u: "h" }, { n: "0", u: "m" }]);
});
