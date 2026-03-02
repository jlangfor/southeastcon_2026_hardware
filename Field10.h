#ifndef FIELD10_H
#define FIELD10_H

#include <Arduino.h>

// Field 10: suffix 'J', unique radio channel
const uint8_t downlinkAddresses[5][6] = {
  "Ant0J",
  "Ant1J",
  "Ant2J",
  "Ant3J",
  "Ant4J"
};

const uint8_t RADIO_CHANNEL = 125;

#endif

