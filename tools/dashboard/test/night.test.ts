import assert from "node:assert/strict";
import { test } from "node:test";
import type { NightDetail } from "../src/api.ts";
import { clipOf, eventAt, nightView } from "../src/view/night.ts";

const BED = Date.UTC(2026, 10, 9, 6, 16) / 1000;
const UP = BED + 5 * 3600;

function detail(): NightDetail {
  const peaks: (number | null)[] = [];
  for (let index = 0; index < 1200; index++) {
    peaks.push(43);
  }
  return {
    session: {
      session_id: "20261109_0116", started_unix: BED, ended_unix: UP, segment_count: 60, sample_shift: 9,
      firmware_version: "v0.2.0", time_reliable: true, resumed: false, resume_count: 0, recovered: false,
      muted_ms: 5000, buffer_overflows: 0, sd_write_errors: 0, upload_retries: 0, wifi_disconnects: 0,
    },
    metrics: {
      bedtime_unix: BED, up_unix: UP, time_in_bed_min: 300, onset_unix: BED + 600, onset_latency_min: 10,
      wake_unix: UP - 120, lie_in_min: 2, sleep_min: 288, efficiency: 0.96, midpoint_unix: BED + 9000,
      restless_bouts: 1, restless_min: 1.5, snoring_min: 9.8, snoring_share: 0.03, snoring_per_hour: 1.9,
      snoring_peak_db: 80.2, snoring_worst_hour_unix: BED + 4 * 3600 - 960, breathing_pauses: 0,
      breathing_pauses_per_hour: 0, coughs: 4, sneezes: 0, sleep_talk: 2, bathroom_trips: 0, alarms: 2,
      first_alarm_unix: UP - 120, last_alarm_unix: UP - 20, snooze_span_min: 1.7, alarm_to_up_min: 0.3,
      noise_floor_db: 41.2, loud_moments: 3, outage_min: 0, muted_min: 0.1, missing_min: 0,
    },
    context: {
      night_of: "2026-11-08", weekday: 7, free_day_after: false, timezone: "America/Toronto", utc_offset_min: -300,
      clock_shift_min: -60, days_since_clock_change: 8, sunset_unix: BED - 8 * 3600 - 1020, sunrise_unix: BED + 20880,
    },
    weather: {
      weather_hours: 6, temperature_c: 6.4, temperature_min_c: 5.8, temperature_max_c: 6.9, humidity_pct: 82,
      pressure_hpa: 1008, pressure_change_hpa: 3.5, precipitation_mm: 1.8,
    },
    events: [
      {
        class: "speech", start_segment: 0, start_frame: 10, end_segment: 0, end_frame: 58,
        start_unix: BED + 4, end_unix: BED + 5, duration_ms: 960, peak_score: 0.8,
      },
      {
        class: "snoring", start_segment: 3, start_frame: 2000, end_segment: 3, end_frame: 2480,
        start_unix: BED + 1000, end_unix: BED + 1010, duration_ms: 9600, peak_score: 0.9,
      },
    ],
    outages: [],
    levels: { start_unix: BED, bin_s: 15, peak_db: peaks },
    previous: "20261108_0410",
    next: null,
  };
}

test("the night reads in its own zone", () => {
  const view = nightView(detail());

  assert.equal(view.dateText, "Sun 8 Nov");
  assert.equal(view.bed, "1:16 AM");
  assert.equal(view.up, "6:16 AM");
  assert.equal(view.dial.bedMin, 76);
  assert.equal(view.dial.spanMin, 300);
  assert.deepEqual(view.big, [{ n: "4", u: "h" }, { n: "48", u: "m" }]);
  assert.equal(view.bigLabel, "asleep");
  assert.equal(view.previous, "20261108_0410");
});

test("events become rows with minutes from bed and stir flags", () => {
  const view = nightView(detail());

  assert.equal(view.events.length, 2);
  assert.equal(view.events[0].kind, "Talk");
  assert.equal(view.events[0].stir, true);
  assert.equal(view.events[1].stir, false);
  assert.equal(view.events[1].length, "10s");
  assert.ok(Math.abs(view.events[1].startMin - 1000 / 60) < 1e-9);
  const classes: string[] = [];
  for (const item of view.classes) {
    classes.push(item.className);
  }
  assert.deepEqual(classes, ["speech", "snoring"]);
});

test("the event under a minute allows a quarter minute either side", () => {
  const view = nightView(detail());

  assert.equal(eventAt(view.events, 1000 / 60 + 0.2), 1);
  assert.equal(eventAt(view.events, 0.3), 0);
  assert.equal(eventAt(view.events, 100), null);
});

test("a clip starts half a second early and runs half a second past the end", () => {
  const event = detail().events[1];

  assert.deepEqual(clipOf(event), { segment: 3, frame: 1975, frames: 25 + 480 + 25 });
  assert.deepEqual(clipOf({ ...event, start_frame: 10 }), { segment: 3, frame: 0, frames: 10 + 480 + 25 });
});

test("the details list every group with estimates marked", () => {
  const view = nightView(detail());
  const titles: string[] = [];
  for (const section of view.info) {
    titles.push(section.title);
  }

  assert.deepEqual(titles, ["Sleep", "Breathing", "Sounds", "Alarms", "Room and weather", "Day", "Recording"]);
  const sleep = view.info[0].rows;
  assert.deepEqual(sleep[1], { k: "Fell asleep", v: "1:26 AM", est: true });
  assert.deepEqual(sleep[10], { k: "Restless", v: "1, 1m 30s", est: false });
  const breathing = view.info[1].rows;
  assert.equal(breathing[4].v, "5–6 AM");
  assert.equal(breathing[5].v, "None");
  const weather: string[] = [];
  for (const row of view.info[4].rows) {
    weather.push(row.v);
  }
  assert.deepEqual(weather, ["41 dB", "6°C, low 6, high 7", "82%", "1008 hPa, +3.5", "1.8 mm"]);
  assert.equal(view.info[5].rows[3].v, "8 days ago, back 1 h");
});
