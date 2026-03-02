#ifndef FIELD3_H
#define FIELD3_H

#include <Arduino.h>

// Field 3: suffix 'C', unique radio channel
const uint8_t downlinkAddresses[5][6] = {
  "Ant0C",
  "Ant1C",
  "Ant2C",
  "Ant3C",
  "Ant4C"
};

const uint8_t RADIO_CHANNEL = 100;

#endif

