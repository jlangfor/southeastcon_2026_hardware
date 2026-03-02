#include <SPI.h>
#include <RF24.h>

// Select the field configuration header (Field1.h .. Field10.h)
#include "Field1.h"

#define CE_PIN  9
#define CSN_PIN 10

#define ANTENNA_ID 1  // !! Change per Nano: Range 0 - 4 !! //

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

uint8_t myColor[3]     = {0, 0, 0};
bool    taskDone       = false;
char    pendingAck[16] = "idle";

// ── ACK loader ─────────────────────────────────────────────

void loadAck(const char* cmd) {
  AckPayload ack = {};
  strncpy(ack.cmd, cmd, 15);
  ack.antennaId = ANTENNA_ID;
  radio.writeAckPayload(1, &ack, sizeof(AckPayload));
  strncpy(pendingAck, cmd, 15);
}

// ── Incoming handler ───────────────────────────────────────

void handleIncoming() {
  while (radio.available()) {
    Packet pkt = {};
    radio.read(&pkt, sizeof(Packet));

    if (pkt.antennaId != ANTENNA_ID) {
      loadAck(pendingAck);
      return;
    }

    if (strcmp(pkt.cmd, "reset") == 0) {
      myColor[0] = pkt.r;
      myColor[1] = pkt.g;
      myColor[2] = pkt.b;

      Serial.print(F("[Antenna] Reset! Color=RGB("));
      Serial.print(myColor[0]); Serial.print(',');
      Serial.print(myColor[1]); Serial.print(',');
      Serial.print(myColor[2]); Serial.println(')');

      // Apply color to hardware here...

      taskDone = false;
      loadAck("ready");
    }
    else if (strcmp(pkt.cmd, "ping") == 0) {
      loadAck(pendingAck);
    }
  }
}

// ── Serial (test taskComplete from monitor) ────────────────

void handleSerial() {
  static String buf = "";
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      buf.trim();
      if (buf.length() == 0) { buf = ""; return; }

      if (buf.equalsIgnoreCase("task") || buf.equalsIgnoreCase("taskComplete")) {
        if (!taskDone) {
          taskDone = true;
          loadAck("taskComplete");
          Serial.println(F("[Antenna] taskComplete queued — Earth will collect on next ping"));
        } else {
          Serial.println(F("[Antenna] Task already complete, reset first"));
        }
      }
      else if (buf.equalsIgnoreCase("status")) {
        Serial.print(F("[Antenna] ID="));
        Serial.print(ANTENNA_ID);
        Serial.print(F(" pendingAck="));
        Serial.print(pendingAck);
        Serial.print(F(" taskDone="));
        Serial.println(taskDone);
      }
      else {
        Serial.println(F("[Antenna] Commands: task | status"));
      }
      buf = "";
    } else {
      buf += c;
    }
  }
}

// ── Task trigger example (button on pin 2) ─────────────────────────

void checkTaskTrigger() {
  static bool lastBtn = HIGH;
  bool btn = digitalRead(2);
  if (btn == LOW && lastBtn == HIGH && !taskDone) {
    taskDone = true;
    loadAck("taskComplete");
    Serial.println(F("[Antenna] Task triggered via button"));
  }
  lastBtn = btn;
}

// ── Setup & Loop ───────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  pinMode(2, INPUT_PULLUP);
  delay(100);

  if (!radio.begin()) {
    Serial.println(F("[Antenna] NRF24 init FAILED. Check wiring!"));
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

  radio.openReadingPipe(1, downlinkAddresses[ANTENNA_ID]);
  radio.startListening();

  loadAck("ready");

  Serial.print(F("[Antenna "));
  Serial.print(ANTENNA_ID);
  Serial.println(F("] Online. Commands: task | status"));
}

void loop() {
  handleIncoming();
  handleSerial();
  checkTaskTrigger();
}