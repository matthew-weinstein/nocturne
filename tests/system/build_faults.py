#!/usr/bin/env python3
"""Build the fault-injection firmware for the failure-matrix system tests (design doc 15, 20).

Each tests/system/faults/sdkconfig.<fault> is layered on the system-test
overlay and built into build-fault-<fault>/ with its own sdkconfig, so the
default and system-test builds are untouched. Run from the ESP-IDF shell:

    python tests/system/build_faults.py
    python tests/system/build_faults.py sd_write_error mic_silent
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FAULTS_DIR = Path(__file__).resolve().parent / "faults"
OVERLAY_PREFIX = "sdkconfig."

def find_faults(names):
    faults = []
    for overlay in sorted(FAULTS_DIR.glob(OVERLAY_PREFIX + "*")):
        fault = overlay.name[len(OVERLAY_PREFIX):]
        if not names or fault in names:
            faults.append(fault)
    return faults

def idf_command():
    idf_path = os.environ.get("IDF_PATH")
    python_env = os.environ.get("IDF_PYTHON_ENV_PATH")
    if not idf_path or not python_env:
        return None

    if sys.platform == "win32":
        python = Path(python_env) / "Scripts" / "python.exe"
    else:
        python = Path(python_env) / "bin" / "python"
    return [str(python), str(Path(idf_path) / "tools" / "idf.py")]

def build(idf, fault):
    build_dir = f"build-fault-{fault}"
    overlay = (FAULTS_DIR / (OVERLAY_PREFIX + fault)).relative_to(ROOT).as_posix()
    defaults = f"sdkconfig.defaults;sdkconfig.ci.system_test;{overlay}"
    command = idf + [
        "-B", build_dir,
        "-D", f"SDKCONFIG={build_dir}/sdkconfig",
        "-D", f"SDKCONFIG_DEFAULTS={defaults}",
        "build",
    ]
    return subprocess.run(command, cwd=ROOT).returncode == 0

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("faults", nargs="*", help="only build these faults")
    args = parser.parse_args()

    faults = find_faults(args.faults)
    if not faults:
        print("no fault overlays found")
        return 1

    idf = idf_command()
    if idf is None:
        print("IDF_PATH or IDF_PYTHON_ENV_PATH is not set; run this from the ESP-IDF shell")
        return 1

    results = {}
    for fault in faults:
        print(f"==== {fault}", flush=True)
        results[fault] = build(idf, fault)

    print("\n==== summary")
    failed = False
    for fault, ok in results.items():
        if ok:
            print(f"ok      build-fault-{fault}")
        else:
            print(f"FAILED  build-fault-{fault}")
            failed = True

    if failed:
        return 1
    return 0

if __name__ == "__main__":
    sys.exit(main())
