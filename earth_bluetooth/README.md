# Arduino R4 IR Antenna Monitor

A Bluetooth Low Energy (BLE) system where an **Arduino UNO R4 WiFi** monitors four IR antenna signals and streams their status to a **Python terminal client** in real time. Supports up to four simultaneous independent instances operating in close proximity without interference.

---

## System Overview

```
┌─────────────────────┐         BLE          ┌─────────────────────┐
│  Arduino UNO R4     │ ◄──── START/STOP ──── │  Python Client      │
│  (arduino_ir_ble)   │ ───── JSON status ──► │  (ble_monitor.py)   │
│                     │                       │                     │
│  • 3-min countdown  │                       │  • Live dashboard   │
│  • 4 IR antennas   │                       │  • Event log        │
│  • BLE advertising  │                       │  • start/stop/quit  │
└─────────────────────┘                       └─────────────────────┘
```

The Arduino advertises a BLE service, waits for a `START` command, then runs a 180-second countdown while reading four IR sensor inputs and sending status updates. The Python client connects, displays antenna states in a live terminal dashboard, and lets the operator send commands at any time.

---

## Files

| File | Description |
|---|---|
| `arduino_ir_ble.ino` | Arduino sketch — flash onto each UNO R4 WiFi board |
| `ble_monitor.py` | Python BLE client — run on the connected computer |
| `config.json` | Per-instance configuration for the Python client |

---

## Requirements

### Arduino
- **Board:** Arduino UNO R4 WiFi
- **Library:** `ArduinoBLE` (install via Arduino IDE Library Manager)
- **IDE:** Arduino IDE 2.x recommended

### Python
- Python 3.10 or later
- `bleak` library:
  ```bash
  pip install bleak
  ```

---

## Quick Start (Single Instance)

### 1. Flash the Arduino

1. Open `arduino_ir_ble.ino` in Arduino IDE.
2. Confirm `INSTANCE_ID` is set to `1` (default).
3. Select board: **Arduino UNO R4 WiFi**.
4. Upload the sketch.
5. Open Serial Monitor at **9600 baud** and confirm you see:
   ```
   [INIT] Instance ID : 1
   [BLE] Advertising as 'Arduino-R4-1'...
   ```

### 2. Configure the Python Client

Create `config.json` in the same folder as `ble_monitor.py`:
```json
{ "instance_id": 1 }
```

### 3. Run the Python Client

```bash
python3 ble_monitor.py
```

The client scans for the Arduino, connects, and displays the live dashboard. Type commands at the prompt at the bottom of the screen.

---

## Python Client — Terminal Dashboard

The display is divided into three fixed panels that update independently:

```
══════════════════════════════════════════════════════════════
 Arduino R4 IR Monitor  —  Instance 1 (Arduino-R4-1)
══════════════════════════════════════════════════════════════
  Status   : ACTIVE
  Remaining: 02:47

  Antennas:
  [1] Antenna 1     [2] Antenna 2     [3] Antenna 3     [4] Antenna 4
  ● ACTIVE          ○  idle           ● ACTIVE          ○  idle
──────────────────────────────────────────────────────────────
  Events:
  [10:42:01] Instance 1 connected and ready
  [10:42:08] Session started — 03:00 countdown
  [10:42:12] Antenna 1 changed
──────────────────────────────────────────────────────────────
  cmd (start/stop/quit) > _
```

### Commands

| Command | Action |
|---|---|
| `start` | Sends START to Arduino — resets all antennas, begins 3-minute countdown |
| `stop` | Sends STOP to Arduino — ends the session immediately |
| `quit` | Disconnects from the Arduino and exits the program |

Type the command at the bottom prompt and press **Enter**. The top panels update continuously without interrupting typing.

---

## Multi-Instance Deployment

Up to four independent instances can run simultaneously in the same area. Each instance uses unique BLE UUIDs and a unique device name, so Arduinos and Python clients are strictly paired and cannot interfere with each other.

### Instance UUID Table

| Instance | Device Name | Service UUID |
|---|---|---|
| 1 | `Arduino-R4-1` | `12345671-1234-1234-1234-123456789012` |
| 2 | `Arduino-R4-2` | `12345672-1234-1234-1234-123456789012` |
| 3 | `Arduino-R4-3` | `12345673-1234-1234-1234-123456789012` |
| 4 | `Arduino-R4-4` | `12345674-1234-1234-1234-123456789012` |

### Deployment Steps for Each Instance

**Arduino — change one line before flashing each board:**
```cpp
// ╔══════════════════════════════════════════════════════════╗
// ║  CHANGE THIS VALUE (1, 2, 3, or 4) FOR EACH BOARD       ║
#define INSTANCE_ID 2
// ╚══════════════════════════════════════════════════════════╝
```

**Python — each deployed folder needs its own `config.json`:**
```json
{ "instance_id": 2 }
```

### Deployment Checklist

| | Instance 1 | Instance 2 | Instance 3 | Instance 4 |
|---|---|---|---|---|
| Arduino `INSTANCE_ID` | `1` | `2` | `3` | `4` |
| `config.json` value | `1` | `2` | `3` | `4` |
| BLE device name | `Arduino-R4-1` | `Arduino-R4-2` | `Arduino-R4-3` | `Arduino-R4-4` |

Each Python deployment is a folder containing `ble_monitor.py` and its own `config.json`. The Arduino sketch file is the same for all boards — only `INSTANCE_ID` changes.

---

## BLE Protocol Reference

All messages are JSON strings transmitted over BLE notifications.

### Arduino → Python (TX Characteristic)

| Event | Fields | Description |
|---|---|---|
| `ready` | `instance` | Sent on BLE power-up |
| `connected` | `instance` | Sent when Python client connects |
| `started` | `instance`, `remaining` | Sent when START is received; `remaining` = 180 |
| `status` | `instance`, `remaining`, `antennas` | Periodic / on-change update |
| `stopped` | `instance`, `reason` | Session ended; reason: `stopped`, `timeout`, or `disconnected` |

**Status message example:**
```json
{"event":"status","instance":1,"remaining":142,"antennas":[1,0,0,1]}
```
`antennas` is an array of four integers: `1` = signal active, `0` = idle.

### Python → Arduino (RX Characteristic)

| Command | Effect |
|---|---|
| `START` | Resets all antenna states, begins 180-second countdown |
| `STOP` | Ends the current session immediately |

---

## IR Sensor Hardware

The sketch currently includes a **placeholder** that simulates antenna state changes to allow testing without hardware. When your IR receiver hardware is available, replace the placeholder in `readIRSensors()`:

```cpp
void readIRSensors() {

  // ── REAL HARDWARE (uncomment when ready) ─────────────────
  // for (int i = 0; i < NUM_ANTENNAS; i++) {
  //   antennaState[i] = (digitalRead(IR_PINS[i]) == LOW);
  // }

  // ── PLACEHOLDER — delete when real hardware is connected ─
  // ... simulated random state changes ...
}
```

Also define the input pins near the top of the sketch:
```cpp
const int IR_PINS[NUM_ANTENNAS] = {2, 3, 4, 5};
```

And uncomment the `pinMode` calls in `setup()`:
```cpp
for (int i = 0; i < NUM_ANTENNAS; i++) {
  pinMode(IR_PINS[i], INPUT_PULLUP);
}
```

**Typical wiring:** Connect each IR receiver's signal output to a digital pin. The sketch uses `INPUT_PULLUP` mode, where a LOW reading indicates an active signal.

---

## Timing Configuration

These constants can be adjusted in the Arduino sketch:

| Constant | Default | Description |
|---|---|---|
| `COUNTDOWN_SECONDS` | `180` | Session duration in seconds |
| `STATUS_INTERVAL_MS` | `500` | Periodic status send interval (ms) |
| `SIM_CHANGE_INTERVAL` | `4000` | Placeholder: simulated IR change interval (ms) |

---

## Troubleshooting

**Python cannot find the Arduino**
- Confirm the Arduino Serial Monitor shows `[BLE] Advertising as 'Arduino-R4-N'...`
- Verify Bluetooth is enabled on the host computer
- Ensure `config.json` exists in the same directory as `ble_monitor.py`
- Check that the `instance_id` in `config.json` matches `INSTANCE_ID` in the sketch
- Try unplugging and replugging the Arduino, then re-run the Python script

**Arduino reports `[ERROR] BLE failed to start!`**
- Unplug and replug the board
- Verify the `ArduinoBLE` library is installed in the Arduino IDE

**Python exits immediately with a config error**
- Confirm `config.json` is in the same folder as `ble_monitor.py`
- Confirm the file contains valid JSON: `{ "instance_id": 1 }`
- `instance_id` must be an integer: `1`, `2`, `3`, or `4`

**Instance cross-talk / connecting to wrong Arduino**
- Verify each board has a unique `INSTANCE_ID` flashed
- Verify each Python folder has its own `config.json` with the matching value
- The sketch will fail to compile if `INSTANCE_ID` is set to an invalid value