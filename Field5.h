#ifndef FIELD5_H
#define FIELD5_H

#include <Arduino.h>

// Field 5: suffix 'E', unique radio channel
const uint8_t downlinkAddresses[5][6] = {
  "Ant0E",
  "Ant1E",
  "Ant2E",
  "Ant3E",
  "Ant4E"
};

const uint8_t RADIO_CHANNEL = 108;

#endif

