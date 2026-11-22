import type { TrendNight, Trends } from "../api.ts";
import { clockFromNoon, addDays } from "./month.ts";
import { durationText, nightDateText } from "./format.ts";

export type RangeName = "week" | "month" | "quarter" | "year";
export type Kind = "duration" | "clock" | "percent" | "minutes";
export type Direction = "more" | "less" | "neither";

export const RANGES: { name: RangeName; label: string; days: number; period: string }[] = [
  { name: "week", label: "Week", days: 7, period: "week" },
  { name: "month", label: "Month", days: 30, period: "month" },
  { name: "quarter", label: "3 months", days: 91, period: "3 months" },
  { name: "year", label: "Year", days: 365, period: "year" },
];

export const MIN_NIGHTS = 14;
export const OVER_TIME = "time";

export interface Metric {
  name: string;
  label: string;
  column: string;
  kind: Kind;
  better: Direction;
}

export const METRICS: Metric[] = [
  { name: "sleep", label: "Sleep time", column: "sleep_min", kind: "duration", better: "more" },
  { name: "bedtime", label: "Bedtime", column: "bedtime_clock_min", kind: "clock", better: "neither" },
  { name: "wake", label: "Wake-up", column: "wake_clock_min", kind: "clock", better: "neither" },
  { name: "midpoint", label: "Midpoint", column: "midpoint_clock_min", kind: "clock", better: "neither" },
  { name: "latency", label: "Time to fall asleep", column: "onset_latency_min", kind: "minutes", better: "less" },
  { name: "efficiency", label: "Efficiency", column: "efficiency", kind: "percent", better: "more" },
  { name: "snoring", label: "Snoring", column: "snoring_min", kind: "minutes", better: "less" },
];

type FactorKind = "number" | "weekday" | "free";

export interface Factor {
  name: string;
  label: string;
  kind: FactorKind;
  column: string;
  step: number;
  stepText: string;
  format: (value: number) => string;
  excludes: string[];
}

function hoursText(value: number): string {
  return durationText(value);
}

export const FACTORS: Factor[] = [
  { name: "bedtime", label: "Bedtime", kind: "number", column: "bedtime_clock_min", step: 60, stepText: "with each hour later to bed", format: clockFromNoon, excludes: ["bedtime", "midpoint"] },
  { name: "wake", label: "Wake-up", kind: "number", column: "wake_clock_min", step: 60, stepText: "with each hour later up", format: clockFromNoon, excludes: ["wake", "midpoint"] },
  { name: "before", label: "The night before's sleep", kind: "number", column: "before_sleep_min", step: 60, stepText: "with each hour more sleep the night before", format: hoursText, excludes: [] },
  { name: "weekday", label: "Day of week", kind: "weekday", column: "weekday", step: 1, stepText: "", format: String, excludes: [] },
  { name: "free", label: "Free day next", kind: "free", column: "free_day_after", step: 1, stepText: "", format: String, excludes: [] },
  { name: "temperature", label: "Temperature", kind: "number", column: "temperature_c", step: 5, stepText: "with each 5 °C warmer", format: (v) => `${Math.round(v)}°C`, excludes: [] },
  { name: "humidity", label: "Humidity", kind: "number", column: "humidity_pct", step: 10, stepText: "with each 10% more humid", format: (v) => `${Math.round(v)}%`, excludes: [] },
  { name: "pressure", label: "Pressure", kind: "number", column: "pressure_hpa", step: 10, stepText: "with each 10 hPa higher pressure", format: (v) => `${Math.round(v)} hPa`, excludes: [] },
  { name: "rain", label: "Rain", kind: "number", column: "precipitation_mm", step: 5, stepText: "with each 5 mm more rain", format: (v) => `${v.toFixed(1)} mm`, excludes: [] },
  { name: "noise", label: "Room noise", kind: "number", column: "noise_floor_db", step: 3, stepText: "with each 3 dB louder room", format: (v) => `${Math.round(v)} dB`, excludes: [] },
  { name: "alarm", label: "First alarm", kind: "number", column: "first_alarm_clock_min", step: 60, stepText: "with each hour later first alarm", format: clockFromNoon, excludes: [] },
  { name: "sunrise", label: "Sunrise", kind: "number", column: "sunrise_clock_min", step: 60, stepText: "with each hour later sunrise", format: clockFromNoon, excludes: [] },
  { name: "snoring", label: "Snoring", kind: "number", column: "snoring_min", step: 10, stepText: "with each 10 min more snoring", format: (v) => `${Math.round(v)}m`, excludes: ["snoring"] },
  { name: "latency", label: "Time to fall asleep", kind: "number", column: "onset_latency_min", step: 10, stepText: "with each 10 min longer to fall asleep", format: (v) => `${Math.round(v)}m`, excludes: ["latency"] },
];

const WEEKDAY_NAMES = ["", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"];
const WEEKDAY_SHORT = ["", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"];

export interface Point {
  x: number;
  y: number;
  nightOf: string;
  free: boolean;
}

export interface Strength {
  value: number | null;
  word: string;
}

export interface FactorChoice {
  name: string;
  label: string;
  strength: Strength;
}

export interface ChartView {
  kind: "time" | "scatter" | "weekday" | "free";
  points: Point[];
  xMin: number;
  xMax: number;
  yMin: number;
  yMax: number;
  xTicks: { x: number; label: string }[];
  yTicks: { y: number; label: string }[];
  mean: { x: number; y: number }[][];
  fit: { slope: number; intercept: number } | null;
  groups: { x: number; y: number; label: string }[];
  target: number | null;
  shadeFree: boolean;
}

export interface TrendsView {
  range: RangeName;
  metric: Metric;
  factor: Factor | null;
  metricChoices: { name: string; label: string; note: string }[];
  factorChoices: FactorChoice[];
  connective: string;
  figure: string;
  line: string;
  chart: ChartView;
  summary: string;
  nights: number;
}

export function valueText(kind: Kind, value: number | null): string {
  if (value === null) {
    return "—";
  }
  if (kind === "duration") {
    return durationText(value);
  }
  if (kind === "clock") {
    return clockFromNoon(value);
  }
  if (kind === "percent") {
    return `${Math.round(value * 100)}%`;
  }
  return `${Math.round(value)}m`;
}

/** A change in a metric's own units, signed: +12m, −3%. */
export function changeText(kind: Kind, delta: number): string {
  let sign = "";
  if (kind === "percent") {
    const points = Math.round(delta * 100);
    if (points > 0) {
      sign = "+";
    } else if (points < 0) {
      sign = "−";
    }
    return `${sign}${Math.abs(points)}%`;
  }
  const minutes = Math.round(delta);
  if (minutes > 0) {
    sign = "+";
  } else if (minutes < 0) {
    sign = "−";
  }
  return `${sign}${durationText(Math.abs(minutes))}`;
}

export function numberOf(night: TrendNight, column: string): number | null {
  const value = night[column];
  if (typeof value === "number" && Number.isFinite(value)) {
    return value;
  }
  if (typeof value === "boolean") {
    return value ? 1 : 0;
  }
  return null;
}

export function mean(values: number[]): number | null {
  if (values.length === 0) {
    return null;
  }
  let sum = 0;
  for (const value of values) {
    sum += value;
  }
  return sum / values.length;
}

/** Least squares through the points: slope, intercept and Pearson's r, or null without spread. */
export function linearFit(points: { x: number; y: number }[]): { slope: number; intercept: number; r: number } | null {
  if (points.length < 2) {
    return null;
  }
  const xs: number[] = [];
  const ys: number[] = [];
  for (const point of points) {
    xs.push(point.x);
    ys.push(point.y);
  }
  const mx = mean(xs) as number;
  const my = mean(ys) as number;
  let sxx = 0;
  let syy = 0;
  let sxy = 0;
  for (const point of points) {
    sxx += (point.x - mx) ** 2;
    syy += (point.y - my) ** 2;
    sxy += (point.x - mx) * (point.y - my);
  }
  if (sxx === 0) {
    return null;
  }
  const slope = sxy / sxx;
  let r = 0;
  if (syy > 0) {
    r = sxy / Math.sqrt(sxx * syy);
  }
  return { slope, intercept: my - slope * mx, r };
}

/** The correlation ratio: how much of the spread the groups explain, 0 to 1. */
export function eta(points: { x: number; y: number }[]): number | null {
  const ys: number[] = [];
  const groups = new Map<number, number[]>();
  for (const point of points) {
    ys.push(point.y);
    const group = groups.get(point.x);
    if (group === undefined) {
      groups.set(point.x, [point.y]);
    } else {
      group.push(point.y);
    }
  }
  const grand = mean(ys);
  if (grand === null || groups.size < 2) {
    return null;
  }
  let total = 0;
  for (const y of ys) {
    total += (y - grand) ** 2;
  }
  if (total === 0) {
    return null;
  }
  let between = 0;
  for (const group of groups.values()) {
    between += group.length * ((mean(group) as number) - grand) ** 2;
  }
  return Math.sqrt(between / total);
}

export function strengthWord(value: number | null, nights: number): string {
  if (nights < MIN_NIGHTS) {
    return "Too few nights";
  }
  if (value === null) {
    return "None";
  }
  const size = Math.abs(value);
  if (size >= 0.5) {
    return "Strong";
  }
  if (size >= 0.3) {
    return "Moderate";
  }
  if (size >= 0.1) {
    return "Weak";
  }
  return "None";
}

/** Each night with the night before's sleep added as before_sleep_min, if that calendar night was recorded. */
export function withNightBefore(nights: TrendNight[]): TrendNight[] {
  const sleepOn = new Map<string, number | null>();
  for (const night of nights) {
    sleepOn.set(night.night_of, numberOf(night, "sleep_min"));
  }
  const out: TrendNight[] = [];
  for (const night of nights) {
    let before = sleepOn.get(addDays(night.night_of, -1));
    if (before === undefined) {
      before = null;
    }
    out.push({ ...night, before_sleep_min: before });
  }
  return out;
}

export function pointsFor(nights: TrendNight[], metric: Metric, column: string): Point[] {
  const points: Point[] = [];
  for (const night of nights) {
    const x = numberOf(night, column);
    const y = numberOf(night, metric.column);
    if (x === null || y === null) {
      continue;
    }
    points.push({ x, y, nightOf: night.night_of, free: night.free_day_after });
  }
  return points;
}

function strengthOf(points: Point[], factor: Factor): Strength {
  let value: number | null = null;
  if (factor.kind === "number") {
    const fit = linearFit(points);
    if (fit !== null) {
      value = Math.abs(fit.r);
    }
  } else {
    value = eta(points);
  }
  return { value, word: strengthWord(value, points.length) };
}

export function factorsFor(metric: Metric): Factor[] {
  const list: Factor[] = [];
  for (const factor of FACTORS) {
    if (!factor.excludes.includes(metric.name)) {
      list.push(factor);
    }
  }
  return list;
}

export function rankedFactors(nights: TrendNight[], metric: Metric): FactorChoice[] {
  const choices: FactorChoice[] = [];
  for (const factor of factorsFor(metric)) {
    const strength = strengthOf(pointsFor(nights, metric, factor.column), factor);
    choices.push({ name: factor.name, label: factor.label, strength });
  }
  choices.sort((a, b) => (b.strength.value ?? -1) - (a.strength.value ?? -1));
  return choices;
}

function dayNumber(isoDate: string): number {
  const [year, month, day] = isoDate.split("-").map(Number);
  return Date.UTC(year, month - 1, day) / 86400000;
}

/** A seven-night centred mean, broken where a night is missing; the raw values for a week. */
export function centredMean(points: Point[], raw: boolean): { x: number; y: number }[][] {
  const runs: { x: number; y: number }[][] = [];
  let run: { x: number; y: number }[] = [];
  for (let index = 0; index < points.length; index++) {
    const point = points[index];
    const previous = points[index - 1];
    if (previous !== undefined && point.x - previous.x > 1 && run.length > 0) {
      runs.push(run);
      run = [];
    }
    if (raw) {
      run.push({ x: point.x, y: point.y });
      continue;
    }
    const near: number[] = [];
    for (const other of points) {
      if (Math.abs(other.x - point.x) <= 3) {
        near.push(other.y);
      }
    }
    run.push({ x: point.x, y: mean(near) as number });
  }
  if (run.length > 0) {
    runs.push(run);
  }
  return runs;
}

function niceStep(span: number, kind: Kind): number {
  if (kind === "percent") {
    for (const step of [0.01, 0.02, 0.05, 0.1, 0.2]) {
      if (span / step <= 5) {
        return step;
      }
    }
    return 0.25;
  }
  for (const step of [5, 10, 15, 30, 60, 120, 180]) {
    if (span / step <= 5) {
      return step;
    }
  }
  return 240;
}

function axisRange(values: number[], kind: Kind, extra: number | null): { min: number; max: number; step: number } {
  let low = Infinity;
  let high = -Infinity;
  for (const value of values) {
    low = Math.min(low, value);
    high = Math.max(high, value);
  }
  if (extra !== null) {
    low = Math.min(low, extra);
    high = Math.max(high, extra);
  }
  if (low === Infinity) {
    low = 0;
    high = 1;
  }
  if (high - low < 1e-9) {
    low -= kind === "percent" ? 0.05 : 15;
    high += kind === "percent" ? 0.05 : 15;
  }
  const step = niceStep(high - low, kind);
  return { min: Math.floor(low / step) * step, max: Math.ceil(high / step) * step, step };
}

export function tickText(label: string): string {
  return label.replace(":00 ", " ").replace(/h 0m$/, "h");
}

function yTicks(range: { min: number; max: number; step: number }, kind: Kind): { y: number; label: string }[] {
  const ticks: { y: number; label: string }[] = [];
  for (let y = range.min; y <= range.max + range.step / 1000; y += range.step) {
    ticks.push({ y, label: tickText(valueText(kind, y)) });
  }
  return ticks;
}

const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];

function dateLabel(day: number): string {
  const date = new Date(day * 86400000);
  return `${date.getUTCDate()} ${MONTHS[date.getUTCMonth()]}`;
}

function timeTicks(first: number, last: number): { x: number; label: string }[] {
  const span = last - first;
  let step = 1;
  for (const candidate of [1, 2, 7, 14, 30, 61]) {
    step = candidate;
    if (span / candidate <= 6) {
      break;
    }
  }
  const ticks: { x: number; label: string }[] = [];
  for (let x = last; x >= first; x -= step) {
    ticks.unshift({ x, label: dateLabel(x) });
  }
  return ticks;
}

function numberTicks(min: number, max: number, factor: Factor): { x: number; label: string }[] {
  const span = max - min;
  let step = factor.step;
  while (span / step > 6) {
    step *= 2;
  }
  while (span / step < 3 && step > 1e-6) {
    step /= 2;
  }
  const ticks: { x: number; label: string }[] = [];
  for (let x = Math.ceil(min / step) * step; x <= max + 1e-9; x += step) {
    ticks.push({ x, label: tickText(factor.format(x)) });
  }
  return ticks;
}

export interface TrendsChoice {
  range: RangeName;
  metric: string;
  factor: string;
}

export function rangeOf(name: RangeName): { name: RangeName; label: string; days: number; period: string } {
  for (const range of RANGES) {
    if (range.name === name) {
      return range;
    }
  }
  return RANGES[1];
}

export function metricOf(name: string): Metric {
  for (const metric of METRICS) {
    if (metric.name === name) {
      return metric;
    }
  }
  return METRICS[0];
}

/** The nights a range asks for, oldest first: this period ending at last, and the one before it. */
export function rangeDates(range: RangeName, last: string): { first: string; previousFirst: string } {
  const days = rangeOf(range).days;
  return { first: addDays(last, 1 - days), previousFirst: addDays(last, 1 - 2 * days) };
}

function overTime(nights: TrendNight[], metric: Metric, range: RangeName, previous: TrendNight[], target: number | null): Pick<TrendsView, "figure" | "line" | "chart" | "summary"> {
  const points: Point[] = [];
  for (const night of nights) {
    const y = numberOf(night, metric.column);
    if (y !== null) {
      points.push({ x: dayNumber(night.night_of), y, nightOf: night.night_of, free: night.free_day_after });
    }
  }
  const values: number[] = [];
  for (const point of points) {
    values.push(point.y);
  }
  const average = mean(values);
  const earlier: number[] = [];
  for (const night of previous) {
    const y = numberOf(night, metric.column);
    if (y !== null) {
      earlier.push(y);
    }
  }
  const before = mean(earlier);
  const period = rangeOf(range).period;
  let line = `No earlier nights to compare`;
  if (average === null) {
    line = "No nights in this range";
  } else if (before !== null) {
    line = `${changeText(metric.kind, average - before)} on the previous ${period}`;
  }

  const shownTarget = metric.name === "sleep" ? target : null;
  const y = axisRange(values, metric.kind, shownTarget);
  const lastDay = points.length > 0 ? points[points.length - 1].x : 0;
  const days = rangeOf(range).days;
  const firstDay = lastDay - days + 1;
  return {
    figure: valueText(metric.kind, average),
    line,
    chart: {
      kind: "time",
      points,
      xMin: firstDay - 0.5,
      xMax: lastDay + 0.5,
      yMin: y.min,
      yMax: y.max,
      xTicks: timeTicks(firstDay, lastDay),
      yTicks: yTicks(y, metric.kind),
      mean: centredMean(points, range === "week"),
      fit: null,
      groups: [],
      target: shownTarget,
      shadeFree: range !== "year",
    },
    summary: `${metric.label} each night over the last ${period}, averaging ${valueText(metric.kind, average)}.`,
  };
}

function against(nights: TrendNight[], metric: Metric, factor: Factor): Pick<TrendsView, "figure" | "line" | "chart" | "summary"> {
  const points = pointsFor(nights, metric, factor.column);
  const strength = strengthOf(points, factor);
  const link = `${strength.word.toLowerCase()} link, ${points.length} nights`;
  const ys: number[] = [];
  for (const point of points) {
    ys.push(point.y);
  }
  const y = axisRange(ys, metric.kind, null);
  const base = {
    points,
    yMin: y.min,
    yMax: y.max,
    yTicks: yTicks(y, metric.kind),
    mean: [],
    target: null,
    shadeFree: false,
  };

  if (factor.kind === "number") {
    const fit = linearFit(points);
    const xs: number[] = [];
    for (const point of points) {
      xs.push(point.x);
    }
    let xMin = Math.min(...xs);
    let xMax = Math.max(...xs);
    if (xs.length === 0) {
      xMin = 0;
      xMax = 1;
    }
    const pad = Math.max((xMax - xMin) * 0.05, factor.step * 0.1);
    let figure = "—";
    let line = `${factor.stepText} · ${link}`;
    if (fit !== null && points.length >= 2) {
      figure = changeText(metric.kind, fit.slope * factor.step);
    } else {
      line = `Too few nights with ${factor.label.toLowerCase()}`;
    }
    return {
      figure,
      line,
      chart: { ...base, kind: "scatter", xMin: xMin - pad, xMax: xMax + pad, xTicks: numberTicks(xMin - pad, xMax + pad, factor), fit, groups: [] },
      summary: `${metric.label} against ${factor.label.toLowerCase()}: ${figure} ${factor.stepText}, ${link}.`,
    };
  }

  const groups = new Map<number, number[]>();
  for (const point of points) {
    const group = groups.get(point.x);
    if (group === undefined) {
      groups.set(point.x, [point.y]);
    } else {
      group.push(point.y);
    }
  }
  const means: { x: number; y: number; label: string }[] = [];
  for (const [x, values] of groups) {
    let label = WEEKDAY_SHORT[x];
    if (factor.kind === "free") {
      label = x === 1 ? "Free day next" : "Work day next";
    }
    means.push({ x, y: mean(values) as number, label });
  }
  means.sort((a, b) => a.x - b.x);

  if (factor.kind === "free") {
    let free: number | null = null;
    let work: number | null = null;
    for (const group of means) {
      if (group.x === 1) {
        free = group.y;
      } else {
        work = group.y;
      }
    }
    let figure = "—";
    if (free !== null && work !== null) {
      figure = changeText(metric.kind, free - work);
    }
    const xTicks = [
      { x: 0, label: "Work day next" },
      { x: 1, label: "Free day next" },
    ];
    return {
      figure,
      line: `before a free day · ${link}`,
      chart: { ...base, kind: "free", xMin: -0.6, xMax: 1.6, xTicks, fit: null, groups: means },
      summary: `${metric.label} before a free day against a work day: ${figure}, ${link}.`,
    };
  }

  let most = means[0];
  let least = means[0];
  for (const group of means) {
    if (group.y > most.y) {
      most = group;
    }
    if (group.y < least.y) {
      least = group;
    }
  }
  const xTicks: { x: number; label: string }[] = [];
  for (let day = 1; day <= 7; day++) {
    xTicks.push({ x: day, label: WEEKDAY_SHORT[day] });
  }
  let figure = "—";
  let line = "No nights in this range";
  if (most !== undefined) {
    let best = most;
    if (metric.better === "less") {
      best = least;
    }
    figure = WEEKDAY_NAMES[best.x];
    const high = metric.kind === "clock" ? "Latest" : "Most";
    const low = metric.kind === "clock" ? "earliest" : "least";
    line = `${high} ${valueText(metric.kind, most.y)} on ${WEEKDAY_SHORT[most.x]}, ${low} ${valueText(metric.kind, least.y)} on ${WEEKDAY_SHORT[least.x]} · ${link}`;
  }
  return {
    figure,
    line,
    chart: { ...base, kind: "weekday", xMin: 0.4, xMax: 7.6, xTicks, fit: null, groups: means },
    summary: `${metric.label} by day of week: ${line}.`,
  };
}

/** What a hovered night shows in the figure. */
export function nightFigure(view: TrendsView, point: Point): { figure: string; line: string } {
  let line = nightDateText(point.nightOf);
  if (view.factor !== null && view.factor.kind === "number") {
    line += ` · ${view.factor.label.toLowerCase()} ${view.factor.format(point.x)}`;
  }
  return { figure: valueText(view.metric.kind, point.y), line };
}

export function trendsView(data: Trends, choice: TrendsChoice): TrendsView {
  const metric = metricOf(choice.metric);
  const all = withNightBefore(data.nights);
  let last = data.latest;
  if (last === null && all.length > 0) {
    last = all[all.length - 1].night_of;
  }
  let first = "";
  if (last !== null) {
    first = rangeDates(choice.range, last).first;
  }
  const nights: TrendNight[] = [];
  const previous: TrendNight[] = [];
  for (const night of all) {
    if (night.night_of >= first) {
      nights.push(night);
    } else {
      previous.push(night);
    }
  }

  const metricChoices: { name: string; label: string; note: string }[] = [];
  for (const candidate of METRICS) {
    const values: number[] = [];
    for (const night of nights) {
      const value = numberOf(night, candidate.column);
      if (value !== null) {
        values.push(value);
      }
    }
    metricChoices.push({ name: candidate.name, label: candidate.label, note: valueText(candidate.kind, mean(values)) });
  }

  let factor: Factor | null = null;
  for (const candidate of factorsFor(metric)) {
    if (candidate.name === choice.factor) {
      factor = candidate;
    }
  }

  let shown;
  if (factor === null) {
    shown = overTime(nights, metric, choice.range, previous, data.sleep_target_min);
  } else {
    shown = against(nights, metric, factor);
  }

  return {
    range: choice.range,
    metric,
    factor,
    metricChoices,
    factorChoices: rankedFactors(nights, metric),
    connective: factor === null ? "over" : "against",
    nights: nights.length,
    ...shown,
  };
}
