# Earth & Antenna — NRF24L01+ Wireless Communication System

 A reliable one-to-many wireless communication system built on Arduino using the NRF24L01+ radio module and the RF24 library. One **Earth** (Arduino Uno R3/R4) manages a pool of up to 5 **Antennas** (Arduino Nanos), sending reset commands with color data and receiving status updates in return. The firmware was initially developed on the Uno R3, with the Uno R4 as the intended deployment platform to enable Bluetooth communication back to the controlling laptop for issuing reset commands.

---

## Table of Contents

- [Hardware Requirements](#hardware-requirements)
- [Wiring](#wiring)
- [System Architecture](#system-architecture)
- [How Communication Works](#how-communication-works)
- [Address Configuration](#address-configuration)
- [Packet Structure](#packet-structure)
- [Earth — Full Behaviour](#earth--full-behaviour)
- [Antenna — Full Behaviour](#antenna--full-behaviour)
- [Serial Commands](#serial-commands)
- [Deploying Multiple Networks](#deploying-multiple-networks)
- [Troubleshooting](#troubleshooting)
- [Key Design Decisions](#key-design-decisions)
- [Development History](#development-history)

---

## Hardware Requirements

| Component | Role | Qty |
|---|---|---|
| Arduino Uno R3/R4 | Earth — central controller | 1 |
| Arduino Nano | Antenna — remote node | Up to 5 |
| NRF24L01+ module | Wireless radio (one per board) | 2–6 |

> **Power note:** The NRF24L01+ runs on **3.3V only** — connecting VCC to 5V will damage the module. The NRF24L01+ can draw up to 115mA during transmission which can exceed the Arduino's onboard 3.3V regulator. If you experience intermittent failures, adding a 10µF electrolytic capacitor across VCC and GND on the NRF24L01+ module is the most effective fix. A 100nF ceramic cap in parallel provides additional high-frequency filtering. A 10Ω–100Ω resistor in series with VCC is a useful alternative if no capacitor is available.

---

## Wiring

The wiring is identical for both the Arduino Uno R3/R4 (Earth) and the Nano (Antenna):

| NRF24L01+ Pin | Arduino Pin |
|---|---|
| VCC | 3.3V |
| GND | GND |
| CE | 9 |
| CSN | 10 |
| SCK | 13 |
| MOSI | 11 |
| MISO | 12 |

```
NRF24L01+ module pinout (top view, antenna facing away):

  [ antenna ]

  IRQ  MISO
  SCK  MOSI
  CSN  CE
  GND  VCC
```

---

## System Architecture

```
  ┌─────────────────────────────────────────────────────┐
  │                    EARTH (Uno R3/R4)                │
  │                                                     │
  │  Serial Monitor ──► handleSerial()                  │
  │                         │                           │
  │                    resetAntenna(id)                 │
  │                    resetAll()                       │
  │                         │                           │
  │  pingAll() ─────────────┤                           │
  │  (every 500ms)          │                           │
  │                         ▼                           │
  │              sendToAntenna(id, pkt)                 │
  │              openWritingPipe(downlink[id])           │
  │              radio.write() ────────────────────────┼──► Antenna N
  │              radio.read()  ◄───────────────────────┼─── hardware ACK + payload
  └─────────────────────────────────────────────────────┘

  ┌─────────────────────────────────────────────────────┐
  │                  ANTENNA (Nano)                     │
  │                                                     │
  │  radio.startListening()                             │
  │  openReadingPipe(1, downlink[ANTENNA_ID])           │
  │                                                     │
  │  handleIncoming() ◄── receives pkt                  │
  │       │                                             │
  │       ├─ "reset" ──► apply color                    │
  │       │              loadAck("ready")               │
  │       │                                             │
  │       └─ "ping"  ──► loadAck(pendingAck)           │
  │                                                     │
  │  handleSerial() / checkTaskTrigger()                │
  │       └─► loadAck("taskComplete")                   │
  │                                                     │
  │  *** Antenna never transmits ***                    │
  │  All replies travel inside the hardware ACK packet  │
  └─────────────────────────────────────────────────────┘
```

Earth stays permanently in **TX mode** and never calls `startListening()` after setup. Antennas stay permanently in **RX mode** and never call `stopListening()`. All communication from Antenna to Earth travels inside the NRF24L01+ hardware ACK packet — the Antenna sketch never performs a transmission of its own.

---

## How Communication Works

### The ACK Payload Mechanism

The NRF24L01+ has a built-in auto-acknowledgement system: when a receiver gets a packet, its radio hardware automatically sends a tiny ACK packet back to the transmitter to confirm receipt. The RF24 library's `enableAckPayload()` feature allows **data to be piggybacked onto that ACK packet**.

This system uses ACK payloads as the exclusive return channel from Antenna to Earth. The Antenna loads its response into the ACK buffer using `radio.writeAckPayload()`. When Earth's next packet arrives, the Antenna's radio hardware sends that buffer back inside the automatic ACK — without any code in the Antenna sketch initiating a transmission. Earth reads the ACK payload immediately after `radio.write()` returns.

This means:
- There is only **one address pool** — the downlink addresses Earth writes to
- The Antenna **never transmits** — it only loads data into the ACK buffer
- Earth **never enters RX mode** — it reads ACK payloads while staying in TX mode
- There are **no pipe address conflicts** and **no mode-switching race conditions**

### Communication Flow

```
1. BOOT
   Antenna powers on
   └─► loads "ready" into ACK buffer via writeAckPayload()
   └─► stays in RX mode, waiting

2. PING (every 500ms, Earth → each Antenna)
   Earth opens writing pipe to downlinkAddresses[id]
   Earth calls radio.write("ping")
   └─► packet arrives at Antenna
   └─► Antenna's radio hardware auto-sends ACK + payload ("ready", "taskComplete", or "idle")
   └─► Earth reads ACK payload and updates antennaReady / antennaComplete flags
   └─► Antenna reloads pendingAck into buffer for next ping

3. RESET (user command on Earth → Antenna)
   Earth opens writing pipe to downlinkAddresses[id]
   Earth calls radio.write("reset") with RGB color values
   └─► Antenna receives reset, applies color, clears taskDone
   └─► Antenna calls loadAck("ready")
   └─► Earth reads "ready" from ACK payload, logs "Antenna N is READY"
   └─► Earth starts 750ms cooldown to discard any stale "taskComplete" ACKs

4. TASK COMPLETE (Antenna → Earth, via ACK payload)
   Event triggers on Antenna (button press or serial command)
   └─► Antenna calls loadAck("taskComplete") — no transmission occurs
   └─► Next ping from Earth collects "taskComplete" from ACK payload automatically
   └─► Earth sets antennaComplete[id] = true and logs the event

5. ANTENNA OFFLINE DETECTION
   Earth's radio.write() returns false when no ACK is received
   └─► missedPings[id] increments on each failure
   └─► After 3 consecutive misses (~1.5 seconds) flags are cleared and offline is logged
   └─► On reconnection, missedPings resets and "reconnected" is logged
```

### Why Earth Stays in TX Mode

The NRF24L01+ uses **pipe 0** for two purposes: receiving ACK packets after a TX, and general RX when in listening mode. Calling `stopListening()` clears pipe 0. If Earth toggles between TX and RX mode, pipe 0 gets clobbered before each write and the radio never receives the ACK — causing every `radio.write()` to return false even if the Antenna received the packet correctly.

By keeping Earth permanently in TX mode, pipe 0 is always correctly set to the current writing pipe address and ACK reception works reliably on every transmission.

### Stale ACK Cooldown

When Earth resets an Antenna, the Antenna's ACK buffer may still contain `"taskComplete"` from before the reset for a brief moment while it processes the reset packet and calls `loadAck("ready")`. Without protection, the next ping (~500ms later) could collect that stale `"taskComplete"` and incorrectly flip `antennaComplete` back to `true` immediately after clearing it.

Earth tracks `resetTime[id]` and ignores any `"taskComplete"` ACKs received within 750ms of a reset. This window covers at least one full ping cycle, giving the Antenna time to reload its buffer with `"ready"`.

---

## Address Configuration

All addresses are defined as 5-byte arrays. The NRF24L01+ requires exactly 5-byte addresses. There is **one address pool** — the downlink addresses Earth writes to and each Antenna listens on:

```cpp
const uint8_t downlinkAddresses[5][6] = {
  "Ant0D",   // Antenna 0
  "Ant1D",   // Antenna 1
  "Ant2D",   // Antenna 2
  "Ant3D",   // Antenna 3
  "Ant4D"    // Antenna 4
};
```

Each string is 5 printable characters plus a null terminator (hence `[6]`). The RF24 library reads exactly 5 bytes so the null terminator is harmless.

Earth opens its writing pipe to `downlinkAddresses[id]` before each transmission. Each Antenna listens on `downlinkAddresses[ANTENNA_ID]` on pipe 1. The ACK payload travels back through the same pipe automatically — no separate return address is needed.

To distinguish this network from others in the same space, change the address strings consistently across all devices. See [Deploying Multiple Networks](#deploying-multiple-networks).

---

## Packet Structure

Both Earth→Antenna packets and ACK payloads are fixed-size structs. Fixed payload sizes are more reliable than dynamic payloads on the NRF24L01+.

### Downlink Packet (Earth → Antenna)

```cpp
struct Packet {
  char    cmd[16];     // "reset" or "ping"
  uint8_t antennaId;   // target antenna (0–4)
  uint8_t r, g, b;     // RGB color values (used with "reset" command)
  uint8_t padding[12]; // pads struct to 32 bytes (NRF24 max payload)
};
```

### ACK Payload (Antenna → Earth, inside hardware ACK)

```cpp
struct AckPayload {
  char    cmd[16];     // "ready", "taskComplete", or "idle"
  uint8_t antennaId;   // sending antenna ID
  uint8_t padding[15]; // pads struct to 32 bytes
};
```

---

## Earth — Full Behaviour

### Startup

1. Initialises radio with conservative settings (low power, slow data rate, high retries)
2. Enables ACK payloads with `radio.enableAckPayload()`
3. Opens writing pipe to `downlinkAddresses[0]` (updated dynamically before each TX)
4. Calls `radio.stopListening()` — stays in TX mode permanently, never leaves
5. Prints ready message to Serial

### Main Loop

`pingAll()` fires every 500ms, cycling through all 5 Antenna addresses. For each Antenna it calls `sendToAntenna(id, ping)` which:

- Opens the writing pipe to `downlinkAddresses[id]`
- Calls `radio.write()` which transmits and waits for the hardware ACK
- On success: resets `missedPings[id]` to zero, reads and processes any ACK payload
- On failure: increments `missedPings[id]`, clears flags after 3 consecutive misses

`handleSerial()` reads commands from the PC over USB Serial.

### State Tracking

| Variable | Type | Description |
|---|---|---|
| `antennaReady[5]` | `bool` | True after Antenna reports ready |
| `antennaComplete[5]` | `bool` | True after Antenna reports taskComplete |
| `loggedReady[5]` | `bool` | Suppresses repeated "is READY" log messages |
| `resetTime[5]` | `uint32_t` | Timestamp of last reset per Antenna, used for stale ACK cooldown |
| `missedPings[5]` | `uint8_t` | Consecutive missed ping counter per Antenna |

### Radio Settings

| Setting | Value | Reason |
|---|---|---|
| PA Level | `RF24_PA_LOW` | Reduces current draw, more stable without decoupling capacitors |
| Data Rate | `RF24_250KBPS` | Slowest rate = best sensitivity and link reliability |
| Channel | 108 | Above the 2.4GHz WiFi band (channels 1–13 = 2.401–2.483GHz) |
| Retries | 15 delay, 15 count | Maximum hardware auto-retransmit attempts before reporting failure |
| Payload Size | 32 bytes (fixed) | More stable than dynamic payloads on clone modules |
| CRC | 16-bit | Strongest built-in error detection |

---

## Antenna — Full Behaviour

### Startup

1. Initialises radio with identical settings to Earth
2. Enables ACK payloads with `radio.enableAckPayload()`
3. Opens reading pipe 1 to `downlinkAddresses[ANTENNA_ID]`
4. Calls `radio.startListening()` — stays in RX mode permanently, never leaves
5. Calls `loadAck("ready")` to pre-load the ACK buffer
6. Prints ready message to Serial

### Main Loop

`handleIncoming()` checks for packets from Earth. On a valid packet:

- **"reset"**: stores RGB color values, clears `taskDone`, calls `loadAck("ready")`
- **"ping"**: calls `loadAck(pendingAck)` to reload current state into the buffer for the next ping

`checkTaskTrigger()` watches pin 2 (active LOW with `INPUT_PULLUP`). On a falling edge it sets `taskDone = true` and calls `loadAck("taskComplete")`.

`handleSerial()` allows testing via the Nano's serial monitor.

### ACK Buffer

`loadAck(cmd)` writes an `AckPayload` struct into the radio's ACK buffer for pipe 1 using `radio.writeAckPayload(1, &ack, sizeof(AckPayload))` and updates `pendingAck`. The buffer holds one payload — it is consumed when Earth next pings this Antenna. `loadAck` must be called after every received packet to replenish the buffer, which is why both the `"reset"` and `"ping"` handlers always call it before returning.

The Antenna sketch never calls `radio.write()`, `radio.stopListening()`, or `radio.startListening()` after setup. All communication back to Earth is handled entirely by the NRF24L01+ hardware.

---

## Serial Commands

### Earth (connected to PC via USB)

| Command | Description |
|---|---|
| `reset all` | Sends reset with a random RGB color to all 5 Antennas |
| `reset 0` – `reset 4` | Sends reset with a random RGB color to a single Antenna |
| `status` | Prints ready, complete, and missed ping count for all 5 Antennas |

### Antenna (via Nano serial monitor)

| Command | Description |
|---|---|
| `task` or `taskComplete` | Loads taskComplete into ACK buffer — collected by Earth on next ping |
| `status` | Prints this Antenna's ID, pendingAck value, and taskDone flag |

---

## Deploying Multiple Networks

If two or more Earth/Antenna networks operate in the same physical space, they must use different address sets and different radio channels to avoid interference.

### Step 1 — Change the channel

In both Earth and Antenna sketches, change `radio.setChannel(108)` to a unique value per network. Valid range is 0–125. Suggested values that avoid WiFi overlap: 90, 95, 100, 105, 108.

### Step 2 — Change the addresses

Replace the address strings with a unique 5-character set per network in **both** Earth and Antenna sketches:

```cpp
// Network A
const uint8_t downlinkAddresses[5][6] = {
  "A0Dwn", "A1Dwn", "A2Dwn", "A3Dwn", "A4Dwn"
};

// Network B
const uint8_t downlinkAddresses[5][6] = {
  "B0Dwn", "B1Dwn", "B2Dwn", "B3Dwn", "B4Dwn"
};
```

> **Important:** Addresses must be exactly 5 characters. The NRF24L01+ uses 5-byte addressing and the RF24 library reads exactly 5 bytes from the array.

### Step 3 — Re-flash all devices in the network

Every Earth and every Antenna in a given network must share the same address set and channel. An Antenna from Network A will not respond to an Earth from Network B — the hardware address filter silently discards any packet not addressed to an open pipe.

### Verifying Isolation

You can create a small test sketch (for example, `impostor_test.ino`) to verify that Earth only responds to addresses in its pool. Flash it onto a spare Nano using a different address set (e.g. `"Imp0D"`) on the same channel. Type `ping` in its serial monitor — Earth should never ACK it. The filtering is hardware-enforced: Earth's radio only ACKs packets received on addresses it has open, and `"Imp0D"` is never opened.

---

## Troubleshooting

### Reset FAILED (no ack)

Earth sent a packet but received no ACK from the Antenna.

- Verify the Antenna is powered and its serial monitor shows it is listening
- Confirm `ANTENNA_ID` in the Antenna sketch matches the ID in the reset command
- Confirm `downlinkAddresses` strings are identical character-for-character in both sketches
- Confirm `radio.setChannel()` is the same value in both sketches
- Check wiring — MOSI→pin 11, MISO→pin 12, not swapped
- Move devices closer together — `RF24_PA_LOW` has limited range

### NRF24 init FAILED

`radio.begin()` returned false.

- Confirm VCC is connected to **3.3V**, not 5V
- Check all 6 SPI wires are connected and firmly seated
- Try a different NRF24L01+ module if available

### Ready flag never clears when Antenna loses power

The missed ping counter handles this. After 3 consecutive failed pings (~1.5 seconds) Earth clears all flags for that Antenna and logs it as offline. If the threshold feels too slow, reduce `MISSED_PING_LIMIT`.

### taskComplete flag flips back to 1 after reset

The stale ACK cooldown (`RESET_COOLDOWN_MS = 750`) should prevent this. If it still occurs, increase the cooldown value. The Nano may be slow to reload its ACK buffer if it has heavy work in `loop()`.

### ACK payload is always empty after ping

The Antenna's ACK buffer must be reloaded after every received packet. If `loadAck()` is not called inside both the `"reset"` and `"ping"` handlers, the buffer empties after the first ping and Earth receives no payload on subsequent pings.

### Only some Antennas respond

Each Antenna must have a unique `ANTENNA_ID` (0–4). Two Antennas sharing the same ID will both listen on the same pipe address — both will receive packets addressed to that ID but only one will ACK, causing unpredictable behaviour.

### Intermittent failures under load

Without decoupling capacitors on the NRF24L01+ VCC and GND pins, voltage spikes during transmission can cause the module to brown out. A 10µF electrolytic capacitor placed as close as possible to the module pins resolves most intermittent issues. A 100nF ceramic cap in parallel provides additional high-frequency filtering.

---

## Key Design Decisions

### Single Address Pool, One Direction Only

The final architecture has only one set of addresses — the downlink addresses Earth writes to. There is no uplink address pool. All communication from Antenna to Earth travels inside the hardware ACK packet on the same downlink address, so no second set of addresses is needed and no separate uplink transmission ever occurs.

### ACK Payloads as the Exclusive Return Channel

Using a dedicated uplink address pool required Earth to switch between TX and RX modes. The NRF24L01+ shares pipe 0 between TX ACK reception and general RX, which causes address conflicts whenever the mode is toggled — every `radio.write()` fails even if the Antenna received the packet. ACK payloads solve this entirely. Earth never leaves TX mode, pipe 0 is always correctly configured, and the Antenna never needs to transmit.

### Earth Permanently in TX Mode

Early versions toggled Earth between TX and RX mode on every loop to listen for incoming messages. This reliably caused `radio.write()` to fail because `stopListening()` clears pipe 0 before each write. A ping test that never called `startListening()` worked perfectly on the first try — confirming the mode switching was the cause. Earth was restructured to call `stopListening()` once in `setup()` and never again.

### Fixed Payload Sizes

Dynamic payload sizes (`enableDynamicPayloads()`) are convenient but less reliable on clone NRF24L01+ modules. Fixed 32-byte payloads with padding are more predictable and consistent across all module variants.

### Missed Ping Counter for Offline Detection

Rather than a simple timeout, a consecutive miss counter (`missedPings[id]`) avoids false positives from single dropped packets. The NRF24L01+ hardware already retries 15 times per `write()` call, so a genuine miss means the Antenna is truly unreachable. Three consecutive misses (~1.5 seconds) is the threshold before flags are cleared.

### Per-Antenna Reset Cooldown

Each Antenna has its own `resetTime[id]` timestamp rather than a global delay. This allows `reset all` to fire quickly across all Antennas in sequence without one slow Antenna blocking the others, while still correctly discarding stale ACK payloads on each individual Antenna independently.

---

## Development History

The final architecture was reached after several failed approaches, each revealing a deeper problem with the NRF24L01+ hardware.

**Attempt 1 — Shared address for both directions.** The Antenna used its own pipe address for both receiving commands from Earth and sending messages back. When Earth called `openWritingPipe()` with that address it overwrote pipe 0, destroying its ability to receive ACKs. Every `write()` failed.

**Attempt 2 — Separate uplink and downlink address pools.** Two address pools were introduced — one for Earth→Antenna and one for Antenna→Earth. Earth toggled between TX and RX mode each loop to listen on the uplink pipes. This still failed because `stopListening()` clears pipe 0 before each write, corrupting the radio state at the moment of transmission.

**Attempt 3 — Permanent TX mode with separate uplink pool.** Earth was locked into TX mode permanently. This fixed the downlink (reset commands arrived at the Antenna) but the uplink address pool became unreachable since Earth was never listening — the ready and taskComplete messages were lost.

**Final solution — ACK payloads, single address pool.** The separate uplink address pool was eliminated entirely. The Antenna loads its responses into the ACK buffer using `writeAckPayload()`. The NRF24L01+ hardware delivers that buffer back to Earth inside the automatic ACK on every ping — no Antenna transmission, no mode switching, no pipe conflicts. Earth stays in TX mode permanently and reads ACK payloads after each `radio.write()`.
