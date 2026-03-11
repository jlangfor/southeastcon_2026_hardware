#ifndef FIELD9_H
#define FIELD9_H

#include <Arduino.h>

// Field 9: suffix 'I', unique radio channel
const uint8_t downlinkAddresses[5][6] = {
  "Ant0I",
  "Ant1I",
  "Ant2I",
  "Ant3I",
  "Ant4I"
};

const uint8_t RADIO_CHANNEL = 122;

#endif

