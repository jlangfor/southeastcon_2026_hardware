#ifndef FIELD6_H
#define FIELD6_H

#include <Arduino.h>

// Field 6: suffix 'F', unique radio channel
const uint8_t downlinkAddresses[5][6] = {
  "Ant0F",
  "Ant1F",
  "Ant2F",
  "Ant3F",
  "Ant4F"
};

const uint8_t RADIO_CHANNEL = 110;

#endif

