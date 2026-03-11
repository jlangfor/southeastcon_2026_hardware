#ifndef FIELD4_H
#define FIELD4_H

#include <Arduino.h>

// Field 4: suffix 'D', unique radio channel
const uint8_t downlinkAddresses[5][6] = {
  "Ant0D",
  "Ant1D",
  "Ant2D",
  "Ant3D",
  "Ant4D"
};

const uint8_t RADIO_CHANNEL = 105;

#endif

