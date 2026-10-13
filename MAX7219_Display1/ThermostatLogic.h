#ifndef THERMOSTAT_LOGIC_H
#define THERMOSTAT_LOGIC_H

// Hysterese ist die Differenz zwischen Ein- und Ausschalttemperatur.
// Die Schaltschwellen liegen symmetrisch um den Sollwert.
// Bei 2 C Hysterese: Heizen Ein bei Soll - 1 C, Aus bei Soll + 1 C.
inline bool nextRelayState(bool currentlyOn, float actual, float target,
                           float hysteresis, bool sensorValid, bool cooling) {
  if (!sensorValid) return false;
  const float halfBand = hysteresis / 2.0f;
  if (cooling) {
    return currentlyOn ? actual > target - halfBand : actual >= target + halfBand;
  }
  return currentlyOn ? actual < target + halfBand : actual <= target - halfBand;
}

#endif
