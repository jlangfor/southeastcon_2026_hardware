#ifndef FIELD2_H
#define FIELD2_H

#include <Arduino.h>

// Field 2: suffix 'B', unique radio channel
const uint8_t downlinkAddresses[5][6] = {
  "Ant0B",
  "Ant1B",
  "Ant2B",
  "Ant3B",
  "Ant4B"
};

const uint8_t RADIO_CHANNEL = 95;

#endif

