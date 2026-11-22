import type { NightDetail, SoundEvent } from "../api.ts";
import {
  clockText,
  countText,
  durationParts,
  durationText,
  lengthText,
  minuteOfDay,
  nightDateText,
  percentText,
  type Part,
} from "./format.ts";
import { filledLevels, ribbonWidths } from "./ribbon.ts";

export const CLASS_NAMES: Record<string, string> = {
  speech: "Talk",
  snoring: "Snoring",
  breathing: "Breathing",
  gasp: "Gasp",
  cough: "Cough",
  sneeze: "Sneeze",
  movement: "Movement",
  footsteps: "Footsteps",
  door: "Door",
  water: "Water",
  alarm: "Alarm",
  music: "Music",
  traffic: "Traffic",
  dog: "Dog",
  weather: "Weather",
  bird: "Bird",
  silence: "Silence",
  other: "Other",
};

const STIR_CLASSES = new Set(["speech", "cough", "sneeze", "gasp", "alarm", "movement", "footsteps", "door", "water"]);
const CLIP_PAD_FRAMES = 25;
const FRAME_MS = 20;

export interface EventRow {
  index: number;
  className: string;
  kind: string;
  time: string;
  length: string;
  startMin: number;
  endMin: number;
  stir: boolean;
  clip: { segment: number; frame: number; frames: number };
}

export interface InfoRow {
  k: string;
  v: string;
  est: boolean;
}

export interface InfoSection {
  title: string;
  rows: InfoRow[];
}

export interface Insight {
  parts: Part[];
  label: string;
}

export interface DialView {
  bedMin: number;
  spanMin: number;
  sunriseMin: number | null;
  sunsetMin: number | null;
  widths: number[];
}

export interface NightView {
  sessionId: string;
  dateText: string;
  zone: string;
  previous: string | null;
  next: string | null;
  dial: DialView;
  bed: string;
  up: string;
  sunrise: string | null;
  sunset: string | null;
  big: Part[];
  bigLabel: string;
  insights: Insight[];
  events: EventRow[];
  classes: { className: string; kind: string; count: number }[];
  infoRange: string;
  info: InfoSection[];
}

export function kindName(className: string): string {
  const name = CLASS_NAMES[className];
  if (name === undefined) {
    return className;
  }
  return name;
}

export function clipOf(event: SoundEvent): { segment: number; frame: number; frames: number } {
  const frame = Math.max(0, event.start_frame - CLIP_PAD_FRAMES);
  const lead = event.start_frame - frame;
  const frames = lead + Math.round(event.duration_ms / FRAME_MS) + CLIP_PAD_FRAMES;
  return { segment: event.start_segment, frame, frames };
}

export function eventRows(detail: NightDetail, zone: string): EventRow[] {
  const bedtime = detail.metrics.bedtime_unix;
  const rows: EventRow[] = [];
  for (const event of detail.events) {
    let end = event.start_unix + event.duration_ms / 1000;
    if (event.end_unix !== null) {
      end = event.end_unix;
    }
    rows.push({
      index: rows.length,
      className: event.class,
      kind: kindName(event.class),
      time: clockText(event.start_unix, zone),
      length: lengthText(event.duration_ms),
      startMin: (event.start_unix - bedtime) / 60,
      endMin: (end - bedtime) / 60,
      stir: STIR_CLASSES.has(event.class),
      clip: clipOf(event),
    });
  }
  return rows;
}

function classCounts(rows: EventRow[]): { className: string; kind: string; count: number }[] {
  const counts = new Map<string, number>();
  for (const row of rows) {
    counts.set(row.className, (counts.get(row.className) ?? 0) + 1);
  }
  const list: { className: string; kind: string; count: number }[] = [];
  for (const [className, count] of counts) {
    list.push({ className, kind: kindName(className), count });
  }
  list.sort((a, b) => b.count - a.count);
  return list;
}

/** The index of the event under a minute from bed, if any, within a quarter minute. */
export function eventAt(rows: EventRow[], minute: number): number | null {
  for (const row of rows) {
    if (minute >= row.startMin - 0.25 && minute <= row.endMin + 0.25) {
      return row.index;
    }
  }
  return null;
}

function minutesOrNone(minutes: number | null): string {
  if (minutes === null) {
    return "—";
  }
  if (minutes * 60 < 0.5) {
    return "None";
  }
  return lengthText(minutes * 60000);
}

function signed(value: number, digits: number): string {
  const text = value.toFixed(digits);
  if (value > 0) {
    return `+${text}`;
  }
  return text;
}

function hourRange(unix: number | null, zone: string): string {
  if (unix === null) {
    return "—";
  }
  const start = Math.floor(minuteOfDay(unix, zone) / 60);
  const end = (start + 1) % 24;
  const label = (hour: number): number => {
    const value = hour % 12;
    if (value === 0) {
      return 12;
    }
    return value;
  };
  let suffix = "PM";
  if (end < 12 && end !== 0) {
    suffix = "AM";
  }
  return `${label(start)}–${label(end)} ${suffix}`;
}

function dbText(value: number | null): string {
  if (value === null) {
    return "—";
  }
  return `${Math.round(value)} dB`;
}

function weatherRows(detail: NightDetail): InfoRow[] {
  const weather = detail.weather;
  const rows: InfoRow[] = [{ k: "Noise floor", v: dbText(detail.metrics.noise_floor_db), est: false }];
  if (weather === null || weather.temperature_c === null) {
    rows.push({ k: "Outside", v: "—", est: false });
    return rows;
  }
  let outside = `${Math.round(weather.temperature_c)}°C`;
  if (weather.temperature_min_c !== null && weather.temperature_max_c !== null) {
    outside += `, low ${Math.round(weather.temperature_min_c)}, high ${Math.round(weather.temperature_max_c)}`;
  }
  rows.push({ k: "Outside", v: outside, est: false });
  if (weather.humidity_pct !== null) {
    rows.push({ k: "Humidity", v: `${Math.round(weather.humidity_pct)}%`, est: false });
  }
  if (weather.pressure_hpa !== null) {
    let pressure = `${Math.round(weather.pressure_hpa)} hPa`;
    if (weather.pressure_change_hpa !== null) {
      pressure += `, ${signed(weather.pressure_change_hpa, 1)}`;
    }
    rows.push({ k: "Pressure", v: pressure, est: false });
  }
  if (weather.precipitation_mm !== null) {
    let rain = "None";
    if (weather.precipitation_mm >= 0.05) {
      rain = `${weather.precipitation_mm.toFixed(1)} mm`;
    }
    rows.push({ k: "Rain", v: rain, est: false });
  }
  return rows;
}

function dayRows(detail: NightDetail, zone: string): InfoRow[] {
  const context = detail.context;
  if (context === null) {
    return [];
  }
  let nextDay = "Work day";
  if (context.free_day_after) {
    nextDay = "Free day";
  }
  const rows: InfoRow[] = [
    { k: "Sunset", v: clockText(context.sunset_unix, zone), est: false },
    { k: "Sunrise", v: clockText(context.sunrise_unix, zone), est: false },
    { k: "Next day", v: nextDay, est: false },
  ];
  if (context.days_since_clock_change !== null && context.clock_shift_min !== null) {
    let direction = "forward";
    if (context.clock_shift_min < 0) {
      direction = "back";
    }
    const hours = Math.abs(context.clock_shift_min) / 60;
    let days = `${context.days_since_clock_change} days ago`;
    if (context.days_since_clock_change === 0) {
      days = "Tonight";
    } else if (context.days_since_clock_change === 1) {
      days = "1 day ago";
    }
    rows.push({ k: "Clock change", v: `${days}, ${direction} ${hours} h`, est: false });
  }
  return rows;
}

function yesNo(value: boolean | null): string {
  if (value) {
    return "Yes";
  }
  return "No";
}

export function infoSections(detail: NightDetail, zone: string): InfoSection[] {
  const m = detail.metrics;
  const session = detail.session;

  let restless = "None";
  if (m.restless_bouts === null) {
    restless = "—";
  } else if (m.restless_bouts > 0) {
    restless = `${m.restless_bouts}, ${minutesOrNone(m.restless_min)}`;
  }

  let pauses = "—";
  if (m.breathing_pauses !== null) {
    pauses = countText(m.breathing_pauses);
    if (m.breathing_pauses > 0 && m.breathing_pauses_per_hour !== null) {
      pauses += `, ${m.breathing_pauses_per_hour.toFixed(1)} an hour`;
    }
  }

  let alarmRows: InfoRow[] = [{ k: "Alarms", v: "None", est: false }];
  if (m.alarms > 0) {
    alarmRows = [
      { k: "Alarms", v: String(m.alarms), est: false },
      { k: "First", v: clockText(m.first_alarm_unix, zone), est: false },
      { k: "Last", v: clockText(m.last_alarm_unix, zone), est: false },
      { k: "Snooze span", v: minutesOrNone(m.snooze_span_min), est: false },
      { k: "Alarm to up", v: minutesOrNone(m.alarm_to_up_min), est: false },
    ];
  }

  let muted = "None";
  if (session.muted_ms !== null && session.muted_ms > 0) {
    muted = lengthText(session.muted_ms);
  }

  return [
    {
      title: "Sleep",
      rows: [
        { k: "Bed", v: clockText(m.bedtime_unix, zone), est: false },
        { k: "Fell asleep", v: clockText(m.onset_unix, zone), est: true },
        { k: "To fall asleep", v: durationText(m.onset_latency_min), est: true },
        { k: "Woke", v: clockText(m.wake_unix, zone), est: true },
        { k: "To get up", v: minutesOrNone(m.lie_in_min), est: true },
        { k: "Up", v: clockText(m.up_unix, zone), est: false },
        { k: "Time in bed", v: durationText(m.time_in_bed_min), est: false },
        { k: "Asleep", v: durationText(m.sleep_min), est: true },
        { k: "Efficiency", v: percentText(m.efficiency), est: true },
        { k: "Midpoint", v: clockText(m.midpoint_unix, zone), est: true },
        { k: "Restless", v: restless, est: false },
      ],
    },
    {
      title: "Breathing",
      rows: [
        { k: "Snoring", v: minutesOrNone(m.snoring_min), est: false },
        { k: "Share of sleep", v: percentText(m.snoring_share), est: false },
        { k: "Per hour", v: minutesOrNone(m.snoring_per_hour), est: false },
        { k: "Loudest", v: dbText(m.snoring_peak_db), est: false },
        { k: "Worst hour", v: hourRange(m.snoring_worst_hour_unix, zone), est: false },
        { k: "Breathing pauses", v: pauses, est: true },
      ],
    },
    {
      title: "Sounds",
      rows: [
        { k: "Coughs", v: countText(m.coughs), est: false },
        { k: "Sneezes and sniffles", v: countText(m.sneezes), est: false },
        { k: "Sleep talk", v: countText(m.sleep_talk), est: false },
        { k: "Bathroom trips", v: countText(m.bathroom_trips), est: false },
        { k: "Loud moments", v: countText(m.loud_moments), est: false },
      ],
    },
    { title: "Alarms", rows: alarmRows },
    { title: "Room and weather", rows: weatherRows(detail) },
    { title: "Day", rows: dayRows(detail, zone) },
    {
      title: "Recording",
      rows: [
        { k: "Segments", v: String(session.segment_count), est: false },
        { k: "Muted", v: muted, est: false },
        { k: "Missing", v: minutesOrNone(m.missing_min), est: false },
        { k: "Outages", v: countText(detail.outages.length), est: false },
        { k: "Resumed", v: yesNo(session.resumed), est: false },
        { k: "Overflows", v: String(session.buffer_overflows ?? 0), est: false },
        { k: "Card write errors", v: String(session.sd_write_errors ?? 0), est: false },
        { k: "Upload retries", v: String(session.upload_retries ?? 0), est: false },
        { k: "Wi-Fi drops", v: String(session.wifi_disconnects ?? 0), est: false },
        { k: "Firmware", v: session.firmware_version ?? "—", est: false },
      ],
    },
  ];
}

function insightOf(minutes: number | null, label: string): Insight {
  if (minutes === null) {
    return { parts: [{ n: "—", u: "" }], label };
  }
  return { parts: durationParts(minutes), label };
}

export function nightView(detail: NightDetail): NightView {
  const m = detail.metrics;
  let zone = Intl.DateTimeFormat().resolvedOptions().timeZone;
  let dateText = clockText(m.bedtime_unix, zone);
  let sunriseUnix: number | null = null;
  let sunsetUnix: number | null = null;
  if (detail.context !== null) {
    zone = detail.context.timezone;
    dateText = nightDateText(detail.context.night_of);
    sunriseUnix = detail.context.sunrise_unix;
    sunsetUnix = detail.context.sunset_unix;
  }

  const spanMin = (m.up_unix - m.bedtime_unix) / 60;
  const events = eventRows(detail, zone);

  let big = durationParts(m.time_in_bed_min);
  let bigLabel = "in bed";
  if (m.sleep_min !== null) {
    big = durationParts(m.sleep_min);
    bigLabel = "asleep";
  }

  let efficiency: Insight = { parts: [{ n: "—", u: "" }], label: "efficiency" };
  if (m.efficiency !== null) {
    efficiency = { parts: [{ n: String(Math.round(m.efficiency * 100)), u: "%" }], label: "efficiency" };
  }

  let sunrise: string | null = null;
  let sunriseMin: number | null = null;
  if (sunriseUnix !== null) {
    sunrise = clockText(sunriseUnix, zone);
    sunriseMin = minuteOfDay(sunriseUnix, zone);
  }
  let sunset: string | null = null;
  let sunsetMin: number | null = null;
  if (sunsetUnix !== null) {
    sunset = clockText(sunsetUnix, zone);
    sunsetMin = minuteOfDay(sunsetUnix, zone);
  }

  return {
    sessionId: detail.session.session_id,
    dateText,
    zone,
    previous: detail.previous,
    next: detail.next,
    dial: {
      bedMin: minuteOfDay(m.bedtime_unix, zone),
      spanMin,
      sunriseMin,
      sunsetMin,
      widths: ribbonWidths(filledLevels(detail.levels.peak_db), spanMin),
    },
    bed: clockText(m.bedtime_unix, zone),
    up: clockText(m.up_unix, zone),
    sunrise,
    sunset,
    big,
    bigLabel,
    insights: [
      insightOf(m.onset_latency_min, "to fall asleep"),
      insightOf(m.lie_in_min, "to get up"),
      insightOf(m.restless_min, "restless"),
      efficiency,
      insightOf(m.snoring_min, "snoring"),
    ],
    events,
    classes: classCounts(events),
    infoRange: `${clockText(m.bedtime_unix, zone)} to ${clockText(m.up_unix, zone)}`,
    info: infoSections(detail, zone),
  };
}
