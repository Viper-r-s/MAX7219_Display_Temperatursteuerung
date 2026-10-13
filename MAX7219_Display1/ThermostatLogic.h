#ifndef THERMOSTAT_LOGIC_H
#define THERMOSTAT_LOGIC_H

// Hysterese ist die Differenz zwischen Ein- und Ausschalttemperatur.
// Kuehlen: Ein bei Soll + Hysterese, Aus bei Soll.
// Heizen: Ein bei Soll - Hysterese, Aus bei Soll.
inline bool nextRelayState(bool currentlyOn, float actual, float target,
                           float hysteresis, bool sensorValid, bool cooling) {
  if (!sensorValid) return false;
  if (cooling) {
    return currentlyOn ? actual > target : actual >= target + hysteresis;
  }
  return currentlyOn ? actual < target : actual <= target - hysteresis;
}

#endif
