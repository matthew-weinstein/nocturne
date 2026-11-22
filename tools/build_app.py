"""Build the Nocturne desktop app.

    cd tools
    python build_app.py

Builds the front end (npm ci, npm run build in dashboard/), then packages
nocturne_pc.app with PyInstaller into dist/Nocturne/, Nocturne.exe on
Windows, with the icon from make_app_icon.py. PyInstaller runs in its own environment (build/venv, made from
requirements-app.txt on the first build), so nothing else installed in
this Python is bundled. The app reads the same data root and passphrase as
the pipeline. On Windows it also adds a Start menu shortcut, so the app
shows up in search and can be pinned.
"""

import hashlib
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

def tag_shortcut(shortcut):
    """Gives the shortcut the app's taskbar ID, so a pinned or launched Nocturne groups with its window."""
    import pythoncom
    from win32com.propsys import propsys, pscon
    from win32com.shell import shellcon

    from nocturne_pc.app import APP_ID

    store = propsys.SHGetPropertyStoreFromParsingName(str(shortcut), None, shellcon.GPS_READWRITE,
                                                      propsys.IID_IPropertyStore)
    store.SetValue(pscon.PKEY_AppUserModel_ID, propsys.PROPVARIANTType(APP_ID, pythoncom.VT_LPWSTR))
    store.Commit()

def versioned_icon(app_dir):
    """A copy of the icon named after its contents: Windows caches a shortcut's icon by path."""
    source = TOOLS / "nocturne_pc" / "app_icon.ico"
    digest = hashlib.sha256(source.read_bytes()).hexdigest()[:10]
    for old in app_dir.glob("icon-*.ico"):
        old.unlink()
    copy = app_dir / f"icon-{digest}.ico"
    shutil.copyfile(source, copy)
    return copy

def add_start_menu_shortcut(target):
    programs = Path(os.environ["APPDATA"]) / "Microsoft" / "Windows" / "Start Menu" / "Programs"
    shortcut = programs / f"{NAME}.lnk"
    script = (
        "$shell = New-Object -ComObject WScript.Shell; "
        f"$link = $shell.CreateShortcut('{shortcut}'); "
        f"$link.TargetPath = '{target}'; "
        f"$link.WorkingDirectory = '{target.parent}'; "
        f"$link.IconLocation = '{versioned_icon(target.parent)},0'; "
        "$link.Description = 'Nocturne sleep recordings'; "
        "$link.Save()"
    )
    subprocess.run(["powershell", "-NoProfile", "-Command", script], check=True)
    tag_shortcut(shortcut)
    print(f"added {shortcut}")

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
        "--icon", str(TOOLS / "nocturne_pc" / "app_icon.ico"),
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
    if os.name == "nt":
        add_start_menu_shortcut(TOOLS / "dist" / NAME / f"{NAME}.exe")

if __name__ == "__main__":
    main()
