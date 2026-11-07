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
