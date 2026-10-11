// SPDX-License-Identifier: MIT
#include "margin/framing.h"

#include <cstring>

namespace margin::proto {

namespace {
constexpr uint32_t kMaxFragments = 0xFFFF;

size_t payloadPerFrame(size_t frameSize) { return frameSize - kFragHeaderSize; }
}  // namespace

uint32_t fragmentCount(uint32_t bodyLen, size_t frameSize) {
  if (frameSize < kMinFrameSize) return 0;
  const size_t p = payloadPerFrame(frameSize);
  const size_t firstBody = p - kMsgHeaderSize;
  uint64_t count = 1;
  if (bodyLen > firstBody) count += (static_cast<uint64_t>(bodyLen - firstBody) + p - 1) / p;
  return count > kMaxFragments ? 0 : static_cast<uint32_t>(count);
}

// ---------------------------------------------------------------- Fragmenter
bool Fragmenter::begin(uint8_t msgId, MsgType type, const uint8_t* body, uint32_t bodyLen, size_t frameSize,
                       uint8_t window) {
  count_ = 0;
  const uint32_t n = fragmentCount(bodyLen, frameSize);
  if (n == 0 || window == 0 || (bodyLen > 0 && body == nullptr)) return false;
  body_ = body;
  bodyLen_ = bodyLen;
  frameSize_ = frameSize;
  type_ = type;
  msgId_ = msgId;
  window_ = window;
  count_ = static_cast<uint16_t>(n);
  sent_ = 0;
  acked_ = 0;
  return true;
}

size_t Fragmenter::writeFrame(uint16_t idx, uint8_t* out) const {
  const size_t p = payloadPerFrame(frameSize_);
  const size_t firstBody = p - kMsgHeaderSize;
  uint8_t flags = 0;
  if (idx == 0) flags |= kFlagFirst;
  if (idx + 1 == count_) flags |= kFlagLast;
  // Ask for an ACK when this frame fills the window or ends the message.
  if ((flags & kFlagLast) || static_cast<uint32_t>(idx + 1 - acked_) >= window_) flags |= kFlagAckReq;

  out[0] = flags;
  out[1] = msgId_;
  putU16(out + 2, idx);
  size_t pos = kFragHeaderSize;

  size_t bodyOff, chunk;
  if (idx == 0) {
    out[pos++] = kProtocolVersion;
    out[pos++] = static_cast<uint8_t>(type_);
    putU16(out + pos, 0);
    pos += 2;
    putU32(out + pos, bodyLen_);
    pos += 4;
    bodyOff = 0;
    chunk = bodyLen_ < firstBody ? bodyLen_ : firstBody;
  } else {
    bodyOff = firstBody + static_cast<size_t>(idx - 1) * p;
    const size_t remaining = bodyLen_ - bodyOff;
    chunk = remaining < p ? remaining : p;
  }
  if (chunk) std::memcpy(out + pos, body_ + bodyOff, chunk);
  return pos + chunk;
}

size_t Fragmenter::next(uint8_t* out, size_t cap) {
  if (!canSend() || cap < frameSize_) return 0;
  const size_t n = writeFrame(sent_, out);
  ++sent_;
  return n;
}

void Fragmenter::onAck(uint16_t nextFrag) {
  if (nextFrag <= acked_ || nextFrag > sent_) return;  // stale or ahead of what we sent
  acked_ = nextFrag;
}

void Fragmenter::rewindTo(uint16_t fragIdx) {
  if (fragIdx < acked_) fragIdx = acked_;
  if (fragIdx > sent_) return;
  sent_ = fragIdx;
}

// --------------------------------------------------------------- Reassembler
void Reassembler::reset() {
  hasCompleted_ = false;
  inProgress_ = false;
  ackReq_ = false;
  nextFrag_ = 0;
  bodyLen_ = 0;
  received_ = 0;
  error_ = NackReason::None;
}

FeedResult Reassembler::duplicate(uint8_t flags, uint8_t id, uint16_t next) {
  // Report the id/progress of the message the duplicate belongs to so the re-ACK is meaningful.
  msgId_ = id;
  nextFrag_ = next;
  ackReq_ = (flags & (kFlagAckReq | kFlagLast)) != 0;
  return FeedResult::Duplicate;
}

FeedResult Reassembler::fail(NackReason r, bool keepPartial) {
  error_ = r;
  ackReq_ = false;
  if (!keepPartial) inProgress_ = false;
  return FeedResult::Error;
}

FeedResult Reassembler::feed(const uint8_t* frame, size_t len) {
  ackReq_ = false;
  error_ = NackReason::None;
  if (frame == nullptr || len < kFragHeaderSize) return fail(NackReason::Malformed, inProgress_);

  const uint8_t flags = frame[0];
  const uint8_t id = frame[1];
  const uint16_t idx = getU16(frame + 2);
  const uint8_t* payload = frame + kFragHeaderSize;
  size_t plen = len - kFragHeaderSize;

  if (!inProgress_ && hasCompleted_ && id == completedId_) {
    return duplicate(flags, id, completedCount_);
  }

  if (flags & kFlagFirst) {
    // A FIRST frame always (re)starts a message, abandoning any partial one.
    msgId_ = id;
    nextFrag_ = 0;
    inProgress_ = false;
    if (idx != 0 || plen < kMsgHeaderSize) return fail(NackReason::Malformed, false);
    if (payload[0] != kProtocolVersion) return fail(NackReason::BadVersion, false);
    const uint32_t bodyLen = getU32(payload + 4);
    if (bodyLen > cap_) return fail(NackReason::TooLarge, false);
    type_ = static_cast<MsgType>(payload[1]);
    bodyLen_ = bodyLen;
    received_ = 0;
    inProgress_ = true;
    payload += kMsgHeaderSize;
    plen -= kMsgHeaderSize;
  } else {
    if (!inProgress_ || id != msgId_) return fail(NackReason::OutOfOrder, false);
    if (idx < nextFrag_) return duplicate(flags, id, nextFrag_);
    if (idx > nextFrag_) return fail(NackReason::OutOfOrder, true);  // gap: ask sender to go back
  }

  if (plen > bodyLen_ - received_) return fail(NackReason::BadLength, false);
  if (plen) std::memcpy(buf_ + received_, payload, plen);
  received_ += static_cast<uint32_t>(plen);
  ++nextFrag_;

  if (flags & kFlagLast) {
    inProgress_ = false;
    if (received_ != bodyLen_) return fail(NackReason::BadLength, false);
    hasCompleted_ = true;
    completedId_ = msgId_;
    completedCount_ = nextFrag_;
    ackReq_ = true;
    return FeedResult::Complete;
  }
  ackReq_ = (flags & kFlagAckReq) != 0;
  return FeedResult::NeedMore;
}

// ---------------------------------------------------------------------- CTRL
size_t encodeAck(uint8_t* out, uint8_t msgId, uint16_t nextFrag, uint8_t credits) {
  out[0] = static_cast<uint8_t>(CtrlOp::Ack);
  out[1] = msgId;
  putU16(out + 2, nextFrag);
  out[4] = credits;
  return kCtrlAckSize;
}

size_t encodeNack(uint8_t* out, uint8_t msgId, NackReason reason, uint16_t nextFrag) {
  out[0] = static_cast<uint8_t>(CtrlOp::Nack);
  out[1] = msgId;
  out[2] = static_cast<uint8_t>(reason);
  putU16(out + 3, nextFrag);
  return kCtrlNackSize;
}

size_t encodeFileAck(uint8_t* out, uint16_t transferId, uint32_t nextOffset, uint8_t credits) {
  out[0] = static_cast<uint8_t>(CtrlOp::FileAck);
  putU16(out + 1, transferId);
  putU32(out + 3, nextOffset);
  out[7] = credits;
  return kCtrlFileAckSize;
}

size_t encodeStatus(uint8_t* out, uint16_t maxFrame, uint8_t window, uint8_t flags, uint32_t maxBody) {
  out[0] = static_cast<uint8_t>(CtrlOp::Status);
  out[1] = kProtocolVersion;
  putU16(out + 2, maxFrame);
  out[4] = window;
  out[5] = flags;
  putU32(out + 6, maxBody);
  return kCtrlStatusSize;
}

bool decodeCtrl(const uint8_t* in, size_t len, CtrlPacket& out) {
  if (in == nullptr || len < 1) return false;
  out = CtrlPacket{static_cast<CtrlOp>(in[0])};
  switch (out.op) {
    case CtrlOp::Ack:
      if (len < kCtrlAckSize) return false;
      out.msgId = in[1];
      out.nextFrag = getU16(in + 2);
      out.credits = in[4];
      return true;
    case CtrlOp::Nack:
      if (len < kCtrlNackSize) return false;
      out.msgId = in[1];
      out.reason = static_cast<NackReason>(in[2]);
      out.nextFrag = getU16(in + 3);
      return true;
    case CtrlOp::FileAck:
      if (len < kCtrlFileAckSize) return false;
      out.transferId = getU16(in + 1);
      out.nextOffset = getU32(in + 3);
      out.credits = in[7];
      return true;
    case CtrlOp::Status:
      if (len < kCtrlStatusSize) return false;
      out.version = in[1];
      out.maxFrame = getU16(in + 2);
      out.window = in[4];
      out.flags = in[5];
      out.maxBody = getU32(in + 6);
      return true;
  }
  return false;
}

// ---------------------------------------------------------------------- FILE
size_t encodeFileChunkHeader(uint8_t* out, uint16_t transferId, uint32_t offset) {
  putU16(out, transferId);
  putU32(out + 2, offset);
  return kFileChunkHeaderSize;
}

bool decodeFileChunk(const uint8_t* in, size_t len, FileChunk& out) {
  if (in == nullptr || len <= kFileChunkHeaderSize) return false;
  out.transferId = getU16(in);
  out.offset = getU32(in + 2);
  out.data = in + kFileChunkHeaderSize;
  out.len = len - kFileChunkHeaderSize;
  return true;
}

}  // namespace margin::proto
