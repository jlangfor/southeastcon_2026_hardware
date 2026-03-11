#!/usr/bin/env python3
"""
============================================================
Python BLE Client — Arduino R4 Earth Monitor
============================================================
Commands (type at the bottom prompt and press Enter):
  start  → sends START to Arduino, begins 3-min countdown
  stop   → sends STOP  to Arduino at any time
  quit   → disconnects and exits
============================================================
Dependencies:
  pip install bleak
============================================================
Setup:
  Place a config.json file in the same directory as this
  script. Set instance_id to match the INSTANCE_ID flashed
  onto your Arduino (1, 2, 3, or 4).

  Example config.json:
    { "instance_id": 1 }

  Each deployed instance uses its own config.json with a
  different instance_id. This ensures the Python script only
  connects to its paired Arduino even when other units are
  nearby and advertising.
"""

import asyncio
import curses
import json
import os
import sys
import threading
from bleak import BleakScanner, BleakClient

SCAN_TIMEOUT = 15.0

# ── Antenna labels ───────────────────────────────────────────
ANTENNA_LABELS = ["Antenna 1", "Antenna 2", "Antenna 3", "Antenna 4"]

# ── UUID table — must match the Arduino sketch ───────────────
UUID_TABLE = {
    1: {
        "service": "12345671-1234-1234-1234-123456789012",
        "rx":      "12345671-1234-1234-1234-123456789013",
        "tx":      "12345671-1234-1234-1234-123456789014",
    },
    2: {
        "service": "12345672-1234-1234-1234-123456789012",
        "rx":      "12345672-1234-1234-1234-123456789013",
        "tx":      "12345672-1234-1234-1234-123456789014",
    },
    3: {
        "service": "12345673-1234-1234-1234-123456789012",
        "rx":      "12345673-1234-1234-1234-123456789013",
        "tx":      "12345673-1234-1234-1234-123456789014",
    },
    4: {
        "service": "12345674-1234-1234-1234-123456789012",
        "rx":      "12345674-1234-1234-1234-123456789013",
        "tx":      "12345674-1234-1234-1234-123456789014",
    },
}


# ============================================================
# Config loader
# ============================================================

def load_config() -> dict:
    """
    Loads config.json from the same directory as this script.
    Exits with a clear message if the file is missing or invalid.
    """
    config_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json")

    if not os.path.exists(config_path):
        print(f"[ERROR] config.json not found at: {config_path}")
        print()
        print("Create a config.json file in the same folder as this script:")
        print('  { "instance_id": 1 }')
        print()
        print("Set instance_id to 1, 2, 3, or 4 to match the INSTANCE_ID")
        print("value flashed onto the paired Arduino.")
        sys.exit(1)

    try:
        with open(config_path) as f:
            cfg = json.load(f)
    except json.JSONDecodeError as e:
        print(f"[ERROR] config.json is not valid JSON: {e}")
        sys.exit(1)

    instance_id = cfg.get("instance_id")
    if instance_id not in UUID_TABLE:
        print(f"[ERROR] instance_id must be 1, 2, 3, or 4 — got: {instance_id!r}")
        sys.exit(1)

    return cfg


# ── Load config and resolve UUIDs before anything else ───────
_cfg         = load_config()
INSTANCE_ID  = _cfg["instance_id"]
SERVICE_UUID = UUID_TABLE[INSTANCE_ID]["service"]
RX_UUID      = UUID_TABLE[INSTANCE_ID]["rx"]
TX_UUID      = UUID_TABLE[INSTANCE_ID]["tx"]
DEVICE_LABEL = f"Arduino-R4-{INSTANCE_ID}"


# ── Shared state ─────────────────────────────────────────────
antenna_states = [False, False, False, False]
session_active = False
time_remaining = 0
last_event     = "waiting"
log_lines      = []
input_queue    = None

# ── Curses windows ───────────────────────────────────────────
status_win = None
log_win    = None
input_win  = None
ui_lock    = threading.Lock()

MAX_LOG_LINES = 6


# ============================================================
# Helpers
# ============================================================

def format_time(seconds: int) -> str:
    m, s = divmod(max(seconds, 0), 60)
    return f"{m:02d}:{s:02d}"


def add_log(msg: str):
    import time
    ts = time.strftime("%H:%M:%S")
    log_lines.append(f"[{ts}] {msg}")
    if len(log_lines) > MAX_LOG_LINES:
        log_lines.pop(0)


# ============================================================
# Curses layout & drawing
# ============================================================

def init_windows(scr):
    global status_win, log_win, input_win

    curses.curs_set(1)
    curses.use_default_colors()
    curses.init_pair(1, curses.COLOR_GREEN,  -1)
    curses.init_pair(2, curses.COLOR_YELLOW, -1)
    curses.init_pair(3, curses.COLOR_RED,    -1)
    curses.init_pair(4, curses.COLOR_CYAN,   -1)
    curses.init_pair(5, curses.COLOR_WHITE,  -1)
    curses.init_pair(6, curses.COLOR_BLACK, curses.COLOR_WHITE)

    rows, cols = scr.getmaxyx()
    status_h = 9
    log_h    = MAX_LOG_LINES + 2

    status_win = curses.newwin(status_h, cols, 0, 0)
    log_win    = curses.newwin(log_h, cols, status_h, 0)
    input_win  = curses.newwin(1, cols, rows - 1, 0)
    input_win.keypad(True)
    input_win.nodelay(False)


def draw_status():
    if status_win is None:
        return
    _, cols = status_win.getmaxyx()
    with ui_lock:
        status_win.erase()

        # Header — shows instance identity prominently
        header = f" Arduino R4 IR Monitor  —  Instance {INSTANCE_ID} ({DEVICE_LABEL}) "
        try:
            status_win.addstr(0, 0, header.center(cols), curses.color_pair(6))
        except curses.error:
            pass

        # Session status
        status_win.addstr(2, 2, "Status   : ")
        if session_active:
            status_win.addstr("ACTIVE", curses.color_pair(1) | curses.A_BOLD)
            status_win.addstr(3, 2, "Remaining: ")
            status_win.addstr(format_time(time_remaining),
                              curses.color_pair(2) | curses.A_BOLD)
        else:
            labels = {
                "waiting":      ("Waiting for START",            5),
                "stopped":      ("Stopped",                      3),
                "timeout":      ("Session Complete",             4),
                "disconnected": ("Disconnected",                 3),
                "connected":    (f"Connected — type start",      2),
                "scanning":     (f"Scanning for {DEVICE_LABEL}...", 4),
            }
            text, pair = labels.get(last_event, (last_event, 5))
            status_win.addstr(text, curses.color_pair(pair) | curses.A_BOLD)

        # Antenna grid
        status_win.addstr(5, 2, "Antennas:", curses.A_UNDERLINE)
        col_w = max(cols // 4, 16)
        for i, label in enumerate(ANTENNA_LABELS):
            x      = 2 + i * col_w
            active = antenna_states[i]
            dot    = "● ACTIVE" if active else "○  idle "
            pair   = curses.color_pair(1) if active else curses.color_pair(5)
            try:
                status_win.addstr(6, x, f"[{i+1}] {label}", curses.A_DIM)
                status_win.addstr(7, x, dot, pair)
            except curses.error:
                pass

        try:
            status_win.addstr(8, 0, "─" * (cols - 1), curses.A_DIM)
        except curses.error:
            pass

        status_win.noutrefresh()


def draw_log():
    if log_win is None:
        return
    _, cols = log_win.getmaxyx()
    with ui_lock:
        log_win.erase()
        log_win.addstr(0, 2, "Events:", curses.A_DIM)
        for i, line in enumerate(log_lines):
            try:
                log_win.addstr(i + 1, 2, line[:cols - 3], curses.A_DIM)
            except curses.error:
                pass
        try:
            log_win.addstr(MAX_LOG_LINES + 1, 0, "─" * (cols - 1), curses.A_DIM)
        except curses.error:
            pass
        log_win.noutrefresh()


def draw_input(current_text: str = ""):
    if input_win is None:
        return
    _, cols = input_win.getmaxyx()
    with ui_lock:
        input_win.erase()
        prompt = "cmd (start/stop/quit) > "
        try:
            input_win.addstr(0, 0, prompt, curses.color_pair(4))
            input_win.addstr(current_text[:cols - len(prompt) - 1])
        except curses.error:
            pass
        input_win.noutrefresh()
    curses.doupdate()


def refresh_all(current_text: str = ""):
    draw_status()
    draw_log()
    draw_input(current_text)
    curses.doupdate()


# ============================================================
# BLE notification handler
# ============================================================

def notification_handler(sender, data: bytearray):
    global antenna_states, session_active, time_remaining, last_event

    try:
        msg = json.loads(data.decode("utf-8"))
    except (json.JSONDecodeError, UnicodeDecodeError) as e:
        add_log(f"Bad JSON: {e}")
        refresh_all()
        return

    # Sanity-check: confirm the message is from our instance
    reported_id = msg.get("instance")
    if reported_id is not None and reported_id != INSTANCE_ID:
        add_log(f"[WARN] Ignored message from instance {reported_id}")
        return

    event      = msg.get("event", "")
    last_event = event

    if event == "status":
        time_remaining = msg.get("remaining", 0)
        raw            = msg.get("antennas", [0, 0, 0, 0])
        antenna_states = [bool(v) for v in raw]
        session_active = True

    elif event == "started":
        time_remaining = msg.get("remaining", 180)
        antenna_states = [False, False, False, False]
        session_active = True
        add_log(f"Session started — {format_time(time_remaining)} countdown")

    elif event == "stopped":
        session_active = False
        add_log(f"Session stopped ({msg.get('reason', 'unknown')})")

    elif event == "timeout":
        session_active = False
        add_log("Session complete — countdown finished")

    elif event in ("connected", "ready"):
        session_active = False
        add_log(f"Instance {INSTANCE_ID} connected and ready")

    refresh_all()


# ============================================================
# Input thread
# ============================================================

def input_thread(loop):
    buf = ""
    while True:
        try:
            ch = input_win.get_wch()
        except curses.error:
            continue

        if ch in (curses.KEY_ENTER, "\n", "\r"):
            cmd = buf.strip().lower()
            buf = ""
            draw_input("")
            if cmd:
                asyncio.run_coroutine_threadsafe(input_queue.put(cmd), loop)
        elif ch in (curses.KEY_BACKSPACE, "\x7f", "\b"):
            buf = buf[:-1]
            draw_input(buf)
        elif isinstance(ch, str) and ch.isprintable():
            buf += ch
            draw_input(buf)


# ============================================================
# BLE scan
# ============================================================

async def find_arduino() -> str | None:
    global last_event
    found_address = None
    found_event   = asyncio.Event()

    def _callback(device, adv):
        nonlocal found_address
        uuids = [str(u).lower() for u in (adv.service_uuids or [])]
        if SERVICE_UUID.lower() in uuids and not found_event.is_set():
            found_address = device.address
            found_event.set()

    last_event = "scanning"
    add_log(f"Scanning for instance {INSTANCE_ID} (up to {int(SCAN_TIMEOUT)}s)...")
    refresh_all()

    async with BleakScanner(detection_callback=_callback):
        try:
            await asyncio.wait_for(found_event.wait(), timeout=SCAN_TIMEOUT)
            add_log(f"Found {DEVICE_LABEL} @ {found_address}")
            return found_address
        except asyncio.TimeoutError:
            pass

    add_log(f"Scan timed out — {DEVICE_LABEL} not found.")
    add_log("Check BLE permission & Serial Monitor, then restart.")
    last_event = "disconnected"
    refresh_all()
    return None


# ============================================================
# Main BLE coroutine
# ============================================================

async def run_ble(scr):
    global input_queue, last_event

    input_queue = asyncio.Queue()
    init_windows(scr)
    refresh_all()

    address = await find_arduino()
    if not address:
        await asyncio.sleep(6)
        return

    add_log("Connecting...")
    refresh_all()

    async with BleakClient(address) as client:
        last_event = "connected"
        add_log(f"Connected to {DEVICE_LABEL}. Type  start  to begin.")
        refresh_all()

        await client.start_notify(TX_UUID, notification_handler)

        loop = asyncio.get_event_loop()
        t = threading.Thread(target=input_thread, args=(loop,), daemon=True)
        t.start()

        while True:
            cmd = await input_queue.get()

            if cmd == "start":
                add_log("Sending START →")
                await client.write_gatt_char(RX_UUID, b"START", response=False)
            elif cmd == "stop":
                add_log("Sending STOP →")
                await client.write_gatt_char(RX_UUID, b"STOP", response=False)
            elif cmd == "quit":
                add_log("Disconnecting...")
                refresh_all()
                break
            else:
                add_log(f"Unknown: '{cmd}'  (start / stop / quit)")

            refresh_all()

        await client.stop_notify(TX_UUID)


def main(scr):
    asyncio.run(run_ble(scr))


if __name__ == "__main__":
    try:
        curses.wrapper(main)
    except KeyboardInterrupt:
        pass
    print("Goodbye.")