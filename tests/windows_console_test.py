"""Exercise real Windows console modes without touching the user's console."""

import ctypes
from ctypes import wintypes
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def read_status(path):
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return None


def wait_for(predicate, process, path, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        status = read_status(path)
        if status and predicate(status):
            return status
        if process.poll() is not None:
            raise AssertionError(f"probe exited {process.returncode}: {status}")
        time.sleep(0.025)
    raise AssertionError(f"probe stopped responding: {read_status(path)}")


def check_mode(binary, directory, mode):
    path = directory / f"{mode}.json"
    environment = os.environ.copy()
    environment["TERM"] = "xterm"
    environment.pop("BRMINER_CONSOLE", None)
    if mode == "plain":
        environment["BRMINER_CONSOLE"] = "plain"
    startup = subprocess.STARTUPINFO()
    startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0  # SW_HIDE: keep test windows out of the user's way.
    process = subprocess.Popen(
        [str(binary), "--windows-console-probe", str(path), mode],
        env=environment,
        creationflags=subprocess.CREATE_NEW_CONSOLE,
        startupinfo=startup,
        stdout=subprocess.DEVNULL if mode == "redirected" else None,
    )
    try:
        status = wait_for(lambda s: s["tick"] >= 2, process, path)
        expected = status["before"]
        if mode not in ("quiet", "redirected"):
            expected &= ~0x40  # ENABLE_QUICK_EDIT_MODE
        assert status["active"] == expected, (mode, status)
        assert status["live"] == (mode == "live"), (mode, status)

        # Send mouse clicks only to the probe's own hidden window, then verify
        # output keeps advancing. No focus changes or real mouse input are used.
        hwnd = status["hwnd"]
        assert hwnd, status
        position = (12 << 16) | 12
        assert user32.PostMessageW(hwnd, 0x0201, 1, position)  # WM_LBUTTONDOWN
        assert user32.PostMessageW(hwnd, 0x0200, 1, position + 40)  # WM_MOUSEMOVE
        assert user32.PostMessageW(hwnd, 0x0202, 0, position + 40)  # WM_LBUTTONUP
        status = wait_for(lambda s: s["tick"] >= status["tick"] + 4, process, path)

        if mode == "live":
            user32.ShowWindow(hwnd, 7)  # SW_SHOWMINNOACTIVE, only this test window.
            try:
                assert user32.IsIconic(hwnd), "Probe console did not minimize"
                wait_for(lambda s: s["tick"] >= status["tick"] + 4, process, path)
            finally:
                user32.ShowWindow(hwnd, 0)

        Path(str(path) + ".stop").touch()
        assert process.wait(timeout=5) == 0, read_status(path)
        status = read_status(path)
        assert status["finished"], status
        assert status["restored"] == status["before"], status
        assert status["output_restored"], status
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)
    print(f"PASS {mode}: input modes, mouse responsiveness, and restoration")


if __name__ == "__main__":
    if sys.platform != "win32":
        raise SystemExit("Windows console test requires Windows")
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user32.PostMessageW.restype = wintypes.BOOL
    user32.ShowWindow.argtypes = [wintypes.HWND, ctypes.c_int]
    user32.ShowWindow.restype = wintypes.BOOL
    user32.IsIconic.argtypes = [wintypes.HWND]
    user32.IsIconic.restype = wintypes.BOOL
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="brminer-console-") as directory:
        for mode in ("live", "plain", "diagnostics", "quiet", "redirected"):
            check_mode(binary, Path(directory), mode)
