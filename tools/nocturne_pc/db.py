"""The pipeline's DuckDB database, nocturne.duckdb at the data root."""

from pathlib import Path

import duckdb

from nocturne_pc import data_root

DATABASE_NAME = "nocturne.duckdb"

SCHEMA = """
CREATE TABLE IF NOT EXISTS sessions (
    session_id VARCHAR PRIMARY KEY,
    device_id VARCHAR NOT NULL,
    started_unix BIGINT NOT NULL,
    ended_unix BIGINT NOT NULL,
    segment_count INTEGER NOT NULL,
    sample_rate INTEGER,
    channels INTEGER,
    sample_shift INTEGER,
    codec VARCHAR,
    bitrate INTEGER,
    opus_application VARCHAR,
    encryption VARCHAR,
    chunk_frames INTEGER,
    firmware_version VARCHAR,
    time_reliable BOOLEAN,
    resumed BOOLEAN,
    resume_count INTEGER,
    recovered BOOLEAN,
    muted_ms BIGINT,
    muted_truncated BOOLEAN,
    buffer_overflows INTEGER,
    sd_write_errors INTEGER,
    upload_retries INTEGER,
    wifi_disconnects INTEGER,
    ingested_unix BIGINT NOT NULL
);

CREATE TABLE IF NOT EXISTS resumes (
    session_id VARCHAR NOT NULL,
    resume INTEGER NOT NULL,
    segment INTEGER NOT NULL,
    anchor_unix BIGINT NOT NULL,
    PRIMARY KEY (session_id, resume)
);

CREATE TABLE IF NOT EXISTS mutes (
    session_id VARCHAR NOT NULL,
    mute INTEGER NOT NULL,
    segment INTEGER NOT NULL,
    start_ms INTEGER NOT NULL,
    ms INTEGER NOT NULL,
    PRIMARY KEY (session_id, mute)
);

CREATE TABLE IF NOT EXISTS segments (
    session_id VARCHAR NOT NULL,
    segment INTEGER NOT NULL,
    frames INTEGER NOT NULL,
    chunks INTEGER NOT NULL,
    bytes BIGINT NOT NULL,
    torn BOOLEAN NOT NULL,
    corrupt BOOLEAN NOT NULL,
    PRIMARY KEY (session_id, segment)
);

CREATE TABLE IF NOT EXISTS windows (
    session_id VARCHAR NOT NULL,
    source VARCHAR NOT NULL,
    segment INTEGER NOT NULL,
    first_frame INTEGER NOT NULL,
    class VARCHAR NOT NULL,
    score REAL NOT NULL
);

CREATE TABLE IF NOT EXISTS teacher (
    session_id VARCHAR NOT NULL,
    segment INTEGER NOT NULL,
    first_frame INTEGER NOT NULL,
    rank INTEGER NOT NULL,
    yamnet_class VARCHAR NOT NULL,
    score REAL NOT NULL
);

CREATE TABLE IF NOT EXISTS levels (
    session_id VARCHAR NOT NULL,
    segment INTEGER NOT NULL,
    first_frame INTEGER NOT NULL,
    rms_dbfs REAL NOT NULL,
    peak_dbfs REAL NOT NULL
);

CREATE TABLE IF NOT EXISTS scored (
    session_id VARCHAR NOT NULL,
    source VARCHAR NOT NULL,
    windows INTEGER NOT NULL,
    muted_windows INTEGER NOT NULL,
    scored_unix BIGINT NOT NULL,
    PRIMARY KEY (session_id, source)
);

CREATE TABLE IF NOT EXISTS events (
    session_id VARCHAR NOT NULL,
    source VARCHAR NOT NULL,
    class VARCHAR NOT NULL,
    start_segment INTEGER NOT NULL,
    start_frame INTEGER NOT NULL,
    end_segment INTEGER NOT NULL,
    end_frame INTEGER NOT NULL,
    start_unix DOUBLE,
    end_unix DOUBLE,
    duration_ms INTEGER NOT NULL,
    windows INTEGER NOT NULL,
    peak_score REAL NOT NULL,
    mean_score REAL NOT NULL
);

CREATE TABLE IF NOT EXISTS evented (
    session_id VARCHAR NOT NULL,
    source VARCHAR NOT NULL,
    rules_sha256 VARCHAR NOT NULL,
    scored_unix BIGINT NOT NULL,
    events INTEGER NOT NULL,
    derived_unix BIGINT NOT NULL,
    PRIMARY KEY (session_id, source)
);

CREATE TABLE IF NOT EXISTS nights (
    session_id VARCHAR NOT NULL,
    source VARCHAR NOT NULL,
    bedtime_unix DOUBLE NOT NULL,
    up_unix DOUBLE NOT NULL,
    time_in_bed_min DOUBLE NOT NULL,
    onset_unix DOUBLE,
    onset_latency_min DOUBLE,
    wake_unix DOUBLE NOT NULL,
    lie_in_min DOUBLE NOT NULL,
    sleep_min DOUBLE,
    efficiency DOUBLE,
    midpoint_unix DOUBLE,
    restless_bouts INTEGER,
    restless_min DOUBLE,
    snoring_min DOUBLE NOT NULL,
    snoring_share DOUBLE,
    snoring_per_hour DOUBLE NOT NULL,
    snoring_peak_db DOUBLE,
    snoring_worst_hour_unix BIGINT,
    breathing_pauses INTEGER,
    breathing_pauses_per_hour DOUBLE,
    coughs INTEGER NOT NULL,
    sneezes INTEGER NOT NULL,
    sleep_talk INTEGER,
    bathroom_trips INTEGER,
    alarms INTEGER NOT NULL,
    first_alarm_unix DOUBLE,
    last_alarm_unix DOUBLE,
    snooze_span_min DOUBLE,
    alarm_to_up_min DOUBLE,
    noise_floor_db DOUBLE,
    loud_moments INTEGER NOT NULL,
    outage_min DOUBLE NOT NULL,
    muted_min DOUBLE NOT NULL,
    missing_min DOUBLE NOT NULL,
    rules_sha256 VARCHAR NOT NULL,
    events_derived_unix BIGINT NOT NULL,
    derived_unix BIGINT NOT NULL,
    PRIMARY KEY (session_id, source)
);

CREATE TABLE IF NOT EXISTS loud_moments (
    session_id VARCHAR NOT NULL,
    segment INTEGER NOT NULL,
    first_frame INTEGER NOT NULL,
    moment_unix DOUBLE,
    peak_db REAL NOT NULL,
    rms_db REAL NOT NULL,
    top_class VARCHAR
);

CREATE TABLE IF NOT EXISTS night_context (
    session_id VARCHAR PRIMARY KEY,
    night_of DATE NOT NULL,
    weekday TINYINT NOT NULL,
    free_day_after BOOLEAN NOT NULL,
    timezone VARCHAR NOT NULL,
    utc_offset_min SMALLINT NOT NULL,
    clock_shift_min SMALLINT,
    days_since_clock_change TINYINT,
    sunset_unix DOUBLE,
    sunrise_unix DOUBLE
);

CREATE TABLE IF NOT EXISTS weather (
    hour_unix BIGINT PRIMARY KEY,
    temperature_c REAL,
    humidity_pct REAL,
    pressure_hpa REAL,
    precipitation_mm REAL,
    fetched_unix BIGINT NOT NULL
);

CREATE TABLE IF NOT EXISTS trend_settings (
    sleep_target_h DOUBLE NOT NULL,
    illness_factor DOUBLE NOT NULL,
    illness_min_count INTEGER NOT NULL,
    illness_min_baseline_nights INTEGER NOT NULL,
    predictor_min_nights INTEGER NOT NULL
);

CREATE OR REPLACE MACRO night_clock_min(unix, zone, night_of) AS
    (epoch(timezone(zone, to_timestamp(unix))) - epoch(CAST(night_of AS TIMESTAMP))) / 60 - 720;

CREATE OR REPLACE VIEW night_weather AS
SELECT night_context.session_id,
       COUNT(weather.hour_unix) AS weather_hours,
       AVG(weather.temperature_c) AS temperature_c,
       MIN(weather.temperature_c) AS temperature_min_c,
       MAX(weather.temperature_c) AS temperature_max_c,
       AVG(weather.humidity_pct) AS humidity_pct,
       AVG(weather.pressure_hpa) AS pressure_hpa,
       ARG_MAX(weather.pressure_hpa, weather.hour_unix) - ARG_MIN(weather.pressure_hpa, weather.hour_unix)
           AS pressure_change_hpa,
       SUM(weather.precipitation_mm) FILTER (WHERE weather.hour_unix > sessions.started_unix // 3600 * 3600)
           AS precipitation_mm
FROM night_context
JOIN sessions ON sessions.session_id = night_context.session_id
LEFT JOIN weather
       ON weather.hour_unix >= sessions.started_unix // 3600 * 3600
      AND weather.hour_unix <= (sessions.ended_unix + 3599) // 3600 * 3600
GROUP BY night_context.session_id;

CREATE OR REPLACE VIEW runs AS
SELECT session_id, 0 AS run, 0 AS first_segment, started_unix AS anchor_unix
FROM sessions
UNION ALL
SELECT session_id, resume AS run, segment AS first_segment, anchor_unix
FROM resumes;

CREATE OR REPLACE VIEW outages AS
WITH ordered AS (
    SELECT session_id, run, first_segment, anchor_unix,
           LAG(first_segment) OVER run_order AS previous_segment,
           LAG(anchor_unix) OVER run_order AS previous_anchor_unix
    FROM runs
    WINDOW run_order AS (PARTITION BY session_id ORDER BY first_segment, run)
),
previous_run AS (
    SELECT ordered.session_id, ordered.run,
           COUNT(segments.segment) AS num_segments,
           COALESCE(SUM(segments.frames), 0) AS num_frames
    FROM ordered
    LEFT JOIN segments
           ON segments.session_id = ordered.session_id
          AND segments.segment >= ordered.previous_segment
          AND segments.segment < ordered.first_segment
    WHERE ordered.previous_segment IS NOT NULL
    GROUP BY ordered.session_id, ordered.run
)
SELECT ordered.session_id,
       ordered.run,
       ordered.first_segment AS segment,
       CASE WHEN previous_run.num_segments = ordered.first_segment - ordered.previous_segment
            THEN ordered.previous_anchor_unix + previous_run.num_frames * CAST(0.02 AS DOUBLE)
       END AS start_unix,
       ordered.anchor_unix AS end_unix
FROM ordered
JOIN previous_run
  ON previous_run.session_id = ordered.session_id
 AND previous_run.run = ordered.run;

CREATE OR REPLACE VIEW trend_nights AS
SELECT nights.source,
       night_context.night_of,
       nights.session_id,
       COUNT(*) OVER (PARTITION BY nights.source, night_context.night_of) AS sessions,
       night_context.weekday,
       night_context.free_day_after,
       night_context.timezone,
       night_context.utc_offset_min,
       night_context.clock_shift_min,
       night_context.days_since_clock_change,
       night_clock_min(night_context.sunset_unix, night_context.timezone, night_context.night_of) AS sunset_clock_min,
       night_clock_min(nights.bedtime_unix, night_context.timezone, night_context.night_of) AS bedtime_clock_min,
       night_clock_min(nights.onset_unix, night_context.timezone, night_context.night_of) AS onset_clock_min,
       night_clock_min(nights.midpoint_unix, night_context.timezone, night_context.night_of) AS midpoint_clock_min,
       night_clock_min(nights.first_alarm_unix, night_context.timezone, night_context.night_of) AS first_alarm_clock_min,
       night_clock_min(nights.wake_unix, night_context.timezone, night_context.night_of) AS wake_clock_min,
       night_clock_min(nights.up_unix, night_context.timezone, night_context.night_of) AS up_clock_min,
       night_clock_min(night_context.sunrise_unix, night_context.timezone, night_context.night_of) AS sunrise_clock_min,
       (nights.bedtime_unix - night_context.sunset_unix) / 60 AS bedtime_after_sunset_min,
       (nights.wake_unix - night_context.sunrise_unix) / 60 AS wake_after_sunrise_min,
       nights.time_in_bed_min,
       nights.onset_latency_min,
       nights.sleep_min,
       nights.efficiency,
       nights.lie_in_min,
       nights.restless_bouts,
       nights.restless_min,
       nights.snoring_min,
       nights.snoring_share,
       nights.snoring_per_hour,
       nights.snoring_peak_db,
       nights.breathing_pauses_per_hour,
       nights.coughs,
       nights.sneezes,
       nights.sleep_talk,
       nights.bathroom_trips,
       nights.alarms,
       nights.snooze_span_min,
       nights.alarm_to_up_min,
       nights.noise_floor_db,
       nights.loud_moments,
       night_weather.temperature_c,
       night_weather.humidity_pct,
       night_weather.pressure_hpa,
       night_weather.pressure_change_hpa,
       night_weather.precipitation_mm
FROM nights
JOIN night_context ON night_context.session_id = nights.session_id
LEFT JOIN night_weather ON night_weather.session_id = nights.session_id
QUALIFY ROW_NUMBER() OVER (PARTITION BY nights.source, night_context.night_of
                           ORDER BY nights.time_in_bed_min DESC, nights.session_id) = 1;

CREATE OR REPLACE VIEW trend_values AS
UNPIVOT (
    SELECT source, night_of, session_id, weekday, free_day_after,
           CAST(bedtime_clock_min AS DOUBLE) AS bedtime_clock_min,
           CAST(onset_clock_min AS DOUBLE) AS onset_clock_min,
           CAST(midpoint_clock_min AS DOUBLE) AS midpoint_clock_min,
           CAST(first_alarm_clock_min AS DOUBLE) AS first_alarm_clock_min,
           CAST(wake_clock_min AS DOUBLE) AS wake_clock_min,
           CAST(up_clock_min AS DOUBLE) AS up_clock_min,
           CAST(bedtime_after_sunset_min AS DOUBLE) AS bedtime_after_sunset_min,
           CAST(wake_after_sunrise_min AS DOUBLE) AS wake_after_sunrise_min,
           CAST(time_in_bed_min AS DOUBLE) AS time_in_bed_min,
           CAST(onset_latency_min AS DOUBLE) AS onset_latency_min,
           CAST(sleep_min AS DOUBLE) AS sleep_min,
           CAST(efficiency AS DOUBLE) AS efficiency,
           CAST(lie_in_min AS DOUBLE) AS lie_in_min,
           CAST(restless_bouts AS DOUBLE) AS restless_bouts,
           CAST(restless_min AS DOUBLE) AS restless_min,
           CAST(snoring_min AS DOUBLE) AS snoring_min,
           CAST(snoring_share AS DOUBLE) AS snoring_share,
           CAST(snoring_per_hour AS DOUBLE) AS snoring_per_hour,
           CAST(snoring_peak_db AS DOUBLE) AS snoring_peak_db,
           CAST(breathing_pauses_per_hour AS DOUBLE) AS breathing_pauses_per_hour,
           CAST(coughs AS DOUBLE) AS coughs,
           CAST(sneezes AS DOUBLE) AS sneezes,
           CAST(sleep_talk AS DOUBLE) AS sleep_talk,
           CAST(bathroom_trips AS DOUBLE) AS bathroom_trips,
           CAST(alarms AS DOUBLE) AS alarms,
           CAST(snooze_span_min AS DOUBLE) AS snooze_span_min,
           CAST(alarm_to_up_min AS DOUBLE) AS alarm_to_up_min,
           CAST(noise_floor_db AS DOUBLE) AS noise_floor_db,
           CAST(loud_moments AS DOUBLE) AS loud_moments,
           CAST(temperature_c AS DOUBLE) AS temperature_c,
           CAST(humidity_pct AS DOUBLE) AS humidity_pct,
           CAST(pressure_hpa AS DOUBLE) AS pressure_hpa,
           CAST(pressure_change_hpa AS DOUBLE) AS pressure_change_hpa,
           CAST(precipitation_mm AS DOUBLE) AS precipitation_mm
    FROM trend_nights
)
ON COLUMNS(* EXCLUDE (source, night_of, session_id, weekday, free_day_after))
INTO NAME metric VALUE value;

CREATE OR REPLACE VIEW trend_rolling AS
SELECT source, metric, night_of, value,
       AVG(value) OVER week AS mean_7,
       COUNT(value) OVER week AS nights_7
FROM trend_values
WINDOW week AS (PARTITION BY source, metric ORDER BY night_of
                RANGE BETWEEN INTERVAL 6 DAYS PRECEDING AND CURRENT ROW);

CREATE OR REPLACE VIEW trend_weekday AS
SELECT source, metric, weekday, COUNT(*) AS nights, AVG(value) AS mean
FROM trend_values
GROUP BY source, metric, weekday;

CREATE OR REPLACE VIEW trend_weekend AS
SELECT source, metric,
       COUNT(*) FILTER (WHERE NOT free_day_after) AS work_nights,
       COUNT(*) FILTER (WHERE free_day_after) AS free_nights,
       AVG(value) FILTER (WHERE NOT free_day_after) AS work_mean,
       AVG(value) FILTER (WHERE free_day_after) AS free_mean,
       AVG(value) FILTER (WHERE free_day_after) - AVG(value) FILTER (WHERE NOT free_day_after) AS free_minus_work
FROM trend_values
GROUP BY source, metric;

CREATE OR REPLACE VIEW trend_regularity AS
WITH asleep AS (
    SELECT source, night_of,
           GREATEST(0, LEAST(1440, onset_clock_min)) AS asleep_from,
           GREATEST(0, LEAST(1440, wake_clock_min)) AS asleep_to
    FROM trend_nights
    WHERE onset_clock_min IS NOT NULL
),
pairs AS (
    SELECT later.source, later.night_of,
           (earlier.asleep_to - earlier.asleep_from) + (later.asleep_to - later.asleep_from)
               - 2 * GREATEST(0, LEAST(earlier.asleep_to, later.asleep_to)
                                 - GREATEST(earlier.asleep_from, later.asleep_from)) AS mismatch_min
    FROM asleep AS later
    JOIN asleep AS earlier ON earlier.source = later.source AND earlier.night_of = later.night_of - 1
)
SELECT source, night_of, mismatch_min, 100 - 200 * mismatch_min / 1440 AS sri
FROM pairs;

CREATE OR REPLACE VIEW trend_consistency AS
SELECT trend_nights.source, trend_nights.night_of,
       COUNT(*) OVER week AS nights_7,
       STDDEV_SAMP(trend_nights.bedtime_clock_min) OVER week AS bedtime_spread_min,
       STDDEV_SAMP(trend_nights.onset_clock_min) OVER week AS onset_spread_min,
       STDDEV_SAMP(trend_nights.wake_clock_min) OVER week AS wake_spread_min,
       STDDEV_SAMP(trend_nights.midpoint_clock_min) OVER week AS midpoint_spread_min,
       trend_regularity.sri,
       AVG(trend_regularity.sri) OVER week AS sri_7
FROM trend_nights
LEFT JOIN trend_regularity
       ON trend_regularity.source = trend_nights.source AND trend_regularity.night_of = trend_nights.night_of
WINDOW week AS (PARTITION BY trend_nights.source ORDER BY trend_nights.night_of
                RANGE BETWEEN INTERVAL 6 DAYS PRECEDING AND CURRENT ROW);

CREATE OR REPLACE VIEW trend_sleep_debt AS
SELECT trend_nights.source, trend_nights.night_of, trend_nights.sleep_min,
       trend_settings.sleep_target_h * 60 AS target_min,
       trend_settings.sleep_target_h * 60 - trend_nights.sleep_min AS shortfall_min,
       SUM(trend_settings.sleep_target_h * 60 - trend_nights.sleep_min) OVER fortnight AS debt_14_min,
       COUNT(*) OVER fortnight AS nights_14
FROM trend_nights
CROSS JOIN trend_settings
WHERE trend_nights.sleep_min IS NOT NULL
WINDOW fortnight AS (PARTITION BY trend_nights.source ORDER BY trend_nights.night_of
                     RANGE BETWEEN INTERVAL 13 DAYS PRECEDING AND CURRENT ROW);

CREATE OR REPLACE VIEW trend_illness AS
WITH baseline AS (
    SELECT source, night_of, session_id, coughs, sneezes,
           COUNT(*) OVER before AS baseline_nights,
           AVG(coughs) OVER before AS cough_baseline,
           AVG(sneezes) OVER before AS sneeze_baseline
    FROM trend_nights
    WINDOW before AS (PARTITION BY source ORDER BY night_of
                      RANGE BETWEEN INTERVAL 28 DAYS PRECEDING AND INTERVAL 1 DAY PRECEDING)
),
flagged AS (
    SELECT baseline.*,
           baseline_nights >= illness_min_baseline_nights
               AND coughs >= GREATEST(illness_min_count, illness_factor * cough_baseline) AS cough_flag,
           baseline_nights >= illness_min_baseline_nights
               AND sneezes >= GREATEST(illness_min_count, illness_factor * sneeze_baseline) AS sneeze_flag
    FROM baseline
    CROSS JOIN trend_settings
)
SELECT *, cough_flag OR sneeze_flag AS illness_flag
FROM flagged;

CREATE OR REPLACE VIEW trend_predictors AS
WITH outcomes AS (
    SELECT source, night_of, metric AS outcome, value AS outcome_value
    FROM trend_values
    WHERE metric IN ('efficiency', 'restless_min', 'snoring_per_hour')
),
predictors AS (
    SELECT source, night_of, metric AS predictor, value AS predictor_value
    FROM trend_values
    WHERE metric IN ('bedtime_clock_min', 'first_alarm_clock_min', 'noise_floor_db', 'temperature_c',
                     'humidity_pct', 'pressure_hpa', 'pressure_change_hpa', 'precipitation_mm')
    UNION ALL
    SELECT source, night_of, 'free_day_after', CAST(free_day_after AS DOUBLE)
    FROM trend_nights
),
pairs AS (
    SELECT outcomes.source, outcomes.outcome, predictors.predictor,
           COUNT(*) AS nights,
           CORR(outcomes.outcome_value, predictors.predictor_value) AS correlation
    FROM outcomes
    JOIN predictors ON predictors.source = outcomes.source AND predictors.night_of = outcomes.night_of
    GROUP BY outcomes.source, outcomes.outcome, predictors.predictor
)
SELECT pairs.source, pairs.outcome, pairs.predictor, pairs.nights, pairs.correlation,
       RANK() OVER (PARTITION BY pairs.source, pairs.outcome ORDER BY ABS(pairs.correlation) DESC) AS rank
FROM pairs
CROSS JOIN trend_settings
WHERE pairs.nights >= trend_settings.predictor_min_nights
  AND pairs.correlation IS NOT NULL
  AND NOT isnan(pairs.correlation);

CREATE OR REPLACE VIEW trend_snoring AS
SELECT trend_nights.source, trend_nights.night_of, month(trend_nights.night_of) AS month,
       trend_nights.snoring_min, trend_nights.snoring_share, trend_nights.snoring_per_hour,
       trend_nights.snoring_peak_db,
       AVG(trend_nights.snoring_per_hour) OVER (PARTITION BY trend_nights.source ORDER BY trend_nights.night_of
                                                RANGE BETWEEN INTERVAL 27 DAYS PRECEDING AND CURRENT ROW)
           AS per_hour_mean_28,
       trend_nights.temperature_c, trend_nights.humidity_pct, trend_nights.pressure_hpa,
       trend_nights.precipitation_mm,
       trend_illness.illness_flag
FROM trend_nights
LEFT JOIN trend_illness
       ON trend_illness.source = trend_nights.source AND trend_illness.night_of = trend_nights.night_of;

CREATE OR REPLACE VIEW trend_daylight AS
SELECT source, night_of,
       sunset_clock_min, bedtime_clock_min, bedtime_after_sunset_min,
       sunrise_clock_min, wake_clock_min, wake_after_sunrise_min,
       utc_offset_min, clock_shift_min, days_since_clock_change,
       COALESCE(days_since_clock_change < 7, false) AS week_after_clock_change
FROM trend_nights;

CREATE OR REPLACE VIEW trend_records AS
WITH records (record, metric, highest) AS (
    VALUES ('earliest bedtime', 'bedtime_clock_min', false),
           ('latest bedtime', 'bedtime_clock_min', true),
           ('earliest wake', 'wake_clock_min', false),
           ('latest wake', 'wake_clock_min', true),
           ('longest sleep', 'sleep_min', true),
           ('shortest sleep', 'sleep_min', false),
           ('best efficiency', 'efficiency', true),
           ('worst efficiency', 'efficiency', false),
           ('quickest onset', 'onset_latency_min', false),
           ('longest onset', 'onset_latency_min', true),
           ('most restless', 'restless_min', true),
           ('most snoring', 'snoring_min', true),
           ('loudest snore', 'snoring_peak_db', true),
           ('most coughs', 'coughs', true),
           ('most sneezes', 'sneezes', true),
           ('most sleep talk', 'sleep_talk', true),
           ('most alarms', 'alarms', true),
           ('longest snooze', 'snooze_span_min', true),
           ('longest lie-in', 'lie_in_min', true),
           ('noisiest room', 'noise_floor_db', true),
           ('quietest room', 'noise_floor_db', false),
           ('coldest night', 'temperature_c', false),
           ('warmest night', 'temperature_c', true)
),
ranked AS (
    SELECT trend_values.source, records.record, records.metric, trend_values.value,
           trend_values.night_of, trend_values.session_id,
           ROW_NUMBER() OVER (PARTITION BY trend_values.source, records.record
                              ORDER BY CASE WHEN records.highest THEN -trend_values.value ELSE trend_values.value END,
                                       trend_values.night_of) AS place
    FROM records
    JOIN trend_values ON trend_values.metric = records.metric
)
SELECT source, record, metric, value, night_of, session_id
FROM ranked
WHERE place = 1;

CREATE OR REPLACE VIEW trend_heatmap AS
WITH spans AS (
    SELECT trend_nights.source, trend_nights.night_of, events.class,
           night_clock_min(events.start_unix, trend_nights.timezone, trend_nights.night_of) AS start_min,
           night_clock_min(events.end_unix, trend_nights.timezone, trend_nights.night_of) AS end_min
    FROM trend_nights
    JOIN events ON events.session_id = trend_nights.session_id AND events.source = trend_nights.source
    WHERE events.start_unix IS NOT NULL AND events.end_unix IS NOT NULL
),
hours AS (
    SELECT source, night_of, class, start_min, end_min,
           UNNEST(range(CAST(floor(start_min / 60) AS BIGINT), CAST(ceil(end_min / 60) AS BIGINT))) AS hour
    FROM spans
)
SELECT source, night_of, hour, (hour + 12) % 24 AS clock_hour, class,
       SUM(LEAST(end_min, (hour + 1) * 60) - GREATEST(start_min, hour * 60)) AS minutes
FROM hours
GROUP BY source, night_of, hour, class;

CREATE OR REPLACE VIEW trend_years AS
SELECT trend_nights.source, year(trend_nights.night_of) AS year,
       COUNT(*) AS nights,
       MIN(trend_nights.night_of) AS first_night,
       MAX(trend_nights.night_of) AS last_night,
       AVG(trend_nights.sleep_min) AS sleep_min,
       SUM(trend_nights.sleep_min) / 60 AS sleep_h,
       AVG(trend_nights.efficiency) AS efficiency,
       AVG(trend_nights.onset_latency_min) AS onset_latency_min,
       AVG(trend_nights.bedtime_clock_min) AS bedtime_clock_min,
       AVG(trend_nights.wake_clock_min) AS wake_clock_min,
       SUM(trend_nights.restless_bouts) AS restless_bouts,
       SUM(trend_nights.snoring_min) / 60 AS snoring_h,
       SUM(trend_nights.coughs) AS coughs,
       SUM(trend_nights.sneezes) AS sneezes,
       SUM(trend_nights.sleep_talk) AS sleep_talk,
       SUM(trend_nights.bathroom_trips) AS bathroom_trips,
       SUM(trend_nights.alarms) AS alarms,
       AVG(trend_nights.noise_floor_db) AS noise_floor_db,
       COUNT(*) FILTER (WHERE trend_illness.illness_flag) AS illness_nights
FROM trend_nights
LEFT JOIN trend_illness
       ON trend_illness.source = trend_nights.source AND trend_illness.night_of = trend_nights.night_of
GROUP BY trend_nights.source, year(trend_nights.night_of);
"""

def default_path():
    return data_root() / DATABASE_NAME

def connect(path=None):
    if path is None:
        path = default_path()

    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)

    connection = duckdb.connect(str(path))
    connection.execute(SCHEMA)
    return connection
