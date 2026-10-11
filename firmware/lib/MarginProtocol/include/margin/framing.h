// SPDX-License-Identifier: MIT
// Fragmentation, reassembly and CTRL packets for the margin BLE protocol v1.
// Pure C++17: no heap, no Arduino. The caller owns every buffer.
#pragma once

#include <cstddef>
#include <cstdint>

#include "margin/protocol.h"

namespace margin::proto {

// Number of frames needed to carry `bodyLen` bytes with frames of `frameSize` bytes.
// Returns 0 if frameSize < kMinFrameSize or the count would not fit in u16.
uint32_t fragmentCount(uint32_t bodyLen, size_t frameSize);

// Splits one message into frames and tracks a go-back-N send window.
//
//   Fragmenter f;
//   f.begin(id, MsgType::ChatDelta, body, len, mtu - 3, window);
//   while (f.canSend()) { n = f.next(frame, sizeof frame); notifyOrWrite(frame, n); }
//   ... on CTRL Ack:  f.onAck(nextFrag);    on Nack / timeout: f.rewindTo(nextFrag);
//   f.done() once the receiver acknowledged the LAST fragment.
class Fragmenter {
 public:
  bool begin(uint8_t msgId, MsgType type, const uint8_t* body, uint32_t bodyLen, size_t frameSize,
             uint8_t window = kDefaultWindow);

  bool active() const { return count_ != 0; }
  bool canSend() const { return sent_ < count_ && static_cast<uint32_t>(sent_ - acked_) < window_; }
  bool allSent() const { return sent_ >= count_; }
  bool done() const { return count_ != 0 && acked_ >= count_; }
  uint16_t count() const { return count_; }
  uint16_t nextToSend() const { return sent_; }
  uint16_t acked() const { return acked_; }
  uint8_t msgId() const { return msgId_; }

  // Writes the next frame into `out` (needs >= frameSize bytes). Returns its length, 0 if nothing to send.
  size_t next(uint8_t* out, size_t cap);

  // Receiver confirmed every fragment below `nextFrag`. Stale or bogus values are ignored.
  void onAck(uint16_t nextFrag);
  // Go back and resend from `fragIdx` (NACK OutOfOrder or ACK timeout).
  void rewindTo(uint16_t fragIdx);

 private:
  size_t writeFrame(uint16_t idx, uint8_t* out) const;

  const uint8_t* body_ = nullptr;
  uint32_t bodyLen_ = 0;
  size_t frameSize_ = 0;
  MsgType type_ = MsgType::Hello;
  uint8_t msgId_ = 0;
  uint8_t window_ = kDefaultWindow;
  uint16_t count_ = 0;
  uint16_t sent_ = 0;
  uint16_t acked_ = 0;
};

struct Message {
  uint8_t msgId;
  MsgType type;
  const uint8_t* body;
  uint32_t len;
};

enum class FeedResult : uint8_t {
  NeedMore,   // fragment accepted, message incomplete
  Complete,   // message() is valid until the next feed()/reset()
  Duplicate,  // already-seen fragment, ignored (re-ACK if ackRequested())
  Error,      // see error(); send a NACK with msgId()/nextFrag()
};

// Rebuilds messages from frames into a caller-supplied buffer (PSRAM on device).
class Reassembler {
 public:
  Reassembler(uint8_t* buffer, uint32_t capacity) : buf_(buffer), cap_(capacity) {}
  // Re-points the reassembly buffer (also resets state).
  void attach(uint8_t* buffer, uint32_t capacity) {
    buf_ = buffer;
    cap_ = buffer ? capacity : 0;
    reset();
  }

  FeedResult feed(const uint8_t* frame, size_t len);
  void reset();

  Message message() const { return {msgId_, type_, buf_, bodyLen_}; }
  NackReason error() const { return error_; }
  // True if the last fed frame asked for an ACK (or completed the message).
  bool ackRequested() const { return ackReq_; }
  uint8_t msgId() const { return msgId_; }
  uint16_t nextFrag() const { return nextFrag_; }
  bool inProgress() const { return inProgress_; }

 private:
  FeedResult duplicate(uint8_t flags, uint8_t id, uint16_t next);
  FeedResult fail(NackReason r, bool keepPartial);

  uint8_t* buf_;
  uint32_t cap_;
  bool inProgress_ = false;
  bool ackReq_ = false;
  uint8_t msgId_ = 0;
  MsgType type_ = MsgType::Hello;
  uint16_t nextFrag_ = 0;
  uint32_t bodyLen_ = 0;
  uint32_t received_ = 0;
  NackReason error_ = NackReason::None;
  // Last completed message, so a retransmission after a lost ACK is not delivered twice.
  // Senders never reuse the immediately preceding msg_id, so this is unambiguous.
  bool hasCompleted_ = false;
  uint8_t completedId_ = 0;
  uint16_t completedCount_ = 0;
};

// --- CTRL packets -------------------------------------------------------------
struct CtrlPacket {
  CtrlOp op;
  uint8_t msgId = 0;
  uint16_t nextFrag = 0;
  uint8_t credits = 0;
  NackReason reason = NackReason::None;
  uint16_t transferId = 0;
  uint32_t nextOffset = 0;
  // Status
  uint8_t version = 0;
  uint16_t maxFrame = 0;
  uint8_t window = 0;
  uint8_t flags = 0;
  uint32_t maxBody = 0;
};

enum StatusFlags : uint8_t {
  kStatusBonded = 0x01,
  kStatusBusy = 0x02,
  kStatusPairingOpen = 0x04,
};

size_t encodeAck(uint8_t* out, uint8_t msgId, uint16_t nextFrag, uint8_t credits);
size_t encodeNack(uint8_t* out, uint8_t msgId, NackReason reason, uint16_t nextFrag);
size_t encodeFileAck(uint8_t* out, uint16_t transferId, uint32_t nextOffset, uint8_t credits);
size_t encodeStatus(uint8_t* out, uint16_t maxFrame, uint8_t window, uint8_t flags, uint32_t maxBody);
bool decodeCtrl(const uint8_t* in, size_t len, CtrlPacket& out);

// --- FILE characteristic chunks ----------------------------------------------
struct FileChunk {
  uint16_t transferId;
  uint32_t offset;
  const uint8_t* data;
  size_t len;
};
size_t encodeFileChunkHeader(uint8_t* out, uint16_t transferId, uint32_t offset);
bool decodeFileChunk(const uint8_t* in, size_t len, FileChunk& out);

}  // namespace margin::proto
