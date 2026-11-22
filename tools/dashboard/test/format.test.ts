import assert from "node:assert/strict";
import { test } from "node:test";
import {
  clockOfMinute,
  clockText,
  durationParts,
  lengthText,
  minuteOfDay,
  nightDateText,
} from "../src/view/format.ts";

const TORONTO = "America/Toronto";
const BED = Date.UTC(2026, 10, 9, 6, 16) / 1000;

test("clock minutes follow the night's zone", () => {
  assert.equal(minuteOfDay(BED, TORONTO), 76);
  assert.equal(minuteOfDay(BED, "UTC"), 376);
  assert.equal(minuteOfDay(BED + 30, TORONTO), 76.5);
});

test("clock text is 12-hour with AM and PM", () => {
  assert.equal(clockText(BED, TORONTO), "1:16 AM");
  assert.equal(clockOfMinute(0), "12:00 AM");
  assert.equal(clockOfMinute(720), "12:00 PM");
  assert.equal(clockOfMinute(1439.9), "11:59 PM");
  assert.equal(clockOfMinute(1440 + 61), "1:01 AM");
  assert.equal(clockText(null, TORONTO), "—");
});

test("lengths read in the largest sensible units", () => {
  assert.equal(lengthText(960), "1s");
  assert.equal(lengthText(33000), "33s");
  assert.equal(lengthText(60000), "1m");
  assert.equal(lengthText(122000), "2m 2s");
  assert.equal(lengthText(3 * 3600000 + 5 * 60000), "3h 5m");
});

test("durations split into hours and minutes", () => {
  assert.deepEqual(durationParts(303.07), [{ n: "5", u: "h" }, { n: "3", u: "m" }]);
  assert.deepEqual(durationParts(9.8), [{ n: "10", u: "m" }]);
  assert.deepEqual(durationParts(0), [{ n: "0", u: "m" }]);
});

test("a night's date names its weekday", () => {
  assert.equal(nightDateText("2026-11-08"), "Sun 8 Nov");
  assert.equal(nightDateText("2026-01-01"), "Thu 1 Jan");
});
