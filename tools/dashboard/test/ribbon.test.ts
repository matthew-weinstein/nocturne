import assert from "node:assert/strict";
import { test } from "node:test";
import { END_WIDTH, HALF_WIDTH, STEP_MIN, filledLevels, ribbonWidths } from "../src/view/ribbon.ts";

test("gaps take the night's quiet floor", () => {
  const filled = filledLevels([40, null, 41, 42, 43, 44, 45, 46, 47, 48, 60]);

  assert.equal(filled[1], 41);
  assert.deepEqual(filledLevels([null, null]), [0, 0]);
});

test("the ribbon tapers to its ends and swells where the room was loud", () => {
  const db: number[] = [];
  for (let index = 0; index < 480; index++) {
    db.push(43);
  }
  for (let index = 230; index < 250; index++) {
    db[index] = 65;
  }
  const widths = ribbonWidths(db, 120);

  assert.equal(widths.length, 120 / STEP_MIN + 1);
  assert.equal(widths[0], END_WIDTH);
  assert.equal(widths[widths.length - 1], END_WIDTH);
  assert.ok(widths[240] > widths[100] + 4);
  assert.ok(widths[240] <= HALF_WIDTH);
  for (const width of widths) {
    assert.ok(width >= END_WIDTH - 1e-9);
  }
});

test("a night without levels is a thin line", () => {
  const widths = ribbonWidths([], 10);

  assert.equal(widths.length, 41);
  for (const width of widths) {
    assert.equal(width, END_WIDTH);
  }
});
