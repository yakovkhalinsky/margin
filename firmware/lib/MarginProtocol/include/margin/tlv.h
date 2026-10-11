// SPDX-License-Identifier: MIT
// Message bodies are sequences of TLV items: [tag u8][len u16 LE][value]. Unknown tags are skipped.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "margin/protocol.h"

namespace margin::proto {

constexpr size_t kTlvHeaderSize = 3;

class TlvWriter {
 public:
  TlvWriter(uint8_t* buf, size_t cap) : buf_(buf), cap_(cap) {}

  TlvWriter& bytes(Tag tag, const uint8_t* data, size_t len);
  TlvWriter& str(Tag tag, const char* s) { return bytes(tag, reinterpret_cast<const uint8_t*>(s), s ? std::strlen(s) : 0); }
  TlvWriter& str(Tag tag, const char* s, size_t len) { return bytes(tag, reinterpret_cast<const uint8_t*>(s), len); }
  TlvWriter& u8(Tag tag, uint8_t v) { return bytes(tag, &v, 1); }
  TlvWriter& u16(Tag tag, uint16_t v);
  TlvWriter& u32(Tag tag, uint32_t v);

  // False once any write did not fit; the buffer then holds only the items that fit.
  bool ok() const { return ok_; }
  size_t size() const { return len_; }
  const uint8_t* data() const { return buf_; }

 private:
  uint8_t* buf_;
  size_t cap_;
  size_t len_ = 0;
  bool ok_ = true;
};

struct TlvItem {
  Tag tag;
  const uint8_t* value;
  uint16_t len;

  bool asU8(uint8_t& out) const;
  bool asU16(uint16_t& out) const;
  bool asU32(uint32_t& out) const;
};

class TlvReader {
 public:
  TlvReader(const uint8_t* buf, size_t len) : buf_(buf), len_(len) {}
  // Returns false at the end of input or on a truncated item (then malformed() is true).
  bool next(TlvItem& out);
  bool malformed() const { return malformed_; }

 private:
  const uint8_t* buf_;
  size_t len_;
  size_t pos_ = 0;
  bool malformed_ = false;
};

}  // namespace margin::proto
