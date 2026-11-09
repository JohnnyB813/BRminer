"""Verify the live renderer through a real PTY and an independent VT emulator."""
import errno
import fcntl
import os
import pathlib
import pty
import re
import select
import signal
import socket
import struct
import subprocess
import sys
import termios
import time

import pyte


def verify_miner_refresh(binary):
    """An idle/reconnecting miner must repaint before its 10-second sample."""
    master, slave = pty.openpty()
    fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 28, 100, 0, 0))
    screen = pyte.Screen(100, 28)
    stream = pyte.Stream(screen)
    env = {key: value for key, value in os.environ.items() if not key.upper().startswith("BRMINER_")}
    env.update(TERM="xterm", NO_COLOR="", BRMINER_DISABLE_NVML="1")
    # Keep the port reserved but not listening: no public pool or real work.
    with socket.socket() as reserved:
        reserved.bind(("127.0.0.1", 0))
        port = reserved.getsockname()[1]
        process = subprocess.Popen([str(pathlib.Path(binary).resolve()), "-a", "warthog",
                                    "-p", f"127.0.0.1:{port}", "-w", "syntheticTestWallet",
                                    "--cpu_threads", "1", "--backend", "cpu", "--no-autotune"],
                                   stdin=slave, stdout=slave, stderr=slave, env=env)
        os.close(slave)
        try:
            deadline = time.monotonic() + 3.4
            while time.monotonic() < deadline:
                if select.select([master], [], [], .05)[0]:
                    stream.feed(os.read(master, 65536).decode("utf8", errors="replace"))
                assert process.poll() is None, "\n".join(screen.display)
            text = "\n".join(screen.display)
            assert re.search(r"Up 00:00:0[23]", text), text
            assert "RECONNECTING" in text and "ERROR" in text, text
            process.send_signal(signal.SIGTERM)
            process.wait(timeout=6)
            assert process.returncode == 0
            print("Real miner refreshes uptime and reconnect status before the 10-second control sample.")
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=3)
            os.close(master)


def main():
    master, slave = pty.openpty()
    screen = pyte.Screen(120, 35)
    stream = pyte.Stream(screen)
    transcript = []

    def resize(columns, lines):
        fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", lines, columns, 0, 0))
        screen.resize(lines=lines, columns=columns)

    def consume(seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if select.select([master], [], [], .05)[0]:
                try:
                    data = os.read(master, 65536)
                except OSError as error:
                    if error.errno == errno.EIO:
                        return
                    raise
                text = data.decode("utf8", errors="replace")
                transcript.append(text)
                stream.feed(text)

    def check():
        text = "\n".join(screen.display)
        assert screen.display[0].lstrip().startswith("BRminer / WARTHOG"), text
        assert text.count("JANUS (10s)") == 1, text
        assert "Ctrl+C Stop" in text, text
        return text

    resize(120, 35)
    env = dict(os.environ, TERM="xterm", NO_COLOR="", BRMINER_CONSOLE="")
    process = subprocess.Popen([str(pathlib.Path(sys.argv[1]).resolve()), "--live-demo"],
                               stdin=slave, stdout=slave, stderr=slave, env=env)
    os.close(slave)
    try:
        consume(.7)
        check()
        resize(80, 25)
        consume(.7)
        compact = check()
        assert "DEVICE" in compact and "N/A" in compact, compact
        def anchor(text, prefix):
            return next(i for i, line in enumerate(text.splitlines()) if line.startswith(prefix))
        headings = [" DEVICE", " TIME"]
        positions = [anchor(compact, prefix) for prefix in headings]
        consume(.5)
        assert [anchor(check(), prefix) for prefix in headings] == positions
        resize(45, 14)
        consume(.5)
        small = check()
        assert "EVENTS" in small and "Accepted" in small, small
        resize(120, 35)
        consume(3.5)
        text = check()
        assert "SIMULATED BLOCK ACCEPTED." in text, text
        assert "Accepted 2" in text and "Blocks 2" in text, text
        assert "SIMULATED new job" in text, text
        assert "Up 01:00:04" in text, text
        assert any(cell.bg == "green" for row in screen.buffer.values() for cell in row.values()), text
        consume(3)
        process.wait(timeout=2)
        assert process.returncode == 0
        raw = "".join(transcript)
        assert "\x1b[2J" not in raw and "\x1b[2K" not in raw  # No clear-before-paint flicker.
        assert "\x1b[?25l" in raw and "\x1b[?25h" in raw
        assert "\x1b[?1049h" in raw and "\x1b[?1049l" in raw
        print("Colored dashboard, resize, immediate shares, and terminal restoration passed.")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=3)
        os.close(master)

    miner = pathlib.Path(sys.argv[2]) if len(sys.argv) > 2 else pathlib.Path(__file__).resolve().parents[1] / "brminer"
    if miner.is_file():
        verify_miner_refresh(miner)

    # NO_COLOR keeps cursor updates but removes every SGR color/style sequence.
    master, slave = pty.openpty()
    fcntl.ioctl(master, termios.TIOCSWINSZ, struct.pack("HHHH", 25, 80, 0, 0))
    process = subprocess.Popen([str(pathlib.Path(sys.argv[1]).resolve()), "--live-once"],
                               stdin=slave, stdout=slave, stderr=slave,
                               env=dict(env, NO_COLOR="1"))
    os.close(slave)
    transcript.clear()
    try:
        consume(.5)
        process.wait(timeout=2)
        assert process.returncode == 0
        assert not re.search(r"\x1b\[[0-9;]*m", "".join(transcript))
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=3)
        os.close(master)


if __name__ == "__main__":
    main()
