"""The pipeline's DuckDB database, nocturne.duckdb at the data root (design doc §22)."""

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
    utc_offset_min SMALLINT NOT NULL,
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
