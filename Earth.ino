#include <SPI.h>
#include <RF24.h>

// Select the field configuration header (Field1.h .. Field10.h)
#include "Field1.h"

#define CE_PIN  9
#define CSN_PIN 10

RF24 radio(CE_PIN, CSN_PIN);

struct Packet {
  char    cmd[16];
  uint8_t antennaId;
  uint8_t r, g, b;
  uint8_t padding[12];
};

struct AckPayload {
  char    cmd[16];
  uint8_t antennaId;
  uint8_t padding[15];
};

bool     antennaReady[5]      = {false};
bool     antennaComplete[5]   = {false};
bool     loggedReady[5]       = {false};
uint32_t resetTime[5]         = {0};
uint8_t  missedPings[5]       = {0};       // consecutive missed ping counter
#define  MISSED_PING_LIMIT    3            // mark offline after this many misses
#define  PING_INTERVAL_MS     500
#define  RESET_COOLDOWN_MS    750

uint32_t lastPingTime = 0;

// ── TX with ACK payload read ───────────────────────────────

bool sendToAntenna(uint8_t id, Packet &pkt) {
  radio.openWritingPipe(downlinkAddresses[id]);
  delay(5);
  bool ok = radio.write(&pkt, sizeof(Packet));

  if (ok) {
    // Packet was ACKed — Antenna is alive
    if (missedPings[id] >= MISSED_PING_LIMIT) {
      // Was previously marked offline, log that it's back
      Serial.print(F("[Earth] Antenna "));
      Serial.print(id);
      Serial.println(F(" reconnected"));
    }
    missedPings[id] = 0;

    if (radio.isAckPayloadAvailable()) {
      AckPayload ack = {};
      radio.read(&ack, sizeof(AckPayload));

      if (strcmp(ack.cmd, "ready") == 0) {
        if (!antennaReady[id]) {
          antennaReady[id] = true;
          Serial.print(F("[Earth] Antenna "));
          Serial.print(id);
          Serial.println(F(" is READY"));
        }
        loggedReady[id] = true;
      }
      else if (strcmp(ack.cmd, "taskComplete") == 0) {
        bool inCooldown = (millis() - resetTime[id]) < RESET_COOLDOWN_MS;
        if (!antennaComplete[id] && !inCooldown) {
          antennaComplete[id] = true;
          Serial.print(F("[Earth] Antenna "));
          Serial.print(id);
          Serial.println(F(" taskComplete"));
        }
      }
    }
  } else {
    // No ACK received — increment miss counter
    missedPings[id]++;

    if (missedPings[id] == MISSED_PING_LIMIT) {
      // Just crossed the threshold — mark offline and clear flags
      antennaReady[id]    = false;
      antennaComplete[id] = false;
      loggedReady[id]     = false;
      Serial.print(F("[Earth] Antenna "));
      Serial.print(id);
      Serial.println(F(" offline — flags cleared"));
    }
  }

  return ok;
}

// ── Ping all Antennas periodically ────────────────────────

void pingAll() {
  if (millis() - lastPingTime < PING_INTERVAL_MS) return;
  lastPingTime = millis();

  Packet ping = {};
  strcpy(ping.cmd, "ping");

  for (uint8_t i = 0; i < 5; i++) {
    ping.antennaId = i;
    sendToAntenna(i, ping);
    delay(10);
  }
}

// ── Commands ───────────────────────────────────────────────

void resetAntenna(uint8_t id) {
  Packet pkt = {};
  strcpy(pkt.cmd, "reset");
  pkt.antennaId = id;
  pkt.r = random(0, 256);
  pkt.g = random(0, 256);
  pkt.b = random(0, 256);

  Serial.print(F("[Earth] Sending reset to Antenna "));
  Serial.print(id);
  Serial.print(F(" color=RGB("));
  Serial.print(pkt.r); Serial.print(',');
  Serial.print(pkt.g); Serial.print(',');
  Serial.print(pkt.b); Serial.println(')');

  antennaReady[id]    = false;
  antennaComplete[id] = false;
  loggedReady[id]     = false;
  resetTime[id]       = millis();
  missedPings[id]     = 0;              // reset miss counter on manual reset

  Serial.print(F("[Earth] Antenna "));
  Serial.print(id);
  Serial.println(F(" state cleared"));

  if (sendToAntenna(id, pkt)) {
    Serial.println(F("[Earth] Reset sent OK"));
  } else {
    Serial.println(F("[Earth] Reset FAILED (no ack)"));
  }
}

void resetAll() {
  for (uint8_t i = 0; i < 5; i++) {
    resetAntenna(i);
    delay(50);
  }
}

// ── Serial ─────────────────────────────────────────────────

void handleSerial() {
  static String buf = "";
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      buf.trim();
      if (buf.length() == 0) { buf = ""; return; }

      if (buf.equalsIgnoreCase("reset all")) {
        resetAll();
      }
      else if (buf.startsWith("reset ")) {
        int id = buf.substring(6).toInt();
        if (id >= 0 && id < 5) {
          resetAntenna((uint8_t)id);
        } else {
          Serial.println(F("[Earth] Invalid antenna ID (0-4)"));
        }
      }
      else if (buf.equalsIgnoreCase("status")) {
        Serial.println(F("=== Antenna Status ==="));
        for (uint8_t i = 0; i < 5; i++) {
          Serial.print(F("  Antenna ")); Serial.print(i);
          Serial.print(F(": ready="));   Serial.print(antennaReady[i]);
          Serial.print(F(" complete=")); Serial.print(antennaComplete[i]);
          Serial.print(F(" missed="));   Serial.println(missedPings[i]);
        }
      }
      else {
        Serial.println(F("[Earth] Commands: reset all | reset <0-4> | status"));
      }
      buf = "";
    } else {
      buf += c;
    }
  }
}

// ── Setup & Loop ───────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(A0));
  delay(100);

  if (!radio.begin()) {
    Serial.println(F("[Earth] NRF24 init FAILED. Check wiring!"));
    while (1);
  }

  radio.setPALevel(RF24_PA_LOW);
  radio.setDataRate(RF24_250KBPS);
  radio.setChannel(RADIO_CHANNEL);
  radio.setRetries(15, 15);
  radio.setPayloadSize(sizeof(Packet));
  radio.setCRCLength(RF24_CRC_16);
  radio.enableAckPayload();
  radio.setAutoAck(true);

  radio.openWritingPipe(downlinkAddresses[0]);
  radio.stopListening();

  Serial.println(F("[Earth] Online. Commands: reset all | reset <0-4> | status"));
}

void loop() {
  pingAll();
  handleSerial();
}