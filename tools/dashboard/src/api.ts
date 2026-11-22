export interface NightSummary {
  session_id: string;
  night_of: string;
  weekday: number;
  free_day_after: boolean;
  timezone: string;
  sessions: number;
  bedtime_unix: number;
  onset_unix: number | null;
  wake_unix: number;
  up_unix: number;
  sleep_min: number | null;
  efficiency: number | null;
  onset_latency_min: number | null;
  sunrise_unix: number | null;
  sunset_unix: number | null;
}

export interface SoundEvent {
  class: string;
  start_segment: number;
  start_frame: number;
  end_segment: number;
  end_frame: number;
  start_unix: number;
  end_unix: number | null;
  duration_ms: number;
  peak_score: number;
}

export interface Metrics {
  bedtime_unix: number;
  up_unix: number;
  time_in_bed_min: number;
  onset_unix: number | null;
  onset_latency_min: number | null;
  wake_unix: number;
  lie_in_min: number;
  sleep_min: number | null;
  efficiency: number | null;
  midpoint_unix: number | null;
  restless_bouts: number | null;
  restless_min: number | null;
  snoring_min: number;
  snoring_share: number | null;
  snoring_per_hour: number;
  snoring_peak_db: number | null;
  snoring_worst_hour_unix: number | null;
  breathing_pauses: number | null;
  breathing_pauses_per_hour: number | null;
  coughs: number;
  sneezes: number;
  sleep_talk: number | null;
  bathroom_trips: number | null;
  alarms: number;
  first_alarm_unix: number | null;
  last_alarm_unix: number | null;
  snooze_span_min: number | null;
  alarm_to_up_min: number | null;
  noise_floor_db: number | null;
  loud_moments: number;
  outage_min: number;
  muted_min: number;
  missing_min: number;
}

export interface Session {
  session_id: string;
  started_unix: number;
  ended_unix: number;
  segment_count: number;
  sample_shift: number;
  firmware_version: string | null;
  time_reliable: boolean | null;
  resumed: boolean | null;
  resume_count: number | null;
  recovered: boolean | null;
  muted_ms: number | null;
  buffer_overflows: number | null;
  sd_write_errors: number | null;
  upload_retries: number | null;
  wifi_disconnects: number | null;
}

export interface Context {
  night_of: string;
  weekday: number;
  free_day_after: boolean;
  timezone: string;
  utc_offset_min: number;
  clock_shift_min: number | null;
  days_since_clock_change: number | null;
  sunset_unix: number | null;
  sunrise_unix: number | null;
}

export interface Weather {
  weather_hours: number;
  temperature_c: number | null;
  temperature_min_c: number | null;
  temperature_max_c: number | null;
  humidity_pct: number | null;
  pressure_hpa: number | null;
  pressure_change_hpa: number | null;
  precipitation_mm: number | null;
}

export interface NightDetail {
  session: Session;
  metrics: Metrics;
  context: Context | null;
  weather: Weather | null;
  events: SoundEvent[];
  outages: { start_unix: number; end_unix: number }[];
  levels: Levels;
  previous: string | null;
  next: string | null;
}

export interface Levels {
  start_unix: number;
  bin_s: number;
  peak_db: (number | null)[];
}

export interface MonthNight {
  session_id: string;
  night_of: string;
  weekday: number;
  free_day_after: boolean;
  timezone: string;
  bedtime_clock_min: number;
  onset_clock_min: number | null;
  wake_clock_min: number | null;
  up_clock_min: number;
  midpoint_clock_min: number | null;
  sunrise_clock_min: number | null;
  sleep_min: number | null;
  onset_latency_min: number | null;
  efficiency: number | null;
  bedtime_unix: number;
  up_unix: number;
  levels: Levels;
}

export interface Month {
  first: string;
  last: string;
  earliest: string | null;
  latest: string | null;
  sleep_target_min: number | null;
  debt_14_min: number | null;
  nights: MonthNight[];
}

export type TrendNight = Record<string, number | string | boolean | null> & {
  night_of: string;
  session_id: string;
  free_day_after: boolean;
  weekday: number;
};

export interface Trends {
  sleep_target_min: number | null;
  earliest: string | null;
  latest: string | null;
  nights: TrendNight[];
}

export interface Status {
  last_ingested_unix: number | null;
  sessions: number;
}

async function getJson<T>(path: string): Promise<T> {
  const response = await fetch(path);
  if (!response.ok) {
    throw new Error(`${path}: ${response.status}`);
  }
  return (await response.json()) as T;
}

export function fetchNights(): Promise<NightSummary[]> {
  return getJson("api/nights");
}

export function fetchNight(sessionId: string): Promise<NightDetail> {
  return getJson(`api/night/${encodeURIComponent(sessionId)}`);
}

export function fetchMonth(last: string | null): Promise<Month> {
  if (last === null) {
    return getJson("api/month");
  }
  return getJson(`api/month?to=${encodeURIComponent(last)}`);
}

export function fetchTrends(first: string, last: string): Promise<Trends> {
  return getJson(`api/trends?from=${encodeURIComponent(first)}&to=${encodeURIComponent(last)}`);
}

export function fetchStatus(): Promise<Status> {
  return getJson("api/status");
}

export function clipUrl(
  sessionId: string,
  segment: number,
  frame: number,
  frames: number,
): string {
  return `api/audio/${encodeURIComponent(sessionId)}?segment=${segment}&frame=${frame}&frames=${frames}`;
}
