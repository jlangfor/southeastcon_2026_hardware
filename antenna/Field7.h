#ifndef FIELD7_H
#define FIELD7_H

#include <Arduino.h>

// Field 7: suffix 'G', unique radio channel
const uint8_t downlinkAddresses[5][6] = {
  "Ant0G",
  "Ant1G",
  "Ant2G",
  "Ant3G",
  "Ant4G"
};

const uint8_t RADIO_CHANNEL = 115;

#endif

