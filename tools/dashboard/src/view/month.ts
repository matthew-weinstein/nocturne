import type { Month, MonthNight } from "../api.ts";
import { clockOfMinute, durationParts, nightDateText, type Part } from "./format.ts";
import type { Insight } from "./night.ts";

export const MONTH_DAYS = 30;
export const STEP_DAYS = 7;
export const SUNRISE_JUMP_MIN = 20;

export const CHART_W = 800;
export const CHART_H = 720;
const PL = 91;
const PR = 709;
const PT = 108;
const PITCH = 16;
const NOON_MIN = 720;
const AXIS_PAD_MIN = 20;
const AWAKE_HALF = 0.75;
const TAPER_MIN = 4;

export interface BarView {
  sessionId: string;
  awake: string;
  asleep: string;
  bedX: number;
  upX: number;
  sunriseX: number | null;
  bed: string;
  up: string;
  sunrise: string | null;
  sleepParts: Part[];
  sleepText: string;
  bedtime: string;
}

export interface MonthDay {
  nightOf: string;
  longText: string;
  shortText: string;
  free: boolean;
  y: number;
  night: BarView | null;
}

export interface AxisLine {
  path: string;
  major: boolean;
}

export interface Label {
  x: number;
  y: number;
  text: string;
}

export interface Shade {
  x: number;
  y: number;
  width: number;
  height: number;
  radius: number;
}

export interface MonthView {
  rangeText: string;
  previousLast: string | null;
  nextLast: string | null;
  days: MonthDay[];
  grid: AxisLine[];
  hours: Label[];
  shades: Shade[];
  glow: string;
  sunline: string;
  labelY: number;
  insights: Insight[];
  count: number;
  summary: string;
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

function hourFromNoon(clockMin: number): string {
  return clockFromNoon(clockMin).replace(":00", "");
}

function percentile(sorted: number[], fraction: number): number {
  const index = Math.min(sorted.length - 1, Math.floor(sorted.length * fraction));
  return sorted[index];
}

/** Each level bin from 0 (the night's floor) to 1 (its loud peaks). */
export function levelsOf(peaks: (number | null)[]): number[] {
  const known: number[] = [];
  for (const peak of peaks) {
    if (peak !== null) {
      known.push(peak);
    }
  }
  const levels: number[] = [];
  if (known.length === 0) {
    for (let index = 0; index < peaks.length; index++) {
      levels.push(0);
    }
    return levels;
  }
  known.sort((a, b) => a - b);
  const floor = percentile(known, 0.1);
  const range = Math.max(6, percentile(known, 0.98) - floor);
  for (const peak of peaks) {
    if (peak === null) {
      levels.push(0);
    } else {
      levels.push(Math.max(0, Math.min(1, (peak - floor) / range)));
    }
  }
  return levels;
}

function levelAt(levels: number[], binMin: number, minute: number): number {
  if (levels.length === 0) {
    return 0;
  }
  if (levels.length === 1) {
    return levels[0];
  }
  const x = minute / binMin;
  const index = Math.min(levels.length - 2, Math.max(0, Math.floor(x)));
  const fraction = Math.min(1, Math.max(0, x - index));
  return levels[index] + (levels[index + 1] - levels[index]) * fraction;
}

function fixed(value: number): string {
  return value.toFixed(1);
}

/** A closed band along y from one minute to another, its half-width given per minute. */
export function band(
  y: number,
  toX: (minute: number) => number,
  from: number,
  to: number,
  halfWidth: (minute: number) => number,
): string {
  if (to - from < 0.5) {
    return "";
  }
  const top: string[] = [];
  const bottom: string[] = [];
  for (let minute = from; minute <= to + 0.001; minute += 1) {
    const at = Math.min(minute, to);
    const x = fixed(toX(at));
    const half = halfWidth(at);
    top.push(`${x} ${fixed(y - half)}`);
    bottom.push(`${x} ${fixed(y + half)}`);
  }
  bottom.reverse();
  return `M${top.concat(bottom).join("L")}Z`;
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

const NONE: Part[] = [{ n: "—", u: "" }];

function signedParts(minutes: number, sign: string): Part[] {
  const parts = durationParts(Math.abs(minutes));
  parts[0] = { n: `${sign}${parts[0].n}`, u: parts[0].u };
  return parts;
}

export function sleepInsight(month: Month): Insight {
  const sleep: number[] = [];
  for (const night of month.nights) {
    if (night.sleep_min !== null) {
      sleep.push(night.sleep_min);
    }
  }
  const average = mean(sleep);
  if (average === null) {
    return { parts: NONE, label: "avg. sleep time" };
  }
  return { parts: durationParts(average), label: "avg. sleep time" };
}

export function monthInsights(month: Month): Insight[] {
  const latency: number[] = [];
  const efficiency: number[] = [];
  const bedtimes: number[] = [];
  const freeMidpoints: number[] = [];
  const workMidpoints: number[] = [];
  for (const night of month.nights) {
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

  let latencyParts = NONE;
  const meanLatency = mean(latency);
  if (meanLatency !== null) {
    latencyParts = [{ n: String(Math.round(meanLatency)), u: "m" }];
  }

  let efficiencyParts = NONE;
  const meanEfficiency = mean(efficiency);
  if (meanEfficiency !== null) {
    efficiencyParts = [{ n: String(Math.round(meanEfficiency * 100)), u: "%" }];
  }

  let spreadParts = NONE;
  const spread = standardDeviation(bedtimes);
  if (spread !== null) {
    spreadParts = signedParts(spread, "±");
  }

  let debtParts = NONE;
  if (month.debt_14_min !== null) {
    debtParts = durationParts(Math.max(0, month.debt_14_min));
  }

  let shiftParts = NONE;
  const free = mean(freeMidpoints);
  const work = mean(workMidpoints);
  if (free !== null && work !== null) {
    const shift = Math.round(free - work);
    let sign = "+";
    if (shift < 0) {
      sign = "−";
    } else if (shift === 0) {
      sign = "";
    }
    shiftParts = signedParts(shift, sign);
  }

  return [
    sleepInsight(month),
    { parts: latencyParts, label: "to fall asleep" },
    { parts: efficiencyParts, label: "efficiency" },
    { parts: spreadParts, label: "bedtime spread" },
    { parts: debtParts, label: "sleep debt" },
    { parts: shiftParts, label: "weekend shift" },
  ];
}

/** The sunrise line through each night, broken at a missing night or a jump over SUNRISE_JUMP_MIN. */
export function sunline(points: ({ x: number; y: number; clock: number } | null)[]): string {
  let path = "";
  let before: number | null = null;
  for (const point of points) {
    if (point === null) {
      before = null;
      continue;
    }
    let move = "L";
    if (before === null || Math.abs(point.clock - before) > SUNRISE_JUMP_MIN) {
      move = "M";
    }
    path += `${move}${fixed(point.x)} ${fixed(point.y)}`;
    before = point.clock;
  }
  return path;
}

interface Axis {
  start: number;
  end: number;
}

function axisOf(nights: MonthNight[]): Axis {
  let earliest = Infinity;
  let latest = -Infinity;
  for (const night of nights) {
    earliest = Math.min(earliest, night.bedtime_clock_min);
    latest = Math.max(latest, night.up_clock_min);
    if (night.sunrise_clock_min !== null) {
      latest = Math.max(latest, night.sunrise_clock_min);
    }
  }
  if (earliest === Infinity) {
    return { start: 600, end: 1200 };
  }
  return {
    start: Math.floor((earliest - AXIS_PAD_MIN) / 60) * 60,
    end: Math.ceil((latest + AXIS_PAD_MIN) / 60) * 60,
  };
}

function barOf(night: MonthNight, y: number, toX: (clockMin: number) => number): BarView {
  const bed = night.bedtime_clock_min;
  const spanMin = Math.max(0, night.up_clock_min - bed);
  const atMinute = (minute: number): number => toX(bed + minute);
  const awakeHalf = (): number => AWAKE_HALF;

  let awake = "";
  let asleep = "";
  if (night.onset_clock_min === null || night.wake_clock_min === null) {
    awake = band(y, atMinute, 0, spanMin, awakeHalf);
  } else {
    const onset = Math.max(0, night.onset_clock_min - bed);
    const wake = Math.min(spanMin, night.wake_clock_min - bed);
    const levels = levelsOf(night.levels.peak_db);
    const binMin = night.levels.bin_s / 60;
    const halfWidth = (minute: number): number => {
      const taper = Math.max(0.35, Math.min(1, (minute - onset) / TAPER_MIN, (wake - minute) / TAPER_MIN));
      return (1.2 + 3.4 * Math.pow(levelAt(levels, binMin, minute), 0.7)) * taper;
    };
    awake = band(y, atMinute, 0, onset, awakeHalf) + band(y, atMinute, wake, spanMin, awakeHalf);
    asleep = band(y, atMinute, onset, wake, halfWidth);
  }

  let sunrise: string | null = null;
  if (night.sunrise_clock_min !== null) {
    sunrise = clockFromNoon(night.sunrise_clock_min);
  }
  let sleepParts = NONE;
  let sleepText = "—";
  if (night.sleep_min !== null) {
    const total = Math.round(night.sleep_min);
    sleepParts = [
      { n: String(Math.floor(total / 60)), u: "h" },
      { n: String(total % 60), u: "m" },
    ];
    sleepText = `${Math.floor(total / 60)}h ${total % 60}m`;
  }
  return {
    sessionId: night.session_id,
    awake,
    asleep,
    bedX: toX(bed),
    upX: toX(night.up_clock_min),
    sunriseX: night.sunrise_clock_min === null ? null : toX(night.sunrise_clock_min),
    bed: clockFromNoon(bed),
    up: clockFromNoon(night.up_clock_min),
    sunrise,
    sleepParts,
    sleepText,
    bedtime: clockFromNoon(bed),
  };
}

export function monthView(month: Month): MonthView {
  const byDate = new Map<string, MonthNight>();
  for (const night of month.nights) {
    byDate.set(night.night_of, night);
  }
  const axis = axisOf(month.nights);
  const toX = (clockMin: number): number => PL + ((clockMin - axis.start) / (axis.end - axis.start)) * (PR - PL);
  const bottom = PT + MONTH_DAYS * PITCH;

  const days: MonthDay[] = [];
  const shades: Shade[] = [];
  const sunPoints: ({ x: number; y: number; clock: number } | null)[] = [];
  let glow = "";
  let count = 0;
  for (let index = 0; index < MONTH_DAYS; index++) {
    const nightOf = addDays(month.last, index - (MONTH_DAYS - 1));
    const date = utcDate(nightOf);
    const weekday = date.getUTCDay();
    const y = PT + (MONTH_DAYS - 1 - index) * PITCH + PITCH / 2;
    const free = weekday === 5 || weekday === 6;
    if (free) {
      shades.push({ x: PL - 8, y: y - PITCH / 2 + 1, width: PR - PL + 16, height: PITCH - 2, radius: (PITCH - 2) / 2 });
    }
    const night = byDate.get(nightOf);
    let bar: BarView | null = null;
    if (night === undefined) {
      sunPoints.push(null);
    } else {
      count += 1;
      bar = barOf(night, y, toX);
      glow += bar.asleep;
      if (night.sunrise_clock_min === null) {
        sunPoints.push(null);
      } else {
        sunPoints.push({ x: toX(night.sunrise_clock_min), y, clock: night.sunrise_clock_min });
      }
    }
    days.push({
      nightOf,
      longText: nightDateText(nightOf),
      shortText: `${WEEKDAYS[weekday]} ${date.getUTCDate()}`,
      free,
      y,
      night: bar,
    });
  }

  const grid: AxisLine[] = [];
  const hours: Label[] = [];
  for (let minute = axis.start; minute <= axis.end; minute += 60) {
    const major = (minute + NOON_MIN) % 180 === 0;
    const x = fixed(toX(minute));
    let reach = 3;
    if (major) {
      reach = 7;
      hours.push({ x: toX(minute), y: bottom + 14, text: hourFromNoon(minute) });
    }
    grid.push({ path: `M${x} ${PT}V${bottom + reach}`, major });
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

  const range = rangeText(month.first, month.last);
  return {
    rangeText: range,
    previousLast,
    nextLast,
    days,
    grid,
    hours,
    shades,
    glow,
    sunline: sunline(sunPoints),
    labelY: PT - 12,
    insights: monthInsights(month),
    count,
    summary:
      `One bar per night from ${range}, the latest at the top, from ${hourFromNoon(axis.start)} ` +
      `to ${hourFromNoon(axis.end)}. Each bar runs from bed to up, thick while asleep. ` +
      `Weekend nights are shaded and a line marks sunrise.`,
  };
}

/** The row under a point in chart units, or null outside the rows. */
export function dayAt(y: number): number | null {
  const row = Math.floor((y - PT) / PITCH);
  if (row < 0 || row >= MONTH_DAYS) {
    return null;
  }
  return MONTH_DAYS - 1 - row;
}
