// SPDX-License-Identifier: MIT
#include "margin/crc32.h"

namespace margin::proto {

uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc) {
  // Bitwise implementation: small and table-free. Throughput is far above BLE rates.
  crc = ~crc;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

}  // namespace margin::proto
