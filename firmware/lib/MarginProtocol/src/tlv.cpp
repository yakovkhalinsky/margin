// SPDX-License-Identifier: MIT
#include "margin/tlv.h"

namespace margin::proto {

TlvWriter& TlvWriter::bytes(Tag tag, const uint8_t* data, size_t len) {
  if (!ok_ || len > 0xFFFF || cap_ - len_ < kTlvHeaderSize + len) {
    ok_ = false;
    return *this;
  }
  buf_[len_] = static_cast<uint8_t>(tag);
  putU16(buf_ + len_ + 1, static_cast<uint16_t>(len));
  if (len) std::memcpy(buf_ + len_ + kTlvHeaderSize, data, len);
  len_ += kTlvHeaderSize + len;
  return *this;
}

TlvWriter& TlvWriter::u16(Tag tag, uint16_t v) {
  uint8_t b[2];
  putU16(b, v);
  return bytes(tag, b, 2);
}

TlvWriter& TlvWriter::u32(Tag tag, uint32_t v) {
  uint8_t b[4];
  putU32(b, v);
  return bytes(tag, b, 4);
}

TlvWriter& TlvWriter::u64(Tag tag, uint64_t v) {
  uint8_t b[8];
  putU64(b, v);
  return bytes(tag, b, 8);
}

TlvWriter& TlvWriter::i32(Tag tag, int32_t v) {
  uint8_t b[4];
  putI32(b, v);
  return bytes(tag, b, 4);
}

bool TlvItem::asU8(uint8_t& out) const {
  if (len != 1) return false;
  out = value[0];
  return true;
}
bool TlvItem::asU16(uint16_t& out) const {
  if (len != 2) return false;
  out = getU16(value);
  return true;
}
bool TlvItem::asU32(uint32_t& out) const {
  if (len != 4) return false;
  out = getU32(value);
  return true;
}

bool TlvItem::asU64(uint64_t& out) const {
  if (len != 8) return false;
  out = getU64(value);
  return true;
}

bool TlvItem::asI32(int32_t& out) const {
  if (len != 4) return false;
  out = getI32(value);
  return true;
}

bool TlvReader::next(TlvItem& out) {
  if (malformed_ || pos_ >= len_) return false;
  if (len_ - pos_ < kTlvHeaderSize) {
    malformed_ = true;
    return false;
  }
  const uint16_t l = getU16(buf_ + pos_ + 1);
  if (len_ - pos_ - kTlvHeaderSize < l) {
    malformed_ = true;
    return false;
  }
  out.tag = static_cast<Tag>(buf_[pos_]);
  out.value = buf_ + pos_ + kTlvHeaderSize;
  out.len = l;
  pos_ += kTlvHeaderSize + l;
  return true;
}

}  // namespace margin::proto
