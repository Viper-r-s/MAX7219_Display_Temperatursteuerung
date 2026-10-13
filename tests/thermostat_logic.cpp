#include <cassert>
#include "../MAX7219_Display1/ThermostatLogic.h"

int main() {
  // Heizluefter: bei Soll 8 C und Hysterese 2 C: Ein <= 6, Aus >= 8.
  assert(!nextRelayState(false, 6.1f, 8.0f, 2.0f, true, false));
  assert(nextRelayState(false, 6.0f, 8.0f, 2.0f, true, false));
  assert(nextRelayState(true, 7.0f, 8.0f, 2.0f, true, false));
  assert(!nextRelayState(true, 8.0f, 8.0f, 2.0f, true, false));
  assert(!nextRelayState(true, 5.0f, 8.0f, 2.0f, false, false));

  // Auch bei der optionalen Kuehl-Konfiguration stimmen die Schaltschwellen.
  assert(!nextRelayState(false, 9.9f, 8.0f, 2.0f, true, true));
  assert(nextRelayState(false, 10.0f, 8.0f, 2.0f, true, true));
  assert(nextRelayState(true, 9.0f, 8.0f, 2.0f, true, true));
  assert(!nextRelayState(true, 8.0f, 8.0f, 2.0f, true, true));
}
