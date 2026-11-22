import type { Month, MonthNight } from "../api.ts";
import { clockOfMinute, durationParts, durationText, nightDateText, type Part } from "./format.ts";
import type { Insight } from "./night.ts";

export const MONTH_DAYS = 30;
export const STEP_DAYS = 7;
export const PROFILE_STEP_MIN = 1;
export const SUNRISE_JUMP_MIN = 20;

const NOON_MIN = 720;
const AXIS_PAD_MIN = 30;
const TICK_MIN = 120;
const EDGE_MIN = 8;

export interface BarView {
  sessionId: string;
  bedX: number;
  upX: number;
  onsetX: number | null;
  wakeX: number | null;
  sunriseX: number | null;
  profile: number[];
  bed: string;
  up: string;
  sunrise: string | null;
  sleep: string;
  sleepParts: Part[];
  bedtime: string;
}

export interface MonthDay {
  nightOf: string;
  dateText: string;
  dayText: string;
  free: boolean;
  night: BarView | null;
}

export interface Tick {
  x: number;
  label: string;
}

export interface MonthView {
  rangeText: string;
  last: string;
  previousLast: string | null;
  nextLast: string | null;
  startX: number;
  endX: number;
  ticks: Tick[];
  days: MonthDay[];
  sunrise: { day: number; x: number }[][];
  insights: Insight[];
}

const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];
const WEEKDAYS = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];

function utcDate(isoDate: string): Date {
  const [year, month, day] = isoDate.split("-").map(Number);
  return new Date(Date.UTC(year, month - 1, day));
}

export function addDays(isoDate: string, days: number): string {
  const date = utcDate(isoDate);
  date.setUTCDate(date.getUTCDate() + days);
  return date.toISOString().slice(0, 10);
}

function shortDate(isoDate: string): string {
  const date = utcDate(isoDate);
  return `${date.getUTCDate()} ${MONTHS[date.getUTCMonth()]}`;
}

export function rangeText(first: string, last: string): string {
  return `${shortDate(first)} – ${shortDate(last)}`;
}

/** Clock minutes from noon as a time of day: 690 is 11:30 PM. */
export function clockFromNoon(clockMin: number): string {
  return clockOfMinute(clockMin + NOON_MIN);
}

function percentile(sorted: number[], fraction: number): number {
  const index = Math.min(sorted.length - 1, Math.floor(sorted.length * fraction));
  return sorted[index];
}

function smoothstep(edge: number): number {
  const t = Math.max(0, Math.min(1, edge));
  return t * t * (3 - 2 * t);
}

/**
 * The night's swell every PROFILE_STEP_MIN from bed to up, 0 to 1: louder bins swell more,
 * relative to the night's own floor and peaks, and only while asleep.
 */
export function barProfile(
  peaks: (number | null)[],
  binS: number,
  spanMin: number,
  onsetMin: number | null,
  wakeMin: number | null,
): number[] {
  const known: number[] = [];
  for (const peak of peaks) {
    if (peak !== null) {
      known.push(peak);
    }
  }
  const steps = Math.max(1, Math.round(spanMin / PROFILE_STEP_MIN));
  const profile: number[] = [];
  if (known.length === 0 || onsetMin === null || wakeMin === null) {
    for (let step = 0; step <= steps; step++) {
      profile.push(0);
    }
    return profile;
  }

  known.sort((a, b) => a - b);
  const floor = percentile(known, 0.1);
  const top = percentile(known, 0.98);
  const range = Math.max(6, top - floor);
  const raw: number[] = [];
  for (const peak of peaks) {
    if (peak === null) {
      raw.push(0);
    } else {
      raw.push(Math.max(0, Math.min(1, (peak - floor) / range)));
    }
  }

  const binMin = binS / 60;
  for (let step = 0; step <= steps; step++) {
    const minute = Math.min(spanMin, step * PROFILE_STEP_MIN);
    const centre = minute / binMin;
    let sum = 0;
    let weights = 0;
    for (let offset = -4; offset <= 4; offset++) {
      const index = Math.floor(centre) + offset;
      if (index < 0 || index >= raw.length) {
        continue;
      }
      const weight = Math.exp(-0.5 * ((index + 0.5 - centre) / 1.6) ** 2);
      sum += raw[index] * weight;
      weights += weight;
    }
    let level = 0;
    if (weights > 0) {
      level = sum / weights;
    }
    const asleep = Math.min(
      smoothstep((minute - onsetMin) / EDGE_MIN),
      smoothstep((wakeMin - minute) / EDGE_MIN),
    );
    profile.push(asleep * (0.35 + 0.65 * Math.pow(level, 0.7)));
  }
  return profile;
}

function barOf(night: MonthNight): BarView {
  const spanMin = (night.up_unix - night.bedtime_unix) / 60;
  let onsetMin: number | null = null;
  if (night.onset_clock_min !== null) {
    onsetMin = night.onset_clock_min - night.bedtime_clock_min;
  }
  let wakeMin: number | null = null;
  if (night.wake_clock_min !== null) {
    wakeMin = night.wake_clock_min - night.bedtime_clock_min;
  }
  let sunrise: string | null = null;
  if (night.sunrise_clock_min !== null) {
    sunrise = clockFromNoon(night.sunrise_clock_min);
  }
  let sleepParts: Part[] = [{ n: "—", u: "" }];
  if (night.sleep_min !== null) {
    sleepParts = durationParts(night.sleep_min);
  }
  return {
    sessionId: night.session_id,
    bedX: night.bedtime_clock_min,
    upX: night.up_clock_min,
    onsetX: night.onset_clock_min,
    wakeX: night.wake_clock_min,
    sunriseX: night.sunrise_clock_min,
    profile: barProfile(night.levels.peak_db, night.levels.bin_s, spanMin, onsetMin, wakeMin),
    bed: clockFromNoon(night.bedtime_clock_min),
    up: clockFromNoon(night.up_clock_min),
    sunrise,
    sleep: durationText(night.sleep_min),
    sleepParts,
    bedtime: clockFromNoon(night.bedtime_clock_min),
  };
}

function mean(values: number[]): number | null {
  if (values.length === 0) {
    return null;
  }
  let sum = 0;
  for (const value of values) {
    sum += value;
  }
  return sum / values.length;
}

function standardDeviation(values: number[]): number | null {
  const centre = mean(values);
  if (centre === null || values.length < 2) {
    return null;
  }
  let sum = 0;
  for (const value of values) {
    sum += (value - centre) ** 2;
  }
  return Math.sqrt(sum / (values.length - 1));
}

function durationInsight(minutes: number | null, label: string): Insight {
  if (minutes === null) {
    return { parts: [{ n: "—", u: "" }], label };
  }
  return { parts: durationParts(minutes), label };
}

function signedInsight(minutes: number | null, label: string): Insight {
  if (minutes === null) {
    return { parts: [{ n: "—", u: "" }], label };
  }
  const parts = durationParts(Math.abs(minutes));
  let sign = "";
  if (Math.round(minutes) > 0) {
    sign = "+";
  } else if (Math.round(minutes) < 0) {
    sign = "−";
  }
  parts[0] = { n: `${sign}${parts[0].n}`, u: parts[0].u };
  return { parts, label };
}

export function monthInsights(month: Month): Insight[] {
  const sleep: number[] = [];
  const latency: number[] = [];
  const efficiency: number[] = [];
  const bedtimes: number[] = [];
  const freeMidpoints: number[] = [];
  const workMidpoints: number[] = [];
  for (const night of month.nights) {
    if (night.sleep_min !== null) {
      sleep.push(night.sleep_min);
    }
    if (night.onset_latency_min !== null) {
      latency.push(night.onset_latency_min);
    }
    if (night.efficiency !== null) {
      efficiency.push(night.efficiency);
    }
    bedtimes.push(night.bedtime_clock_min);
    if (night.midpoint_clock_min !== null) {
      if (night.free_day_after) {
        freeMidpoints.push(night.midpoint_clock_min);
      } else {
        workMidpoints.push(night.midpoint_clock_min);
      }
    }
  }

  let efficiencyInsight: Insight = { parts: [{ n: "—", u: "" }], label: "efficiency" };
  const meanEfficiency = mean(efficiency);
  if (meanEfficiency !== null) {
    efficiencyInsight = { parts: [{ n: String(Math.round(meanEfficiency * 100)), u: "%" }], label: "efficiency" };
  }

  let shift: number | null = null;
  const free = mean(freeMidpoints);
  const work = mean(workMidpoints);
  if (free !== null && work !== null) {
    shift = free - work;
  }

  let spread: Insight = { parts: [{ n: "—", u: "" }], label: "bedtime spread" };
  const spreadMin = standardDeviation(bedtimes);
  if (spreadMin !== null) {
    const parts = durationParts(spreadMin);
    parts[0] = { n: `±${parts[0].n}`, u: parts[0].u };
    spread = { parts, label: "bedtime spread" };
  }

  let debt: number | null = null;
  if (month.debt_14_min !== null) {
    debt = Math.max(0, month.debt_14_min);
  }

  return [
    durationInsight(mean(sleep), "average sleep"),
    durationInsight(mean(latency), "to fall asleep"),
    efficiencyInsight,
    spread,
    durationInsight(debt, "sleep debt, 14 nights"),
    signedInsight(shift, "weekend shift"),
  ];
}

function roundDown(value: number, step: number): number {
  return Math.floor(value / step) * step;
}

function roundUp(value: number, step: number): number {
  return Math.ceil(value / step) * step;
}

/** Where each night's sunrise falls, broken at a missing night or a jump over SUNRISE_JUMP_MIN. */
export function sunriseRuns(days: MonthDay[]): { day: number; x: number }[][] {
  const runs: { day: number; x: number }[][] = [];
  let run: { day: number; x: number }[] = [];
  for (let day = 0; day < days.length; day++) {
    const night = days[day].night;
    let x: number | null = null;
    if (night !== null) {
      x = night.sunriseX;
    }
    const previous = run[run.length - 1];
    const broken = x === null || (previous !== undefined && Math.abs(x - previous.x) > SUNRISE_JUMP_MIN);
    if (broken && run.length > 0) {
      runs.push(run);
      run = [];
    }
    if (x !== null) {
      run.push({ day, x });
    }
  }
  if (run.length > 0) {
    runs.push(run);
  }
  return runs;
}

export function monthView(month: Month): MonthView {
  const byDate = new Map<string, MonthNight>();
  for (const night of month.nights) {
    byDate.set(night.night_of, night);
  }

  const days: MonthDay[] = [];
  let earliestX = Infinity;
  let latestX = -Infinity;
  for (let back = 0; back < MONTH_DAYS; back++) {
    const nightOf = addDays(month.last, -back);
    const date = utcDate(nightOf);
    const night = byDate.get(nightOf);
    let bar: BarView | null = null;
    if (night !== undefined) {
      bar = barOf(night);
      earliestX = Math.min(earliestX, bar.bedX);
      latestX = Math.max(latestX, bar.upX);
      if (bar.sunriseX !== null) {
        latestX = Math.max(latestX, bar.sunriseX);
      }
    }
    const weekday = date.getUTCDay();
    days.push({
      nightOf,
      dateText: nightDateText(nightOf),
      dayText: `${WEEKDAYS[weekday]} ${date.getUTCDate()}`,
      free: weekday === 5 || weekday === 6,
      night: bar,
    });
  }
  if (earliestX === Infinity) {
    earliestX = 660;
    latestX = 1140;
  }

  const startX = roundDown(earliestX - AXIS_PAD_MIN, 60);
  const endX = roundUp(latestX + AXIS_PAD_MIN, 60);
  const ticks: Tick[] = [];
  for (let x = roundUp(startX, TICK_MIN); x <= endX; x += TICK_MIN) {
    ticks.push({ x, label: clockFromNoon(x).replace(":00", "") });
  }

  let previousLast: string | null = null;
  if (month.earliest !== null && month.earliest < month.first) {
    previousLast = addDays(month.last, -STEP_DAYS);
  }
  let nextLast: string | null = null;
  if (month.latest !== null && month.last < month.latest) {
    nextLast = addDays(month.last, STEP_DAYS);
    if (nextLast > month.latest) {
      nextLast = month.latest;
    }
  }

  return {
    rangeText: rangeText(month.first, month.last),
    last: month.last,
    previousLast,
    nextLast,
    startX,
    endX,
    ticks,
    days,
    sunrise: sunriseRuns(days),
    insights: monthInsights(month),
  };
}
