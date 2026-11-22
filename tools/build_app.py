"""Build the Nocturne desktop app.

    cd tools
    python build_app.py

Builds the front end (npm ci, npm run build in dashboard/), then packages
nocturne_pc.app with PyInstaller into dist/Nocturne/, Nocturne.exe on
Windows. PyInstaller runs in its own environment (build/venv, made from
requirements-app.txt on the first build), so nothing else installed in
this Python is bundled. The app reads the same data root and passphrase as
the pipeline.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parent
DASHBOARD = TOOLS / "dashboard"
VENV = TOOLS / "build" / "venv"
NAME = "Nocturne"

def venv_python():
    if os.name == "nt":
        return VENV / "Scripts" / "python.exe"
    return VENV / "bin" / "python"

def in_build_venv():
    return Path(sys.prefix).resolve() == VENV.resolve()

def run_in_build_venv():
    python = venv_python()
    if not python.exists():
        subprocess.run([sys.executable, "-m", "venv", str(VENV)], check=True)
        subprocess.run([str(python), "-m", "pip", "install", "--quiet", "--upgrade", "pip"], check=True)
    subprocess.run([str(python), "-m", "pip", "install", "--quiet", "-r", str(TOOLS / "requirements-app.txt")],
                   check=True)
    result = subprocess.run([str(python), str(Path(__file__).resolve())])
    sys.exit(result.returncode)

def npm(*arguments):
    executable = shutil.which("npm")
    if executable is None:
        sys.exit("npm is needed to build the front end (Node 22)")
    subprocess.run([executable, *arguments], cwd=DASHBOARD, check=True)

def main():
    if not in_build_venv():
        run_in_build_venv()

    import PyInstaller.__main__

    npm("ci")
    npm("run", "build")

    front_end = f"{DASHBOARD / 'dist'}{os.pathsep}dashboard/dist"
    PyInstaller.__main__.run([
        str(TOOLS / "nocturne_pc" / "app.py"),
        "--name", NAME,
        "--windowed",
        "--noconfirm",
        "--clean",
        "--paths", str(TOOLS),
        "--add-data", front_end,
        "--collect-data", "nocturne_pc",
        "--distpath", str(TOOLS / "dist"),
        "--workpath", str(TOOLS / "build"),
        "--specpath", str(TOOLS / "build"),
    ])
    print(f"built {TOOLS / 'dist' / NAME}")

if __name__ == "__main__":
    main()
