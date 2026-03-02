#ifndef FIELD8_H
#define FIELD8_H

#include <Arduino.h>

// Field 8: suffix 'H', unique radio channel
const uint8_t downlinkAddresses[5][6] = {
  "Ant0H",
  "Ant1H",
  "Ant2H",
  "Ant3H",
  "Ant4H"
};

const uint8_t RADIO_CHANNEL = 120;

#endif

