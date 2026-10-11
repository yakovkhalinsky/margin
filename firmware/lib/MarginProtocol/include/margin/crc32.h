// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>

namespace margin::proto {

// CRC-32/ISO-HDLC (zlib, java.util.zip.CRC32). crc32("123456789") == 0xCBF43926.
// Pass the previous return value as `crc` to continue a running checksum; start with 0.
uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc = 0);

}  // namespace margin::proto
