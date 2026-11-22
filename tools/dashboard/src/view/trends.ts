import type { TrendNight, Trends } from "../api.ts";
import type { Part } from "./format.ts";

export type RangeName = "week" | "month" | "quarter" | "year";
export type Kind = "dur" | "clock" | "min" | "pct" | "num";

export const RANGES: { name: RangeName; label: string; days: number }[] = [
  { name: "week", label: "Week", days: 7 },
  { name: "month", label: "Month", days: 30 },
  { name: "quarter", label: "3 months", days: 91 },
  { name: "year", label: "Year", days: 365 },
];

export const MIN_NIGHTS = 14;
export const OVER_TIME = "time";

export const CHART_W = 1200;
export const CHART_H = 720;
const PL = 64;
const PR = 1184;
const PT = 236;
const PB = 650;
const HOVER_REACH = 18;

export interface Metric {
  name: string;
  title: string;
  phrase: string;
  column: string;
  kind: Kind;
  scale: number;
  more: string;
  less: string;
  top: string;
  bottom: string;
}

export const METRICS: Metric[] = [
  { name: "sleep", title: "Sleep time", phrase: "sleep time", column: "sleep_min", kind: "dur", scale: 1, more: "more", less: "less", top: "most", bottom: "least" },
  { name: "bedtime", title: "Bedtime", phrase: "bedtime", column: "bedtime_clock_min", kind: "clock", scale: 1, more: "later", less: "earlier", top: "latest", bottom: "earliest" },
  { name: "wake", title: "Wake-up", phrase: "wake-up", column: "wake_clock_min", kind: "clock", scale: 1, more: "later", less: "earlier", top: "latest", bottom: "earliest" },
  { name: "midpoint", title: "Sleep midpoint", phrase: "sleep midpoint", column: "midpoint_clock_min", kind: "clock", scale: 1, more: "later", less: "earlier", top: "latest", bottom: "earliest" },
  { name: "latency", title: "Time to fall asleep", phrase: "to fall asleep", column: "onset_latency_min", kind: "min", scale: 1, more: "longer", less: "shorter", top: "longest", bottom: "shortest" },
  { name: "efficiency", title: "Efficiency", phrase: "efficiency", column: "efficiency", kind: "pct", scale: 100, more: "higher", less: "lower", top: "highest", bottom: "lowest" },
  { name: "snoring", title: "Snoring", phrase: "snoring", column: "snoring_min", kind: "min", scale: 1, more: "more", less: "less", top: "most", bottom: "least" },
];

export interface Factor {
  name: string;
  label: string;
  column: string;
  kind: Kind;
  unit: string;
  step: number;
  words: string;
  categories: string[] | null;
}

const DAYS = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"];
const DAY_NAMES = ["Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"];
const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];

function numeric(name: string, label: string, column: string, kind: Kind, step: number, words: string, unit = ""): Factor {
  return { name, label, column, kind, unit, step, words, categories: null };
}

export const FACTORS: Factor[] = [
  numeric("bedtime", "Bedtime", "bedtime_clock_min", "clock", 60, "hour later to bed"),
  numeric("wake", "Wake-up", "wake_clock_min", "clock", 60, "hour later up"),
  numeric("before", "Night before", "before_sleep_min", "dur", 60, "hour more sleep the night before"),
  { name: "weekday", label: "Day of week", column: "weekday", kind: "num", unit: "", step: 1, words: "", categories: DAYS },
  { name: "free", label: "Free day next", column: "free_day_after", kind: "num", unit: "", step: 1, words: "", categories: ["Work day next", "Free day next"] },
  numeric("temperature", "Temperature", "temperature_c", "num", 5, "5°C warmer outside", "°C"),
  numeric("humidity", "Humidity", "humidity_pct", "num", 10, "10% more humid", "%"),
  numeric("pressure", "Pressure", "pressure_hpa", "num", 10, "10 hPa higher pressure", " hPa"),
  numeric("rain", "Rain", "precipitation_mm", "num", 5, "5 mm more rain", " mm"),
  numeric("noise", "Room noise", "noise_floor_db", "num", 3, "3 dB louder room", " dB"),
  numeric("alarm", "First alarm", "first_alarm_clock_min", "clock", 60, "hour later alarm"),
  numeric("sunrise", "Sunrise", "sunrise_clock_min", "clock", 60, "hour later sunrise"),
  numeric("snoring", "Snoring", "snoring_min", "min", 10, "10m more snoring"),
  numeric("latency", "Falling asleep", "onset_latency_min", "min", 10, "10m longer to fall asleep"),
];

export interface TrendsChoice {
  range: RangeName;
  metric: string;
  factor: string;
}

export interface MetricItem {
  name: string;
  title: string;
  average: string;
  selected: boolean;
}

export interface FactorItem {
  name: string;
  label: string;
  word: string;
  strength: number | null;
  meter: boolean;
  selected: boolean;
}

export interface Label {
  x: number;
  y: number;
  text: string;
}

export interface Band {
  x: number;
  y: number;
  width: number;
  height: number;
  radius: number;
}

export interface Hit {
  day: number;
  x: number;
  y: number;
}

export interface TrendsView {
  rangeIndex: number;
  metric: Metric;
  factor: Factor | null;
  joiner: string;
  factorText: string;
  metricItems: MetricItem[];
  factorItems: FactorItem[];
  figure: Part[];
  sub: string;
  summary: string;
  grid: string;
  target: string;
  bands: Band[];
  dots: string;
  dotFill: string;
  line: string;
  means: string;
  yLabels: Label[];
  xLabels: Label[];
  tags: Label[];
  radius: number;
  hits: Hit[];
  columns: { from: number; slot: number } | null;
}

export interface Hover {
  figure: Part[];
  sub: string;
  hair: string;
  pick: string;
}

interface Series {
  first: string;
  count: number;
  values: Map<string, (number | null)[]>;
  free: boolean[];
}

function dayNumber(isoDate: string): number {
  const [year, month, day] = isoDate.split("-").map(Number);
  return Math.round(Date.UTC(year, month - 1, day) / 86400000);
}

function dateOf(series: Series, day: number): Date {
  return new Date((dayNumber(series.first) + day) * 86400000);
}

function dayText(series: Series, day: number): string {
  const date = dateOf(series, day);
  return `${DAYS[(date.getUTCDay() + 6) % 7]} ${date.getUTCDate()} ${MONTHS[date.getUTCMonth()]}`;
}

function fixed(value: number): string {
  return value.toFixed(1);
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

/** One slot per calendar day from the first night to the latest, null where a night or value is missing. */
export function seriesOf(data: Trends): Series {
  let first = data.earliest;
  let last = data.latest;
  if (data.nights.length > 0) {
    if (first === null) {
      first = data.nights[0].night_of;
    }
    if (last === null) {
      last = data.nights[data.nights.length - 1].night_of;
    }
  }
  if (first === null || last === null) {
    return { first: "2000-01-01", count: 0, values: new Map(), free: [] };
  }
  const count = dayNumber(last) - dayNumber(first) + 1;
  const columns = new Set<string>();
  for (const metric of METRICS) {
    columns.add(metric.column);
  }
  for (const factor of FACTORS) {
    columns.add(factor.column);
  }
  const values = new Map<string, (number | null)[]>();
  for (const column of columns) {
    values.set(column, new Array<number | null>(count).fill(null));
  }
  const free = new Array<boolean>(count).fill(false);
  const start = dayNumber(first);
  for (const night of data.nights) {
    const day = dayNumber(night.night_of) - start;
    if (day < 0 || day >= count) {
      continue;
    }
    free[day] = night.free_day_after === true;
    for (const column of columns) {
      let value = numberOf(night, column);
      if (column === "weekday" && value !== null) {
        value -= 1;
      }
      (values.get(column) as (number | null)[])[day] = value;
    }
  }
  const sleep = values.get("sleep_min") as (number | null)[];
  const before = values.get("before_sleep_min") as (number | null)[];
  for (let day = 0; day < count; day++) {
    before[day] = day > 0 ? sleep[day - 1] : null;
  }
  for (const metric of METRICS) {
    if (metric.scale !== 1) {
      const column = values.get(metric.column) as (number | null)[];
      for (let day = 0; day < count; day++) {
        const value = column[day];
        column[day] = value === null ? null : value * metric.scale;
      }
    }
  }
  return { first, count, values, free };
}

function valueAt(series: Series, column: string, day: number): number | null {
  if (day < 0 || day >= series.count) {
    return null;
  }
  const values = series.values.get(column);
  if (values === undefined) {
    return null;
  }
  return values[day];
}

function durationParts(minutes: number): Part[] {
  const total = Math.round(Math.abs(minutes));
  if (total < 60) {
    return [{ n: String(total), u: "m" }];
  }
  return [
    { n: String(Math.floor(total / 60)), u: "h" },
    { n: String(total % 60), u: "m" },
  ];
}

function clockPart(value: number): Part {
  const clock = (((Math.round(value) + 720) % 1440) + 1440) % 1440;
  const hour = Math.floor(clock / 60);
  return { n: `${hour % 12 || 12}:${String(clock % 60).padStart(2, "0")}`, u: hour < 12 ? "AM" : "PM" };
}

export function valueParts(kind: Kind, value: number): Part[] {
  if (kind === "clock") {
    return [clockPart(value)];
  }
  if (kind === "pct") {
    return [{ n: String(Math.round(value)), u: "%" }];
  }
  if (kind === "num") {
    return [{ n: value.toFixed(Math.abs(value) < 10 ? 1 : 0), u: "" }];
  }
  return durationParts(value);
}

export function valueText(kind: Kind, value: number, unit = ""): string {
  const words: string[] = [];
  for (const part of valueParts(kind, value)) {
    if (part.u === "AM" || part.u === "PM") {
      words.push(`${part.n} ${part.u}`);
    } else {
      words.push(`${part.n}${part.u}`);
    }
  }
  return words.join(" ") + unit;
}

export function signedParts(kind: Kind, value: number): Part[] {
  const sign = value < 0 ? "−" : "+";
  let parts: Part[];
  if (kind === "pct") {
    parts = [{ n: Math.abs(value).toFixed(1), u: "%" }];
  } else {
    parts = durationParts(value);
  }
  parts[0] = { n: sign + parts[0].n, u: parts[0].u };
  return parts;
}

export function pearson(xs: number[], ys: number[]): { r: number; slope: number; intercept: number } {
  const mx = mean(xs) ?? 0;
  const my = mean(ys) ?? 0;
  let sxy = 0;
  let sxx = 0;
  let syy = 0;
  for (let index = 0; index < xs.length; index++) {
    sxy += (xs[index] - mx) * (ys[index] - my);
    sxx += (xs[index] - mx) ** 2;
    syy += (ys[index] - my) ** 2;
  }
  let r = 0;
  if (sxx !== 0 && syy !== 0) {
    r = sxy / Math.sqrt(sxx * syy);
  }
  let slope = 0;
  if (sxx !== 0) {
    slope = sxy / sxx;
  }
  return { r, slope, intercept: my - slope * mx };
}

interface Pair {
  day: number;
  x: number;
  y: number;
}

/** The correlation ratio: how much of the spread the groups explain, 0 to 1. */
export function eta(pairs: Pair[], groups: number): number {
  const ys: number[] = [];
  for (const pair of pairs) {
    ys.push(pair.y);
  }
  const all = mean(ys) ?? 0;
  let between = 0;
  let total = 0;
  for (let group = 0; group < groups; group++) {
    const members: number[] = [];
    for (const pair of pairs) {
      if (pair.x === group) {
        members.push(pair.y);
      }
    }
    const groupMean = mean(members);
    if (groupMean !== null) {
      between += members.length * (groupMean - all) ** 2;
    }
  }
  for (const pair of pairs) {
    total += (pair.y - all) ** 2;
  }
  if (total === 0) {
    return 0;
  }
  return Math.sqrt(between / total);
}

export function strengthWord(strength: number | null): string {
  if (strength === null) {
    return "Too few nights";
  }
  if (strength >= 0.5) {
    return "Strong";
  }
  if (strength >= 0.3) {
    return "Moderate";
  }
  if (strength >= 0.1) {
    return "Weak";
  }
  return "None";
}

function pairsOf(series: Series, metric: Metric, factor: Factor, from: number): Pair[] {
  const pairs: Pair[] = [];
  for (let day = from; day < series.count; day++) {
    const x = valueAt(series, factor.column, day);
    const y = valueAt(series, metric.column, day);
    if (x !== null && y !== null) {
      pairs.push({ day, x, y });
    }
  }
  return pairs;
}

function strengthOf(pairs: Pair[], factor: Factor): number | null {
  if (pairs.length < MIN_NIGHTS) {
    return null;
  }
  if (factor.categories !== null) {
    return eta(pairs, factor.categories.length);
  }
  const xs: number[] = [];
  const ys: number[] = [];
  for (const pair of pairs) {
    xs.push(pair.x);
    ys.push(pair.y);
  }
  return Math.abs(pearson(xs, ys).r);
}

export function factorsFor(metric: Metric): Factor[] {
  const list: Factor[] = [];
  for (const factor of FACTORS) {
    if (factor.name === metric.name) {
      continue;
    }
    if (metric.name === "midpoint" && (factor.name === "bedtime" || factor.name === "wake")) {
      continue;
    }
    list.push(factor);
  }
  return list;
}

export interface Axis {
  min: number;
  max: number;
  ticks: number[];
}

export function niceAxis(low: number, high: number, kind: Kind): Axis {
  let steps = [0.5, 1, 2, 5, 10, 20, 50];
  if (kind === "dur" || kind === "clock") {
    steps = [15, 30, 60, 120, 180, 240];
  } else if (kind === "pct") {
    steps = [1, 2, 5, 10, 20];
  }
  let step = steps[steps.length - 1];
  for (const candidate of steps) {
    if ((high - low) / candidate <= 4) {
      step = candidate;
      break;
    }
  }
  const min = Math.floor(low / step) * step;
  let max = Math.ceil(high / step) * step;
  if (max === min) {
    max = min + step;
  }
  const ticks: number[] = [];
  for (let value = min; value <= max + 1e-9; value += step) {
    ticks.push(value);
  }
  return { min, max, ticks };
}

export function tickText(kind: Kind, value: number, unit = ""): string {
  if (kind === "clock") {
    const clock = clockPart(value);
    if (clock.n.endsWith(":00")) {
      return `${clock.n.slice(0, -3)} ${clock.u}`;
    }
    return `${clock.n} ${clock.u}`;
  }
  if (kind === "dur") {
    if (value % 60 !== 0) {
      return `${Math.floor(value / 60)}h ${value % 60}m`;
    }
    return `${value / 60}h`;
  }
  if (kind === "min") {
    return `${value}m`;
  }
  if (kind === "pct") {
    return `${value}%`;
  }
  return `${value}${unit}`;
}

export function dotPath(x: number, y: number, radius: number): string {
  return `M${fixed(x - radius)} ${fixed(y)}a${radius} ${radius} 0 1 0 ${2 * radius} 0a${radius} ${radius} 0 1 0 ${-2 * radius} 0`;
}

function jitter(day: number, slot: number): number {
  return (((day * 2654435761) % 1000) / 1000 - 0.5) * Math.min(slot * 0.5, 90);
}

export function metricOf(name: string): Metric {
  for (const metric of METRICS) {
    if (metric.name === name) {
      return metric;
    }
  }
  return METRICS[0];
}

export function rangeIndexOf(name: RangeName): number {
  const index = RANGES.findIndex((range) => range.name === name);
  if (index < 0) {
    return 1;
  }
  return index;
}

export function trendsView(data: Trends, choice: TrendsChoice): TrendsView {
  const series = seriesOf(data);
  const rangeIndex = rangeIndexOf(choice.range);
  const days = RANGES[rangeIndex].days;
  const from = series.count - days;
  const metric = metricOf(choice.metric);

  const shown: number[] = [];
  for (let day = from; day < series.count; day++) {
    const value = valueAt(series, metric.column, day);
    if (value !== null) {
      shown.push(value);
    }
  }
  const average = mean(shown);

  const metricItems: MetricItem[] = [];
  for (const candidate of METRICS) {
    const values: number[] = [];
    for (let day = from; day < series.count; day++) {
      const value = valueAt(series, candidate.column, day);
      if (value !== null) {
        values.push(value);
      }
    }
    const candidateMean = mean(values);
    metricItems.push({
      name: candidate.name,
      title: candidate.title,
      average: candidateMean === null ? "" : valueText(candidate.kind, candidateMean),
      selected: candidate.name === metric.name,
    });
  }

  const ranked: { factor: Factor; strength: number | null; pairs: Pair[] }[] = [];
  for (const factor of factorsFor(metric)) {
    const pairs = pairsOf(series, metric, factor, from);
    ranked.push({ factor, strength: strengthOf(pairs, factor), pairs });
  }
  ranked.sort((a, b) => (b.strength ?? -1) - (a.strength ?? -1));

  let picked: (typeof ranked)[number] | null = null;
  for (const entry of ranked) {
    if (entry.factor.name === choice.factor) {
      picked = entry;
    }
  }

  const factorItems: FactorItem[] = [
    { name: OVER_TIME, label: "Over time", word: "", strength: null, meter: false, selected: picked === null },
  ];
  for (const entry of ranked) {
    factorItems.push({
      name: entry.factor.name,
      label: entry.factor.label,
      word: strengthWord(entry.strength),
      strength: entry.strength,
      meter: true,
      selected: picked !== null && entry.factor.name === picked.factor.name,
    });
  }

  let low = Infinity;
  let high = -Infinity;
  for (const value of shown) {
    low = Math.min(low, value);
    high = Math.max(high, value);
  }
  const target = metric.name === "sleep" && picked === null ? data.sleep_target_min : null;
  if (target !== null) {
    low = Math.min(low, target);
    high = Math.max(high, target);
  }
  if (low === Infinity) {
    low = 0;
    high = 1;
  }
  const yAxis = niceAxis(low, high, metric.kind);
  const toY = (value: number): number => PB - ((value - yAxis.min) / (yAxis.max - yAxis.min)) * (PB - PT);

  let grid = "";
  const yLabels: Label[] = [];
  for (const value of yAxis.ticks) {
    grid += `M${PL} ${fixed(toY(value))}H${PR}`;
    yLabels.push({ x: PL - 14, y: toY(value), text: tickText(metric.kind, value) });
  }

  let radius = 1.7;
  if (days <= 7) {
    radius = 4;
  } else if (days <= 30) {
    radius = 3;
  } else if (days <= 91) {
    radius = 2.3;
  }

  const view: TrendsView = {
    rangeIndex,
    metric,
    factor: picked === null ? null : picked.factor,
    joiner: picked === null ? "over" : "against",
    factorText: picked === null ? "time" : picked.factor.label.toLowerCase(),
    metricItems,
    factorItems,
    figure: [{ n: "—", u: "" }],
    sub: "",
    summary: "",
    grid,
    target: "",
    bands: [],
    dots: "",
    dotFill: "rgba(151,171,255,.55)",
    line: "",
    means: "",
    yLabels,
    xLabels: [],
    tags: [],
    radius,
    hits: [],
    columns: null,
  };

  if (picked === null) {
    overTime(view, series, data, from, days, average, shown.length, toY);
  } else if (picked.factor.categories !== null) {
    byCategory(view, picked.factor, picked.pairs, toY);
  } else {
    byNumber(view, picked.factor, picked.strength, picked.pairs, toY);
  }
  return view;
}

function overTime(
  view: TrendsView,
  series: Series,
  data: Trends,
  from: number,
  days: number,
  average: number | null,
  count: number,
  toY: (value: number) => number,
): void {
  const metric = view.metric;
  const slot = (PR - PL) / days;
  const toX = (day: number): number => PL + (day - from + 0.5) * slot;
  view.columns = { from, slot };
  if (days <= 7) {
    view.dotFill = "#97ABFF";
  }

  for (let day = from; day < series.count; day++) {
    if (days <= 91 && day >= 0 && series.free[day]) {
      view.bands.push({ x: toX(day) - slot / 2, y: PT, width: slot, height: PB - PT, radius: Math.min(slot / 2, 6) });
    }
    const value = valueAt(series, metric.column, day);
    if (value !== null) {
      view.dots += dotPath(toX(day), toY(value), view.radius);
      view.hits.push({ day, x: toX(day), y: toY(value) });
    }
  }

  let half = 7;
  if (days <= 7) {
    half = 0;
  } else if (days <= 91) {
    half = 3;
  }
  let open = false;
  for (let day = from; day < series.count; day++) {
    let value: number | null = null;
    if (half === 0) {
      value = valueAt(series, metric.column, day);
    } else {
      const window: number[] = [];
      for (let near = day - half; near <= day + half; near++) {
        const nearValue = valueAt(series, metric.column, near);
        if (nearValue !== null) {
          window.push(nearValue);
        }
      }
      if (window.length > half) {
        value = mean(window);
      }
    }
    if (value === null) {
      open = false;
      continue;
    }
    view.line += `${open ? "L" : "M"}${fixed(toX(day))} ${fixed(toY(value))}`;
    open = true;
  }

  if (metric.name === "sleep" && data.sleep_target_min !== null) {
    view.target = `M${PL} ${fixed(toY(data.sleep_target_min))}H${PR}`;
    view.tags.push({ x: PR, y: toY(data.sleep_target_min), text: `${data.sleep_target_min / 60}h target` });
  }

  for (let day = from; day < series.count; day++) {
    const date = dateOf(series, day);
    const back = series.count - 1 - day;
    let label: string | null = null;
    if (days <= 7) {
      label = `${DAYS[(date.getUTCDay() + 6) % 7]} ${date.getUTCDate()}`;
    } else if (days <= 30) {
      if (back % 7 === 0) {
        label = `${date.getUTCDate()} ${MONTHS[date.getUTCMonth()]}`;
      }
    } else if (date.getUTCDate() === 1) {
      label = MONTHS[date.getUTCMonth()];
    }
    if (label !== null) {
      view.xLabels.push({ x: toX(day), y: PB + 16, text: label });
    }
  }

  if (average !== null) {
    view.figure = valueParts(metric.kind, average);
  }
  const before: number[] = [];
  for (let day = from - days; day < from; day++) {
    const value = valueAt(series, metric.column, day);
    if (value !== null) {
      before.push(value);
    }
  }
  const prior = mean(before);
  let change: number | null = null;
  if (average !== null && prior !== null && before.length >= days / 2) {
    change = average - prior;
  }
  view.sub = `average of ${count} nights`;
  if (change !== null) {
    const same = metric.kind === "pct" ? 0.5 : 1;
    if (Math.abs(change) < same) {
      view.sub += ` · same as the ${days} before`;
    } else {
      let amount = `${Math.abs(change).toFixed(1)}%`;
      if (metric.kind !== "pct") {
        const words: string[] = [];
        for (const part of durationParts(change)) {
          words.push(part.n + part.u);
        }
        amount = words.join(" ");
      }
      view.sub += ` · ${amount} ${change > 0 ? metric.more : metric.less} than the ${days} before`;
    }
  }
  let averageText = "none recorded";
  if (average !== null) {
    averageText = valueText(metric.kind, average);
  }
  view.summary =
    `${metric.title} each night over the last ${days} nights, with a ${half === 0 ? "one" : "seven"}-night line. ` +
    `Average ${averageText}.`;
}

function byCategory(view: TrendsView, factor: Factor, pairs: Pair[], toY: (value: number) => number): void {
  const metric = view.metric;
  const categories = factor.categories as string[];
  const slot = (PR - PL) / categories.length;
  const toX = (group: number): number => PL + (group + 0.5) * slot;
  for (const pair of pairs) {
    const x = toX(pair.x) + jitter(pair.day, slot);
    view.dots += dotPath(x, toY(pair.y), view.radius);
    view.hits.push({ day: pair.day, x, y: toY(pair.y) });
  }
  const averages: (number | null)[] = [];
  for (let group = 0; group < categories.length; group++) {
    const members: number[] = [];
    for (const pair of pairs) {
      if (pair.x === group) {
        members.push(pair.y);
      }
    }
    const groupMean = mean(members);
    averages.push(groupMean);
    view.xLabels.push({ x: toX(group), y: PB + 16, text: categories[group] });
    if (groupMean !== null) {
      view.means += `M${fixed(toX(group) - 22)} ${fixed(toY(groupMean))}H${fixed(toX(group) + 22)}`;
    }
  }
  view.line = view.means;

  if (categories.length === 2) {
    let difference = 0;
    if (averages[0] !== null && averages[1] !== null) {
      difference = averages[1] - averages[0];
    }
    view.figure = signedParts(metric.kind, difference);
    view.sub = `${metric.more} before a free day · ${pairs.length} nights`;
  } else {
    let top = -1;
    let bottom = -1;
    averages.forEach((value, group) => {
      if (value === null) {
        return;
      }
      if (top < 0 || value > (averages[top] as number)) {
        top = group;
      }
      if (bottom < 0 || value < (averages[bottom] as number)) {
        bottom = group;
      }
    });
    if (top < 0) {
      view.figure = [{ n: "—", u: "" }];
      view.sub = "no nights in this range";
    } else {
      view.figure = [{ n: DAY_NAMES[top], u: "" }];
      view.sub =
        `${metric.top} ${metric.phrase}, ${valueText(metric.kind, averages[top] as number)} · ` +
        `${metric.bottom} ${DAYS[bottom]}, ${valueText(metric.kind, averages[bottom] as number)}`;
    }
  }
  view.summary = `${metric.title} by ${factor.label.toLowerCase()} over the range, with each group's average.`;
}

function byNumber(
  view: TrendsView,
  factor: Factor,
  strength: number | null,
  pairs: Pair[],
  toY: (value: number) => number,
): void {
  const metric = view.metric;
  const xs: number[] = [];
  const ys: number[] = [];
  for (const pair of pairs) {
    xs.push(pair.x);
    ys.push(pair.y);
  }
  let low = 0;
  let high = 1;
  if (xs.length > 0) {
    low = Math.min(...xs);
    high = Math.max(...xs);
  }
  const xAxis = niceAxis(low, high, factor.kind);
  const toX = (value: number): number => PL + ((value - xAxis.min) / (xAxis.max - xAxis.min)) * (PR - PL);
  for (const pair of pairs) {
    view.dots += dotPath(toX(pair.x), toY(pair.y), view.radius);
    view.hits.push({ day: pair.day, x: toX(pair.x), y: toY(pair.y) });
  }
  for (const value of xAxis.ticks) {
    view.grid += `M${fixed(toX(value))} ${PT}V${PB}`;
    view.xLabels.push({ x: toX(value), y: PB + 16, text: tickText(factor.kind, value, factor.unit) });
  }
  const fit = pearson(xs, ys);
  if (strength !== null) {
    view.line =
      `M${fixed(toX(low))} ${fixed(toY(fit.intercept + fit.slope * low))}` +
      `L${fixed(toX(high))} ${fixed(toY(fit.intercept + fit.slope * high))}`;
    view.figure = signedParts(metric.kind, fit.slope * factor.step);
    view.sub = `with each ${factor.words} · ${strengthWord(strength).toLowerCase()} link, ${pairs.length} nights`;
  } else {
    view.figure = [{ n: String(pairs.length), u: "" }];
    view.sub = `nights so far; a link needs ${MIN_NIGHTS}`;
  }
  view.summary =
    `${metric.title} against ${factor.label.toLowerCase()} over the range, one dot per night, ` +
    `with a fitted line. ${strengthWord(strength)} link.`;
}

/** The night under a point in chart units, or null. */
export function hoverAt(view: TrendsView, x: number, y: number): number | null {
  if (view.columns !== null) {
    if (x < PL || x > PR) {
      return null;
    }
    const day = view.columns.from + Math.floor((x - PL) / view.columns.slot);
    for (const hit of view.hits) {
      if (hit.day === day) {
        return day;
      }
    }
    return null;
  }
  let best: number | null = null;
  let distance = HOVER_REACH;
  for (const hit of view.hits) {
    const away = Math.hypot(hit.x - x, hit.y - y);
    if (away < distance) {
      distance = away;
      best = hit.day;
    }
  }
  return best;
}

/** What a hovered night shows: its value, its date, and where to mark it. */
export function hoverView(view: TrendsView, data: Trends, day: number): Hover | null {
  let hit: Hit | null = null;
  for (const candidate of view.hits) {
    if (candidate.day === day) {
      hit = candidate;
    }
  }
  if (hit === null) {
    return null;
  }
  const series = seriesOf(data);
  const value = valueAt(series, view.metric.column, day) as number;
  let sub = dayText(series, day);
  const factor = view.factor;
  if (factor !== null && factor.categories === null) {
    const x = valueAt(series, factor.column, day);
    if (x !== null) {
      sub += ` · ${factor.label.toLowerCase()} ${valueText(factor.kind, x, factor.unit)}`;
    }
  }
  let hair = "";
  if (view.columns !== null) {
    hair = `M${fixed(hit.x)} ${PT}V${PB}`;
  }
  return {
    figure: valueParts(view.metric.kind, value),
    sub,
    hair,
    pick: dotPath(hit.x, hit.y, view.radius + 2),
  };
}
