"""Pull finished sessions from B2 into audio/, still encrypted (design doc 9.4).

    cd tools
    python -m nocturne_pc.sync
    python -m nocturne_pc.sync --dry-run

A session is pulled once its session.json is in B2, oldest first, through the
read-only remote that b2_remote.py set up. Each lands in
audio/.partial/<session_id>/ and is moved to audio/<session_id>/ once rclone
has copied and checked every file, so a run that stops part way resumes the
next time. Every pulled session is recorded in synced.jsonl at the data root,
which outlives pruning, so a pruned session is never downloaded again.

B2's free tier allows 1 GB of downloads a day, so a run stops before the
day's total (UTC) would pass --cap-mb, and a long catch-up spreads over
several days. A finished session that has been in B2 21 days without being
pulled is warned about: the lifecycle rule deletes it at 30.
"""

import argparse
import json
import os
import subprocess
import sys
import time
from datetime import datetime, timezone

from nocturne_pc import data_root

DEFAULT_REMOTE = "nocturne"
DEFAULT_CAP_MB = 900
WARN_AGE_DAYS = 21
DELETE_AGE_DAYS = 30
SECONDS_PER_DAY = 86400
BYTES_PER_MB = 1_000_000

COMPLETION_MARKER = "session.json"
LEDGER_NAME = "synced.jsonl"
PARTIAL_DIR = ".partial"


class BucketSession:
    def __init__(self, session_id):
        self.session_id = session_id
        self.num_bytes = 0
        self.first_upload_unix = None
        self.complete = False


def parse_mod_time(text):
    """Unix time from an rclone ModTime, which may carry nanoseconds."""
    if text.endswith("Z"):
        text = text[:-1] + "+00:00"

    date, clock = text.split("T")
    zone_start = max(clock.rfind("+"), clock.rfind("-"))
    seconds = clock[:zone_start]
    zone = clock[zone_start:]

    if "." in seconds:
        seconds = seconds.split(".")[0]

    return int(datetime.fromisoformat(f"{date}T{seconds}{zone}").timestamp())


def parse_bucket_listing(entries):
    """Sessions from `rclone lsjson --recursive --files-only`, oldest first."""
    sessions = {}

    for entry in entries:
        parts = entry["Path"].split("/")
        if len(parts) < 2:
            continue

        session_id = parts[0]
        session = sessions.get(session_id)
        if session is None:
            session = BucketSession(session_id)
            sessions[session_id] = session

        session.num_bytes += entry["Size"]

        uploaded = parse_mod_time(entry["ModTime"])
        if session.first_upload_unix is None or uploaded < session.first_upload_unix:
            session.first_upload_unix = uploaded

        if len(parts) == 2 and parts[1] == COMPLETION_MARKER:
            session.complete = True

    ordered = list(sessions.values())
    ordered.sort(key=upload_order)
    return ordered


def upload_order(session):
    return (session.first_upload_unix, session.session_id)


def utc_day(unix_time):
    return datetime.fromtimestamp(unix_time, timezone.utc).date()


def read_ledger(path):
    entries = []
    if not path.exists():
        return entries

    with open(path, encoding="utf-8") as ledger:
        for line in ledger:
            line = line.strip()
            if line:
                entries.append(json.loads(line))

    return entries


def append_ledger(path, session_id, synced_unix, num_bytes):
    entry = {"session_id": session_id, "synced_unix": synced_unix, "bytes": num_bytes}
    with open(path, "a", encoding="utf-8") as ledger:
        ledger.write(json.dumps(entry) + "\n")
        ledger.flush()
        os.fsync(ledger.fileno())


def bytes_synced_on(ledger, day):
    total = 0
    for entry in ledger:
        if utc_day(entry["synced_unix"]) == day:
            total += entry["bytes"]
    return total


def synced_session_ids(ledger, audio_dir):
    synced = set()

    for entry in ledger:
        synced.add(entry["session_id"])

    if audio_dir.exists():
        for folder in audio_dir.iterdir():
            if (folder / COMPLETION_MARKER).exists():
                synced.add(folder.name)

    return synced


def plan_downloads(sessions, synced, cap_bytes, used_bytes):
    """Finished, unsynced sessions oldest first, up to the day's cap.

    The plan stops at the first session that would pass the cap rather than
    skipping to a smaller one, so the oldest is never left behind.
    """
    planned = []
    total = used_bytes

    for session in sessions:
        if not session.complete or session.session_id in synced:
            continue
        if total + session.num_bytes > cap_bytes:
            break
        planned.append(session)
        total += session.num_bytes

    return planned


def sessions_at_risk(sessions, synced, now_unix):
    """Finished sessions not yet synced whose oldest file is 21 days or more in B2."""
    at_risk = []

    for session in sessions:
        if not session.complete or session.session_id in synced:
            continue
        if age_days(session, now_unix) >= WARN_AGE_DAYS:
            at_risk.append(session)

    return at_risk


def age_days(session, now_unix):
    return (now_unix - session.first_upload_unix) // SECONDS_PER_DAY


def run_rclone(arguments):
    try:
        return subprocess.run(["rclone"] + arguments, capture_output=True, text=True)
    except FileNotFoundError:
        sys.exit("FAIL: rclone is not on PATH; install it from https://rclone.org/downloads/")


def find_bucket(remote):
    result = run_rclone(["lsf", f"{remote}:", "--dirs-only"])
    if result.returncode != 0:
        sys.exit(f"FAIL: rclone lsf {remote}: {result.stderr.strip()}")

    buckets = []
    for line in result.stdout.splitlines():
        name = line.strip().rstrip("/")
        if name:
            buckets.append(name)

    if len(buckets) != 1:
        sys.exit(f"FAIL: remote {remote} sees {len(buckets)} buckets, not one; pass --bucket")

    return buckets[0]


def list_bucket(path):
    result = run_rclone(["lsjson", path, "--recursive", "--files-only", "--fast-list"])
    if result.returncode != 0:
        sys.exit(f"FAIL: rclone lsjson {path}: {result.stderr.strip()}")

    return parse_bucket_listing(json.loads(result.stdout))


def download_session(path, session, audio_dir):
    partial = audio_dir / PARTIAL_DIR / session.session_id
    final = audio_dir / session.session_id

    result = run_rclone(["copy", f"{path}/{session.session_id}", str(partial)])
    if result.returncode != 0:
        sys.exit(f"FAIL: rclone copy {session.session_id}: {result.stderr.strip()}")

    if not (partial / COMPLETION_MARKER).exists():
        sys.exit(f"FAIL: {session.session_id} downloaded without its {COMPLETION_MARKER}")

    os.replace(partial, final)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--remote", default=DEFAULT_REMOTE, help="the read-only rclone remote")
    parser.add_argument("--bucket", help="the device's bucket, if the remote sees more than one")
    parser.add_argument("--cap-mb", type=int, default=DEFAULT_CAP_MB, help="most to download in one UTC day")
    parser.add_argument("--dry-run", action="store_true", help="show what would be pulled, download nothing")
    args = parser.parse_args()

    root = data_root()
    audio_dir = root / "audio"
    ledger_path = root / LEDGER_NAME

    bucket = args.bucket
    if not bucket:
        bucket = find_bucket(args.remote)
    path = f"{args.remote}:{bucket}"

    sessions = list_bucket(path)
    ledger = read_ledger(ledger_path)
    synced = synced_session_ids(ledger, audio_dir)

    now = int(time.time())
    used = bytes_synced_on(ledger, utc_day(now))
    cap = args.cap_mb * BYTES_PER_MB
    planned = plan_downloads(sessions, synced, cap, used)

    waiting = 0
    for session in sessions:
        if session.complete and session.session_id not in synced:
            waiting += 1

    print(f"bucket:  {path}")
    print(f"in B2:   {len(sessions)} sessions, {waiting} finished and not synced")
    print(f"today:   {used / BYTES_PER_MB:.0f} of {args.cap_mb} MB used")

    for session in planned:
        size_mb = session.num_bytes / BYTES_PER_MB
        if args.dry_run:
            print(f"would pull {session.session_id} ({size_mb:.1f} MB)")
            continue

        root.mkdir(parents=True, exist_ok=True)
        download_session(path, session, audio_dir)
        append_ledger(ledger_path, session.session_id, int(time.time()), session.num_bytes)
        synced.add(session.session_id)
        print(f"pulled  {session.session_id} ({size_mb:.1f} MB)")

    left = waiting - len(planned)
    if left > 0:
        print(f"left:    {left} for a later day (cap)")

    for session in sessions_at_risk(sessions, synced, now):
        days = age_days(session, now)
        print(f"WARNING: {session.session_id} has been in B2 {days} days and is not synced; "
              f"the lifecycle rule deletes it at {DELETE_AGE_DAYS}")


if __name__ == "__main__":
    main()
