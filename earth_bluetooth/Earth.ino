// ============================================================
// Earth
// ============================================================
//
// MULTI-INSTANCE SETUP
// ─────────────────────
// Before flashing each board, set INSTANCE_ID to a unique
// value: 1, 2, 3, or 4.
//
// Each instance gets a unique BLE device name and UUIDs so
// nearby units running simultaneously cannot interfere with
// each other.
//
//   Instance 1 → "Arduino-R4-1"  UUIDs: 1234567[1]-...
//   Instance 2 → "Arduino-R4-2"  UUIDs: 1234567[2]-...
//   Instance 3 → "Arduino-R4-3"  UUIDs: 1234567[3]-...
//   Instance 4 → "Arduino-R4-4"  UUIDs: 1234567[4]-...
//
// The matching config.json on the Python side must have the
// same instance_id value.
//
// ── BLE Protocol ─────────────────────────────────────────────
//   RX Characteristic : receives "START" / "STOP" from Python
//   TX Characteristic : sends JSON status updates to Python
//
// ── IR Sensor ────────────────────────────────────────────────
//   Placeholder simulates 4 antenna signals.
//   Replace placeholder block with real IR receiver logic
//   when hardware is available.
//
// Countdown : 3 minutes (180 seconds)
// Sends status on change OR every STATUS_INTERVAL_MS ms
// ============================================================

#include <ArduinoBLE.h>

// ╔══════════════════════════════════════════════════════════╗
// ║  CHANGE THIS VALUE (1, 2, 3, or 4) FOR EACH BOARD       ║
#define INSTANCE_ID 1
// ╚══════════════════════════════════════════════════════════╝

// ── Derive UUIDs and device name from INSTANCE_ID ───────────
//
// UUID pattern: 1234567X-1234-1234-1234-12345678901Y
//   X = INSTANCE_ID digit  (makes service/chars unique per unit)
//   Y = characteristic role (2=service, 3=RX, 4=TX)
//
// These are assembled at runtime in setup() using the macros
// below so the compiler can validate INSTANCE_ID range.

#if   INSTANCE_ID == 1
  #define SERVICE_UUID  "12345671-1234-1234-1234-123456789012"
  #define RX_UUID       "12345671-1234-1234-1234-123456789013"
  #define TX_UUID       "12345671-1234-1234-1234-123456789014"
  #define DEVICE_NAME   "Arduino-R4-1"
#elif INSTANCE_ID == 2
  #define SERVICE_UUID  "12345672-1234-1234-1234-123456789012"
  #define RX_UUID       "12345672-1234-1234-1234-123456789013"
  #define TX_UUID       "12345672-1234-1234-1234-123456789014"
  #define DEVICE_NAME   "Arduino-R4-2"
#elif INSTANCE_ID == 3
  #define SERVICE_UUID  "12345673-1234-1234-1234-123456789012"
  #define RX_UUID       "12345673-1234-1234-1234-123456789013"
  #define TX_UUID       "12345673-1234-1234-1234-123456789014"
  #define DEVICE_NAME   "Arduino-R4-3"
#elif INSTANCE_ID == 4
  #define SERVICE_UUID  "12345674-1234-1234-1234-123456789012"
  #define RX_UUID       "12345674-1234-1234-1234-123456789013"
  #define TX_UUID       "12345674-1234-1234-1234-123456789014"
  #define DEVICE_NAME   "Arduino-R4-4"
#else
  #error "INSTANCE_ID must be 1, 2, 3, or 4"
#endif

// ── Timing ───────────────────────────────────────────────────
#define COUNTDOWN_SECONDS   180     // 3-minute session
#define STATUS_INTERVAL_MS  500     // Periodic status send interval
#define SIM_CHANGE_INTERVAL 4000    // Placeholder: simulate IR change every N ms

// ── Antenna count ────────────────────────────────────────────
#define NUM_ANTENNAS 4

// ── BLE objects ──────────────────────────────────────────────
BLEService              commsService(SERVICE_UUID);
BLEStringCharacteristic rxCharacteristic(RX_UUID, BLEWrite | BLEWriteWithoutResponse, 32);
BLEStringCharacteristic txCharacteristic(TX_UUID, BLERead | BLENotify, 128);

// ── Session state ─────────────────────────────────────────────
bool     sessionActive    = false;
uint32_t sessionStartMs   = 0;
uint32_t lastStatusSendMs = 0;
uint32_t lastSimChangeMs  = 0;    // Placeholder only

bool antennaState[NUM_ANTENNAS]  = {false, false, false, false};
bool lastSentState[NUM_ANTENNAS] = {false, false, false, false};

// ── IR pin definitions (real hardware) ───────────────────────
// Uncomment and set correct pins when hardware is available:
// const int IR_PINS[NUM_ANTENNAS] = {2, 3, 4, 5};


// ============================================================
// setup()
// ============================================================
void setup() {
  Serial.begin(9600);
  while (!Serial);

  Serial.print("[INIT] Instance ID : ");
  Serial.println(INSTANCE_ID);
  Serial.print("[INIT] Device name : ");
  Serial.println(DEVICE_NAME);
  Serial.print("[INIT] Service UUID: ");
  Serial.println(SERVICE_UUID);

  // Real hardware: uncomment when IR receiver is wired up
  // for (int i = 0; i < NUM_ANTENNAS; i++) {
  //   pinMode(IR_PINS[i], INPUT_PULLUP);
  // }

  if (!BLE.begin()) {
    Serial.println("[ERROR] BLE failed to start!");
    while (1);
  }

  BLE.setLocalName(DEVICE_NAME);
  BLE.setAdvertisedService(commsService);
  commsService.addCharacteristic(rxCharacteristic);
  commsService.addCharacteristic(txCharacteristic);
  BLE.addService(commsService);

  txCharacteristic.writeValue("{\"event\":\"ready\"}");
  BLE.advertise();

  Serial.print("[BLE] Advertising as '");
  Serial.print(DEVICE_NAME);
  Serial.println("'...");
}


// ============================================================
// loop()
// ============================================================
void loop() {
  BLEDevice central = BLE.central();

  if (central) {
    Serial.print("[BLE] Connected: ");
    Serial.println(central.address());

    sendJson("connected", -1, "");

    while (central.connected()) {

      // ── Handle incoming commands ──────────────────────────
      if (rxCharacteristic.written()) {
        String cmd = rxCharacteristic.value();
        cmd.trim();
        cmd.toUpperCase();
        Serial.print("[CMD] Received: ");
        Serial.println(cmd);

        if (cmd == "START") {
          startSession();
        } else if (cmd == "STOP") {
          stopSession("stopped");
        }
      }

      // ── Active session logic ──────────────────────────────
      if (sessionActive) {
        uint32_t now     = millis();
        uint32_t elapsed = (now - sessionStartMs) / 1000;
        int remaining    = COUNTDOWN_SECONDS - (int)elapsed;

        if (remaining <= 0) {
          stopSession("timeout");
          continue;
        }

        readIRSensors();

        bool changed = false;
        for (int i = 0; i < NUM_ANTENNAS; i++) {
          if (antennaState[i] != lastSentState[i]) {
            changed = true;
            break;
          }
        }

        if (changed || (now - lastStatusSendMs >= STATUS_INTERVAL_MS)) {
          sendStatus(remaining);
          for (int i = 0; i < NUM_ANTENNAS; i++) {
            lastSentState[i] = antennaState[i];
          }
          lastStatusSendMs = now;
        }
      }

    } // end while connected

    Serial.println("[BLE] Disconnected.");
    if (sessionActive) stopSession("disconnected");
  }
}


// ============================================================
// startSession()
// ============================================================
void startSession() {
  Serial.println("[SESSION] Starting 3-minute countdown.");

  sessionActive    = true;
  sessionStartMs   = millis();
  lastStatusSendMs = millis();
  lastSimChangeMs  = millis();

  for (int i = 0; i < NUM_ANTENNAS; i++) {
    antennaState[i]  = false;
    lastSentState[i] = false;
  }

  sendJson("started", COUNTDOWN_SECONDS, "");
}


// ============================================================
// stopSession()
// ============================================================
void stopSession(const char* reason) {
  Serial.print("[SESSION] Stopped. Reason: ");
  Serial.println(reason);

  sessionActive = false;
  sendJson("stopped", -1, reason);
}


// ============================================================
// readIRSensors()
//
// ── REAL HARDWARE ────────────────────────────────────────────
// When your IR receiver is connected, uncomment the real block
// and delete the placeholder block below.
//
// Typical wiring: IR receiver OUT → digital pin (INPUT_PULLUP)
// Active LOW: signal is LOW when triggered, HIGH when idle.
// ============================================================
void readIRSensors() {

  // ── REAL HARDWARE (uncomment when ready) ─────────────────
  // for (int i = 0; i < NUM_ANTENNAS; i++) {
  //   antennaState[i] = (digitalRead(IR_PINS[i]) == LOW);
  // }

  // ── PLACEHOLDER — delete when real hardware is connected ─
  uint32_t now = millis();
  if (now - lastSimChangeMs >= SIM_CHANGE_INTERVAL) {
    int antenna = random(0, NUM_ANTENNAS);
    antennaState[antenna] = !antennaState[antenna];
    Serial.print("[SIM] Antenna ");
    Serial.print(antenna + 1);
    Serial.print(" → ");
    Serial.println(antennaState[antenna] ? "ACTIVE" : "IDLE");
    lastSimChangeMs = now;
  }
  // ── END PLACEHOLDER ──────────────────────────────────────
}


// ============================================================
// sendStatus() — full antenna state + remaining time
// {"event":"status","instance":N,"remaining":N,"antennas":[...]}
// ============================================================
void sendStatus(int remaining) {
  String msg = "{\"event\":\"status\",\"instance\":";
  msg += INSTANCE_ID;
  msg += ",\"remaining\":";
  msg += remaining;
  msg += ",\"antennas\":[";
  for (int i = 0; i < NUM_ANTENNAS; i++) {
    msg += (antennaState[i] ? "1" : "0");
    if (i < NUM_ANTENNAS - 1) msg += ",";
  }
  msg += "]}";

  txCharacteristic.writeValue(msg);
  Serial.print("[TX] ");
  Serial.println(msg);
}


// ============================================================
// sendJson() — simple event message
// ============================================================
void sendJson(const char* event, int remaining, const char* reason) {
  String msg = "{\"event\":\"";
  msg += event;
  msg += "\",\"instance\":";
  msg += INSTANCE_ID;
  if (remaining >= 0) {
    msg += ",\"remaining\":";
    msg += remaining;
  }
  if (strlen(reason) > 0) {
    msg += ",\"reason\":\"";
    msg += reason;
    msg += "\"";
  }
  msg += "}";

  txCharacteristic.writeValue(msg);
  Serial.print("[TX] ");
  Serial.println(msg);
}
