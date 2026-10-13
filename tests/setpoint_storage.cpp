#include <cassert>
#include "../MAX7219_Display1/SetpointStorage.h"

int main() {
  StoredSetpoint saved = makeStoredSetpoint(12, false);
  assert(validStoredSetpoint(saved));
  assert(saved.degrees == 12);
  assert(saved.pendingSmartHomeSync == 0);

  StoredSetpoint offlineChange = makeStoredSetpoint(15, true);
  assert(validStoredSetpoint(offlineChange));
  assert(offlineChange.pendingSmartHomeSync == 1);

  offlineChange.degrees = 16;
  assert(!validStoredSetpoint(offlineChange));
  assert(!validStoredSetpoint(makeStoredSetpoint(41, false)));
  saved.magic = 0;
  assert(!validStoredSetpoint(saved));
}
