#!/usr/bin/env python3
"""Build and run every host test project on the linux target (design doc 20).

Each components/<name>/host_test/ is a standalone IDF project whose
sdkconfig.defaults selects the linux target. This builds each one with idf.py,
runs the resulting executable, and exits non-zero if any build or any Unity
test fails. Needs a Linux or macOS host with ESP-IDF exported:

    python tools/run_host_tests.py
    python tools/run_host_tests.py crypto storage
"""

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RUN_TIMEOUT_S = 300

def find_projects(names):
    projects = sorted(path.parent for path in ROOT.glob("components/*/host_test/CMakeLists.txt"))

    if names:
        projects = [project for project in projects if project.parts[-2] in names]
    return projects

def build(project):
    command = ["idf.py", "-C", str(project), "-B", str(project / "build"), "build"]
    return subprocess.run(command).returncode == 0

def run(project):
    executables = list((project / "build").glob("*.elf"))
    if len(executables) != 1:
        print(f"expected one .elf in {project / 'build'}, found {len(executables)}")
        return False

    try:
        return subprocess.run([str(executables[0])], timeout=RUN_TIMEOUT_S).returncode == 0
    except subprocess.TimeoutExpired:
        print(f"{executables[0].name} did not finish within {RUN_TIMEOUT_S} s")
        return False

def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("components", nargs="*", help="only run host tests under these components")
    args = parser.parse_args()

    projects = find_projects(args.components)
    if not projects:
        print("no host test projects found")
        return 1

    results = {}
    for project in projects:
        name = project.relative_to(ROOT).as_posix()
        print(f"==== {name}", flush=True)
        results[name] = build(project) and run(project)

    print("\n==== summary")
    for name, passed in results.items():
        print(f"  {'PASS' if passed else 'FAIL'}  {name}")

    return 0 if all(results.values()) else 1

if __name__ == "__main__":
    sys.exit(main())
