import assert from "node:assert/strict";
import { test } from "node:test";
import type { Month, MonthNight } from "../src/api.ts";
import { addDays, band, clockFromNoon, dayAt, levelsOf, monthView, rangeText, sunline } from "../src/view/month.ts";

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

test("thirty days run oldest first, the latest drawn at the top", () => {
  const view = monthView(month([night("2026-11-20", 690, 420, 1150), night("2026-11-18", 700, 400, 1148)]));

  assert.equal(view.days.length, 30);
  assert.equal(view.days[29].nightOf, "2026-11-20");
  assert.equal(view.days[28].night, null);
  assert.equal(view.days[27].night?.bedtime, "11:40 PM");
  assert.equal(view.days[0].nightOf, "2026-10-22");
  assert.ok(view.days[29].y < view.days[0].y);
  assert.equal(view.days[29].longText, "Fri 20 Nov");
  assert.equal(view.days[29].shortText, "Fri 20");
  assert.equal(view.count, 2);
  assert.equal(view.rangeText, "22 Oct – 20 Nov");
});

test("a point picks the row under it, the top row the latest night", () => {
  assert.equal(dayAt(110), 29);
  assert.equal(dayAt(108 + 29 * 16 + 1), 0);
  assert.equal(dayAt(100), null);
  assert.equal(dayAt(108 + 30 * 16), null);
});

test("the clock spans every bed, up and sunrise, padded to the hour", () => {
  const view = monthView(month([night("2026-11-20", 690, 420, 1150)]));

  assert.equal(view.grid.length, 10);
  assert.ok(view.grid[0].path.startsWith("M91.0 108V"));
  assert.ok(view.grid[9].path.startsWith("M709.0 108V"));
  assert.deepEqual(view.hours.map((hour) => hour.text), ["12 AM", "3 AM", "6 AM"]);
});

test("free nights are shaded", () => {
  const view = monthView(month([]));
  assert.equal(view.shades.length, 9);
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
  const path = sunline([
    { x: 1, y: 10, clock: 1150 },
    { x: 2, y: 20, clock: 1151 },
    null,
    { x: 3, y: 30, clock: 1152 },
    { x: 4, y: 40, clock: 1090 },
  ]);

  assert.equal(path, "M1.0 10.0L2.0 20.0M3.0 30.0M4.0 40.0");
});

test("levels run from the night's floor to its loud peaks", () => {
  const peaks: (number | null)[] = [];
  for (let index = 0; index < 50; index++) {
    peaks.push(40);
  }
  peaks.push(70, null);

  const levels = levelsOf(peaks);

  assert.equal(levels[0], 0);
  assert.equal(levels[50], 1);
  assert.equal(levels[51], 0);
  assert.deepEqual(levelsOf([null, null]), [0, 0]);
});

test("a band traces its top edge, then its bottom edge back", () => {
  const path = band(100, (minute) => minute * 10, 0, 2, () => 1);

  assert.equal(path, "M0.0 99.0L10.0 99.0L20.0 99.0L20.0 101.0L10.0 101.0L0.0 101.0Z");
  assert.equal(band(100, (minute) => minute, 3, 3.2, () => 1), "");
});

test("the summary averages the nights and shows the weekend shift", () => {
  const nights = [
    night("2026-11-20", 750, 420, null, true),
    night("2026-11-19", 660, 480, null, false),
  ];
  const view = monthView(month(nights));

  assert.deepEqual(view.insights[0].parts, [{ n: "7", u: "h" }, { n: "30", u: "m" }]);
  assert.equal(view.insights[0].label, "avg. sleep time");
  assert.deepEqual(view.insights[1].parts, [{ n: "15", u: "m" }]);
  assert.equal(view.insights[2].parts[0].n, "90");
  assert.deepEqual(view.insights[3].parts, [{ n: "±1", u: "h" }, { n: "4", u: "m" }]);
  assert.deepEqual(view.insights[4].parts, [{ n: "1", u: "h" }, { n: "35", u: "m" }]);
  assert.deepEqual(view.insights[5].parts, [{ n: "+1", u: "h" }, { n: "0", u: "m" }]);
});
