#ifndef FIELD1_H
#define FIELD1_H

#include <Arduino.h>

// Field 1: suffix 'A', unique radio channel
const uint8_t downlinkAddresses[5][6] = {
  "Ant0A",
  "Ant1A",
  "Ant2A",
  "Ant3A",
  "Ant4A"
};

const uint8_t RADIO_CHANNEL = 90;

#endif

