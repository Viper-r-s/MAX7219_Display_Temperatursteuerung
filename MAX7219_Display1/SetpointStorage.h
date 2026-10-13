#ifndef SETPOINT_STORAGE_H
#define SETPOINT_STORAGE_H

#include <stdint.h>

struct StoredSetpoint {
  uint32_t magic;
  uint8_t degrees;
  uint8_t pendingSmartHomeSync;
  uint16_t checksum;
};

static_assert(sizeof(StoredSetpoint) == 8, "Unerwartetes EEPROM-Format");

inline StoredSetpoint makeStoredSetpoint(uint8_t degrees, bool pendingSync) {
  StoredSetpoint data = {0x4D583731UL, degrees, pendingSync ? uint8_t(1) : uint8_t(0), 0};
  data.checksum = uint16_t(0xB62D ^ data.degrees ^ (data.pendingSmartHomeSync ? 0xA500 : 0x5A00));
  return data;
}

inline bool validStoredSetpoint(const StoredSetpoint &data) {
  return data.magic == 0x4D583731UL && data.degrees <= 40 &&
         data.pendingSmartHomeSync <= 1 &&
         data.checksum == makeStoredSetpoint(data.degrees, data.pendingSmartHomeSync != 0).checksum;
}

#endif
