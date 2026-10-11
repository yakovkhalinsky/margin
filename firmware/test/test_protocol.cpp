// SPDX-License-Identifier: MIT
#include <cstring>
#include <vector>

#include "margin/crc32.h"
#include "margin/framing.h"
#include "margin/messages.h"
#include "margin/tlv.h"
#include "test.h"

using namespace margin::proto;

namespace {

std::vector<uint8_t> pattern(size_t n) {
  std::vector<uint8_t> v(n);
  for (size_t i = 0; i < n; ++i) v[i] = static_cast<uint8_t>(i * 31 + 7);
  return v;
}

// Drives a full send through a lossless link with the CTRL ack loop. Returns true if delivered intact.
bool roundTrip(const std::vector<uint8_t>& body, size_t frameSize, uint8_t window) {
  Fragmenter f;
  if (!f.begin(42, MsgType::ChatDelta, body.data(), static_cast<uint32_t>(body.size()), frameSize, window)) return false;
  std::vector<uint8_t> rxBuf(kMaxBodySize);
  Reassembler r(rxBuf.data(), static_cast<uint32_t>(rxBuf.size()));
  uint8_t frame[kMaxFrameSize];
  bool complete = false;
  int guard = 100000;
  while (!f.done() && guard-- > 0) {
    if (!f.canSend()) return false;  // stalled: window full with no ACK requested
    size_t n = f.next(frame, sizeof frame);
    if (n == 0 || n > frameSize) return false;
    FeedResult res = r.feed(frame, n);
    if (res == FeedResult::Error) return false;
    if (res == FeedResult::Complete) {
      Message m = r.message();
      complete = m.type == MsgType::ChatDelta && m.msgId == 42 && m.len == body.size() &&
                 (body.empty() || std::memcmp(m.body, body.data(), body.size()) == 0);
    }
    if (r.ackRequested()) {
      uint8_t ctrl[8];
      size_t cn = encodeAck(ctrl, r.msgId(), r.nextFrag(), window);
      CtrlPacket p;
      if (!decodeCtrl(ctrl, cn, p) || p.op != CtrlOp::Ack) return false;
      f.onAck(p.nextFrag);
    }
  }
  return complete && f.done();
}

}  // namespace

TEST(crc32_check_value) {
  const char* s = "123456789";
  CHECK_EQ(crc32(reinterpret_cast<const uint8_t*>(s), 9), 0xCBF43926u);
  // Running checksum equals one-shot.
  uint32_t c = crc32(reinterpret_cast<const uint8_t*>(s), 4);
  c = crc32(reinterpret_cast<const uint8_t*>(s) + 4, 5, c);
  CHECK_EQ(c, 0xCBF43926u);
}

TEST(fragment_count) {
  CHECK_EQ(fragmentCount(0, 20), 1u);
  CHECK_EQ(fragmentCount(8, 20), 1u);   // 16-byte payload, 8 header + 8 body
  CHECK_EQ(fragmentCount(9, 20), 2u);
  CHECK_EQ(fragmentCount(24, 20), 2u);
  CHECK_EQ(fragmentCount(25, 20), 3u);
  CHECK_EQ(fragmentCount(232, 244), 1u);
  CHECK_EQ(fragmentCount(233, 244), 2u);
  CHECK_EQ(fragmentCount(10, 19), 0u);  // below minimum frame size
}

TEST(round_trip_sizes_and_mtus) {
  const size_t sizes[] = {0, 1, 7, 8, 9, 15, 16, 17, 100, 231, 232, 233, 1000, 4096, 65536};
  const size_t frames[] = {20, 23, 100, 182, 244};
  const uint8_t windows[] = {1, 2, 8, 255};
  for (size_t s : sizes)
    for (size_t fs : frames)
      for (uint8_t w : windows) {
        bool ok = roundTrip(pattern(s), fs, w);
        if (!ok) std::fprintf(stderr, "  size=%zu frame=%zu window=%u\n", s, fs, w);
        CHECK(ok);
      }
}

TEST(golden_chat_delta_frames) {
  // Shared with android/app/src/test/.../GoldenVectorsTest.kt and docs/BLE_PROTOCOL.md.
  uint8_t body[64];
  TlvWriter w(body, sizeof body);
  w.u32(Tag::ConvId, 1).u32(Tag::TurnId, 2).u8(Tag::Role, 1).str(Tag::Text, "Hi");
  CHECK(w.ok());
  CHECK_EQ(mt::hex(body, w.size()), std::string("01040001000000020400020000000401000103020048" "69"));
  Fragmenter f;
  CHECK(f.begin(7, MsgType::ChatDelta, body, static_cast<uint32_t>(w.size()), 20, 8));
  uint8_t frame[32];
  size_t n0 = f.next(frame, sizeof frame);
  CHECK_EQ(mt::hex(frame, n0), std::string("0107000001100000170000000104000100000002"));
  size_t n1 = f.next(frame, sizeof frame);
  CHECK_EQ(mt::hex(frame, n1), std::string("0607010004000200000004010001030200" "4869"));
  CHECK_EQ(f.next(frame, sizeof frame), 0u);
}

TEST(window_blocks_until_ack) {
  auto body = pattern(500);
  Fragmenter f;
  CHECK(f.begin(1, MsgType::ChatDelta, body.data(), 500, 20, 3));
  uint8_t frame[32];
  CHECK(f.next(frame, sizeof frame) > 0);
  CHECK((frame[0] & kFlagAckReq) == 0);
  CHECK(f.next(frame, sizeof frame) > 0);
  CHECK(f.next(frame, sizeof frame) > 0);
  CHECK((frame[0] & kFlagAckReq) != 0);  // third frame fills the window
  CHECK(!f.canSend());
  f.onAck(99);  // bogus: ahead of what was sent
  CHECK(!f.canSend());
  f.onAck(3);
  CHECK(f.canSend());
}

TEST(gap_triggers_nack_and_rewind) {
  auto body = pattern(200);
  Fragmenter f;
  CHECK(f.begin(9, MsgType::FileOffer, body.data(), 200, 20, 8));
  std::vector<uint8_t> buf(1024);
  Reassembler r(buf.data(), 1024);
  uint8_t frame[32];
  size_t n = f.next(frame, sizeof frame);
  CHECK(r.feed(frame, n) == FeedResult::NeedMore);
  f.next(frame, sizeof frame);  // lost in the air
  n = f.next(frame, sizeof frame);
  CHECK(r.feed(frame, n) == FeedResult::Error);
  CHECK(r.error() == NackReason::OutOfOrder);
  CHECK_EQ(r.nextFrag(), 1);
  CHECK(r.inProgress());
  f.rewindTo(r.nextFrag());
  FeedResult last = FeedResult::NeedMore;
  while (!f.allSent()) {
    if (!f.canSend()) f.onAck(r.nextFrag());
    n = f.next(frame, sizeof frame);
    last = r.feed(frame, n);
    CHECK(last != FeedResult::Error);
  }
  CHECK(last == FeedResult::Complete);
  CHECK(std::memcmp(r.message().body, body.data(), 200) == 0);
}

TEST(retransmit_after_complete_is_duplicate) {
  uint8_t body[4] = {1, 2, 3, 4};
  Fragmenter f;
  CHECK(f.begin(5, MsgType::ChatDone, body, 4, 20, 8));
  uint8_t frame[32];
  size_t n = f.next(frame, sizeof frame);
  uint8_t buf[64];
  Reassembler r(buf, sizeof buf);
  CHECK(r.feed(frame, n) == FeedResult::Complete);
  // ACK lost: sender resends the same single-frame message.
  CHECK(r.feed(frame, n) == FeedResult::Duplicate);
  CHECK(r.ackRequested());
  CHECK_EQ(r.nextFrag(), 1);
  // Next message id is accepted normally.
  CHECK(f.begin(6, MsgType::ChatDone, body, 4, 20, 8));
  n = f.next(frame, sizeof frame);
  CHECK(r.feed(frame, n) == FeedResult::Complete);
}

TEST(reassembler_rejects_bad_input) {
  uint8_t buf[16];
  Reassembler r(buf, sizeof buf);
  uint8_t tooBig[] = {kFlagFirst | kFlagLast, 1, 0, 0, kProtocolVersion, 0x10, 0, 0, 17, 0, 0, 0};
  CHECK(r.feed(tooBig, sizeof tooBig) == FeedResult::Error);
  CHECK(r.error() == NackReason::TooLarge);
  uint8_t badVer[] = {kFlagFirst | kFlagLast, 2, 0, 0, 9, 0x10, 0, 0, 0, 0, 0, 0};
  CHECK(r.feed(badVer, sizeof badVer) == FeedResult::Error);
  CHECK(r.error() == NackReason::BadVersion);
  uint8_t shortBody[] = {kFlagFirst | kFlagLast, 3, 0, 0, kProtocolVersion, 0x10, 0, 0, 4, 0, 0, 0, 0xAA};
  CHECK(r.feed(shortBody, sizeof shortBody) == FeedResult::Error);
  CHECK(r.error() == NackReason::BadLength);
  uint8_t overflow[] = {kFlagFirst | kFlagLast, 4, 0, 0, kProtocolVersion, 0x10, 0, 0, 1, 0, 0, 0, 0xAA, 0xBB};
  CHECK(r.feed(overflow, sizeof overflow) == FeedResult::Error);
  CHECK(r.error() == NackReason::BadLength);
  uint8_t orphan[] = {kFlagLast, 77, 1, 0, 0xAA};
  CHECK(r.feed(orphan, sizeof orphan) == FeedResult::Error);
  CHECK(r.feed(orphan, 3) == FeedResult::Error);
  CHECK(r.error() == NackReason::Malformed);
}

TEST(ctrl_packets) {
  uint8_t b[16];
  CtrlPacket p;
  CHECK(decodeCtrl(b, encodeNack(b, 3, NackReason::Busy, 513), p));
  CHECK(p.op == CtrlOp::Nack && p.msgId == 3 && p.reason == NackReason::Busy && p.nextFrag == 513);
  CHECK(decodeCtrl(b, encodeFileAck(b, 0xBEEF, 123456789, 16), p));
  CHECK(p.op == CtrlOp::FileAck && p.transferId == 0xBEEF && p.nextOffset == 123456789 && p.credits == 16);
  CHECK(decodeCtrl(b, encodeStatus(b, 244, 8, kStatusBonded, kMaxBodySize), p));
  CHECK(p.op == CtrlOp::Status && p.version == kProtocolVersion && p.maxFrame == 244 && p.window == 8 &&
        p.flags == kStatusBonded && p.maxBody == kMaxBodySize);
  CHECK(!decodeCtrl(b, 2, p));
  uint8_t unknown[] = {0x7E, 0, 0, 0, 0};
  CHECK(!decodeCtrl(unknown, sizeof unknown, p));
}

TEST(file_chunk) {
  uint8_t b[16];
  size_t n = encodeFileChunkHeader(b, 7, 4096);
  b[n] = 0x55;
  FileChunk c;
  CHECK(decodeFileChunk(b, n + 1, c));
  CHECK(c.transferId == 7 && c.offset == 4096 && c.len == 1 && c.data[0] == 0x55);
  CHECK(!decodeFileChunk(b, n, c));  // header only, no data
}

TEST(tlv_round_trip_and_unknown_tags) {
  uint8_t b[128];
  TlvWriter w(b, sizeof b);
  w.u32(Tag::ConvId, 0xDEADBEEF).bytes(static_cast<Tag>(0xF0), reinterpret_cast<const uint8_t*>("xyz"), 3);
  w.str(Tag::Text, "héllo");
  ChatDelta d;
  CHECK(decodeChatDelta(b, w.size(), d));
  CHECK_EQ(d.convId, 0xDEADBEEFu);
  CHECK(d.text.len == 6 && std::memcmp(d.text.data, "héllo", 6) == 0);
  // Truncated stream is malformed.
  CHECK(!decodeChatDelta(b, w.size() - 1, d));
  // Wrong-size known tag is rejected.
  uint8_t bad[] = {static_cast<uint8_t>(Tag::ConvId), 2, 0, 1, 2};
  CHECK(!decodeChatDelta(bad, sizeof bad, d));
  // Writer overflow is reported.
  TlvWriter small(b, 5);
  small.u32(Tag::ConvId, 1);
  CHECK(!small.ok());
}

TEST(quick_replies_capped) {
  uint8_t b[256];
  TlvWriter w(b, sizeof b);
  w.u32(Tag::ConvId, 1).u32(Tag::TurnId, 2);
  for (int i = 0; i < 8; ++i) w.str(Tag::Option, "opt");
  QuickReplies q;
  CHECK(decodeQuickReplies(b, w.size(), q));
  CHECK_EQ(q.count, kMaxQuickReplies);
}

TEST(hello_and_passage_encode_decode) {
  uint8_t b[512];
  size_t n = encodeHello(b, sizeof b, "margin-1A2B", "0.1.0", kFeatureChat | kFeaturePassage, 38, 18, kMaxBodySize);
  Hello h;
  CHECK(n > 0 && decodeHello(b, n, h));
  CHECK(h.protoVersion == kProtocolVersion && h.columns == 38 && h.rows == 18);
  CHECK(h.features == (kFeatureChat | kFeaturePassage));
  PassageShare p;
  p.intent = PassageIntent::Explain;
  p.bookTitle = "Moby-Dick";
  p.text = "Call me Ishmael.";
  p.textLen = 16;
  n = encodePassageShare(b, sizeof b, p);
  CHECK(n > 0);
  CHECK_EQ(encodePassageShare(b, 10, p), 0u);
}

TEST(file_offer_and_names) {
  uint8_t b[128];
  TlvWriter w(b, sizeof b);
  w.u16(Tag::TransferId, 3).str(Tag::FileName, "Book.EPUB").u32(Tag::FileSize, 1000).u32(Tag::Crc32, 0x1234);
  FileOffer o;
  CHECK(decodeFileOffer(b, w.size(), o));
  CHECK(isSafeFileName(o.fileName.data, o.fileName.len));
  const char* bad[] = {"../x.epub", "a/b.epub", ".hidden.txt", "x.exe", "a\\b.txt", "", "x..epub", "con:.txt"};
  for (const char* s : bad) CHECK(!isSafeFileName(s, std::strlen(s)));
  CHECK(isSafeFileName("notes.md", 8));
  CHECK(isSafeFileName("Война и мир.fb2", std::strlen("Война и мир.fb2")));
}
