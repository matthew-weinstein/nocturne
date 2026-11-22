"""Nocturne as a desktop app: the dashboard in its own window.

    cd tools
    python -m nocturne_pc.app
    python build_app.py              # dist/Nocturne/Nocturne.exe

The server runs inside the app on a free port of 127.0.0.1 and stops with the
window. The window is the system web view (WebView2 on Windows, WebKit on
macOS). The app never asks for the passphrase: it reads NOCTURNE_PASSPHRASE
or the credential store (python -m nocturne_pc.passphrase), and without one
every page works but sounds don't play.
"""

import os
import sys
from pathlib import Path

import webview

from nocturne_pc.dashboard import AudioCache, HOST, default_database, serve_in_background
from nocturne_pc.passphrase import PASSPHRASE_ENV, stored_passphrase

TITLE = "Nocturne"
BACKGROUND = "#06070B"
WINDOW_SIZE = (1440, 900)
SCREEN_SHARE = 0.92
MIN_SIZE = (360, 560)
ICON = Path(__file__).resolve().parent / "app_icon.ico"
APP_ID = "Nocturne.Dashboard"

def saved_passphrase():
    passphrase = os.environ.get(PASSPHRASE_ENV)
    if passphrase:
        return passphrase
    return stored_passphrase()

def window_size():
    """The design size, shrunk to fit the primary screen with room for the taskbar."""
    width, height = WINDOW_SIZE
    if webview.screens:
        screen = webview.screens[0]
        width = min(width, int(screen.width * SCREEN_SHARE))
        height = min(height, int(screen.height * SCREEN_SHARE))
    return width, height

def claim_taskbar_identity():
    """Groups the window under Nocturne, so the taskbar shows its icon rather than python's or a stale cached one."""
    if sys.platform != "win32":
        return
    import ctypes

    ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(APP_ID)

def use_icon(window):
    """Sets the window's and taskbar's icon from the .ico itself, not Windows' cached copy."""
    if sys.platform != "win32" or not ICON.exists():
        return
    from System import Action
    from System.Drawing import Icon

    form = window.native
    icon = Icon(str(ICON))

    def apply():
        form.Icon = icon

    form.Invoke(Action(apply))

def main():
    claim_taskbar_identity()
    audio_cache = None
    passphrase = saved_passphrase()
    if passphrase:
        audio_cache = AudioCache(passphrase)

    server = serve_in_background(default_database(), audio_cache)
    port = server.server_address[1]
    width, height = window_size()
    window = webview.create_window(TITLE, f"http://{HOST}:{port}/", width=width, height=height,
                                   min_size=MIN_SIZE, background_color=BACKGROUND)
    window.events.shown += use_icon
    try:
        webview.start()
    finally:
        server.shutdown()
        server.server_close()

if __name__ == "__main__":
    main()
