#!/usr/bin/env python3
"""Run the Arduino Now Playing remote on macOS with Google Chrome."""

import argparse
from collections import deque
from concurrent.futures import ThreadPoolExecutor
import glob
import json
import os
from pathlib import Path
import select
import struct
import subprocess
import sys
import tempfile
import termios
import textwrap
import time
import tty
import unicodedata
from urllib.parse import urlparse
from urllib.request import HTTPRedirectHandler, build_opener

ART_SIZE = 40
COMMANDS = {
    b"YTM:" + name.encode(): name for name in (
        "PREVIOUS", "NEXT", "SHUFFLE", "REPEAT", "PLAY_PAUSE", "LOUDER", "QUIETER", "MUTE"
    )
}


class ChromeBridge:
    def __init__(self):
        self.process = None

    def request(self, commands):
        try:
            if self.process is None:
                script = Path(__file__).with_name("chrome_bridge.js")
                self.process = subprocess.Popen(
                    ["/usr/bin/osascript", "-l", "JavaScript", str(script)],
                    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                )
            proc = self.process
            proc.stdin.write(json.dumps(commands).encode() + b"\n")
            proc.stdin.flush()
            if not select.select([proc.stdout], [], [], 6)[0]:
                raise TimeoutError("Chrome did not respond. Check the macOS automation prompt.")
            return json.loads(proc.stdout.readline())
        except (OSError, ValueError) as error:
            self.close()
            return {"error": str(error)}

    def close(self):
        if self.process:
            if self.process.poll() is None:
                self.process.terminate()
                try:
                    self.process.wait(timeout=1)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait()
            self.process.stdin.close()
            self.process.stdout.close()
            self.process = None


class NoRedirects(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def artwork_rows(bmp):
    offset = struct.unpack_from("<I", bmp, 10)[0]
    _, width, height, planes, bpp, compression = struct.unpack_from("<IiiHHI", bmp, 14)
    if (bmp[:2] != b"BM" or width != ART_SIZE or abs(height) != ART_SIZE
            or planes != 1 or bpp not in (24, 32) or compression != 0):
        raise ValueError("Unexpected thumbnail format")
    channels = bpp // 8
    stride = (width * channels + 3) & ~3
    rows = []
    for y in range(ART_SIZE):
        source_y = y if height < 0 else ART_SIZE - 1 - y
        pos = offset + source_y * stride
        row = []
        for x in range(ART_SIZE):
            blue, green, red = bmp[pos + x * channels:pos + x * channels + 3]
            color = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            row.append(f"{color:04x}")
        rows.append(f"R{y:02d}{''.join(row)}".encode())
    return rows


def load_artwork(url):
    location = urlparse(url)
    allowed = ("googleusercontent.com", "ytimg.com", "ggpht.com")
    host = location.hostname or ""
    if location.scheme != "https" or not any(host == h or host.endswith("." + h) for h in allowed):
        return []
    try:
        with build_opener(NoRedirects()).open(url, timeout=3) as response:
            image = response.read(1_000_001)
        if len(image) > 1_000_000:
            return []
        with tempfile.TemporaryDirectory() as tmp:
            source = Path(tmp) / "cover"
            bmp = Path(tmp) / "cover.bmp"
            source.write_bytes(image)
            subprocess.run(
                ["/usr/bin/sips", "-z", str(ART_SIZE), str(ART_SIZE),
                 "-s", "format", "bmp", str(source), "--out", str(bmp)],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=3, check=True,
            )
            return artwork_rows(bmp.read_bytes())
    except (OSError, ValueError, struct.error, subprocess.SubprocessError):
        return []


def display_text(value):
    value = unicodedata.normalize("NFKD", value)
    value = "".join(char for char in value if not unicodedata.combining(char))
    value = value.encode("ascii", "replace").decode().replace("@", "?").replace("|", "/")
    return " ".join(value.split())


def state_packet(state):
    volume = state.get("volume")
    volume = max(0, min(100, round(volume))) if isinstance(volume, (int, float)) else 999
    playing = {True: "P", False: "A"}.get(state.get("playing"), "?")
    shuffle = state.get("shuffle") if state.get("shuffle") in ("0", "1") else "?"
    repeat = state.get("repeat") if state.get("repeat") in ("0", "A", "1") else "?"
    mute = "1" if state.get("muted") is True else "0"
    return f"S{volume:03d}{playing}{shuffle}{repeat}{mute}".encode()


class Display:
    def __init__(self):
        self.active = False
        self.sequence = 0
        self.reset()

    def reset(self):
        self.fields = {}
        self.pending = {}
        self.rows = deque()
        self.cover_url = ""
        self.art_requested = False
        self.waiting = None
        self.deadline = 0
        self.retried = False

    def set_field(self, key, packet):
        if self.fields.get(key) != packet:
            self.fields[key] = packet
            self.pending[key] = packet

    def snapshot(self, state):
        title = display_text(state.get("title") or "Open YouTube Music")
        artist = display_text(state.get("artist") or "")
        title = "|".join(textwrap.wrap(title, width=13, max_lines=3, placeholder="..."))
        self.set_field("title", b"T" + title.encode())
        self.set_field("artist", b"N" + textwrap.shorten(artist, width=13, placeholder="...").encode())
        self.set_field("state", state_packet(state))
        url = state.get("art") or ""
        if url != self.cover_url:
            self.cover_url = url
            self.art_requested = False
            self.pending["cover"] = b"C"
            self.rows.clear()

    def acknowledge(self, sequence):
        if self.waiting and sequence == self.waiting[1:3]:
            self.waiting = None

    def next_packet(self, now):
        if not self.active:
            return None
        if self.waiting:
            if now < self.deadline:
                return None
            if not self.retried:
                self.retried = True
                self.deadline = now + 0.3
                return self.waiting
            # A new packet gets a new ID, so delayed ACKs cannot acknowledge it.
            self.waiting = None
        if self.pending:
            packet = self.pending.pop(next(iter(self.pending)))
        elif self.rows:
            packet = self.rows.popleft()
        else:
            return None
        self.sequence = (self.sequence + 1) % 256
        self.waiting = f"@{self.sequence:02x}".encode() + packet + b"\n"
        self.deadline = now + 0.3
        self.retried = False
        return self.waiting


def listen(port):
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY)
    bridge = ChromeBridge()
    try:
        tty.setraw(fd)
        settings = termios.tcgetattr(fd)
        settings[2] |= termios.CLOCAL | termios.CREAD
        settings[4] = settings[5] = termios.B115200
        termios.tcsetattr(fd, termios.TCSAFLUSH, settings)
        print(f"Listening on {port}. Open YouTube Music on the arcade. Ctrl+C to stop.", flush=True)
        with ThreadPoolExecutor(max_workers=2) as workers:
            relay(fd, bridge, workers)
    finally:
        bridge.close()
        os.close(fd)


def relay(fd, bridge, workers):
    display = Display()
    commands = []
    buffer = b""
    next_poll = 0
    browser_job = art_job = None
    requested_url = ""
    previous_message = ""
    while True:
        if select.select([fd], [], [], 0.012)[0]:
            chunk = os.read(fd, 512)
            if not chunk:
                return
            buffer += chunk
            while b"\n" in buffer:
                line, _, buffer = buffer.partition(b"\n")
                line = line.rstrip(b"\r")
                if line == b"YTM:OPEN":
                    display.reset()
                    display.active = True
                    next_poll = 0
                elif line == b"YTM:CLOSE":
                    display.active = False
                    commands.clear()
                elif line.startswith(b"YTM:ACK"):
                    display.acknowledge(line[7:])
                elif display.active and line in COMMANDS:
                    commands.append(COMMANDS[line])
            if len(buffer) > 128:
                buffer = b""

        now = time.monotonic()
        if browser_job and browser_job.done():
            snapshot = browser_job.result()
            browser_job = None
            message = snapshot.get("error") or snapshot.get("unavailable") or ""
            if message and message != previous_message:
                print(message, file=sys.stderr, flush=True)
            previous_message = message
            if display.active:
                display.snapshot(snapshot)
        if display.active and not browser_job and (commands or now >= next_poll):
            browser_job = workers.submit(bridge.request, commands)
            commands = []
            next_poll = now + 0.4
        if art_job and art_job.done():
            if display.active and requested_url == display.cover_url:
                display.rows.extend(art_job.result())
            art_job = None
        if display.active and not art_job and display.cover_url and not display.art_requested:
            requested_url = display.cover_url
            display.art_requested = True
            art_job = workers.submit(load_artwork, requested_url)
        packet = display.next_packet(now)
        if packet:
            while packet:
                packet = packet[os.write(fd, packet):]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", nargs="?", help="Uno serial port, e.g. /dev/cu.usbmodem11301")
    args = parser.parse_args()
    ports = glob.glob("/dev/cu.usbmodem*")
    if not args.port and len(ports) != 1:
        parser.error("Connect one Uno or pass the port shown in Arduino IDE > Tools > Port.")
    try:
        listen(args.port or ports[0])
    except KeyboardInterrupt:
        print("\nMusic remote stopped.")
    except OSError as error:
        parser.exit(1, f"Serial connection error: {error}\nStop the old helper and close Serial Monitor.\n")


if __name__ == "__main__":
    main()
