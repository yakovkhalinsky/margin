// SPDX-License-Identifier: MIT
// Unit tests for companion message encoding/decoding.
// Test vectors are shared with android/app/src/test/.../CompanionMessagesTest.kt.
#include <cstring>
#include <vector>

#include "margin/messages.h"
#include "margin/tlv.h"
#include "test.h"

using namespace margin::proto;


TEST(time_sync_decode) {
  // Golden vector from docs/companion-v1.md
  uint8_t body[128];
  TlvWriter w(body, sizeof body);
  w.u64(Tag::TimeUtcMs, 1698076800000ULL);
  w.i32(Tag::TimeTzOffset, -240);  // EDT, UTC-4
  w.str(Tag::TimeTzName, "America/New_York");
  CHECK(w.ok());

  TimeSync ts;
  CHECK(decodeTimeSync(body, w.size(), ts));
  CHECK_EQ(ts.utcMs, 1698076800000ULL);
  CHECK_EQ(ts.tzOffsetMinutes, -240);
  CHECK(ts.tzName.len == 16);
  CHECK(std::memcmp(ts.tzName.data, "America/New_York", 16) == 0);
}

TEST(notif_push_decode) {
  uint8_t body[512];
  TlvWriter w(body, sizeof body);
  w.str(Tag::NotifKey, "com.whatsapp|null|12345");
  w.u32(Tag::NotifId, 12345);
  w.str(Tag::NotifApp, "com.whatsapp");
  w.str(Tag::NotifAppName, "WhatsApp");
  w.str(Tag::NotifTitle, "John");
  w.str(Tag::NotifBody, "Hey, are you coming to dinner?");
  w.u64(Tag::NotifTimestamp, 1698076800000ULL);
  w.u8(Tag::NotifIcon, static_cast<uint8_t>(NotifIcon::Message));
  w.str(Tag::QuickReplyOpt, "Yes");
  w.str(Tag::QuickReplyOpt, "No");
  w.str(Tag::QuickReplyOpt, "Maybe later");
  CHECK(w.ok());

  NotifPush np;
  CHECK(decodeNotifPush(body, w.size(), np));
  CHECK(std::strncmp(np.notifKey.data, "com.whatsapp|null|12345", np.notifKey.len) == 0);
  CHECK_EQ(np.notifId, 12345u);
  CHECK(np.app.len == 12);
  CHECK(np.appName.len == 8);
  CHECK(std::strncmp(np.title.data, "John", np.title.len) == 0);
  CHECK(np.body.len == 30);
  CHECK_EQ(np.timestamp, 1698076800000ULL);
  CHECK(np.icon == NotifIcon::Message);
  CHECK_EQ(np.quickReplyCount, 3);
  CHECK(std::strncmp(np.quickReplies[0].data, "Yes", np.quickReplies[0].len) == 0);
  CHECK(std::strncmp(np.quickReplies[1].data, "No", np.quickReplies[1].len) == 0);
  CHECK(std::strncmp(np.quickReplies[2].data, "Maybe later", np.quickReplies[2].len) == 0);
}

TEST(msg_thread_push_decode) {
  uint8_t body[1024];
  TlvWriter w(body, sizeof body);
  w.str(Tag::MsgConvId, "com.whatsapp:thread123");
  w.str(Tag::MsgThreadTitle, "Family Group");
  w.str(Tag::NotifApp, "com.whatsapp");

  // First message (nested TLV)
  uint8_t rec1[128];
  TlvWriter r1(rec1, sizeof rec1);
  r1.str(Tag::MsgSender, "Mom");
  r1.str(Tag::MsgText, "Dinner at 7?");
  r1.u64(Tag::MsgTimestamp, 1698076700000ULL);
  CHECK(r1.ok());
  w.bytes(Tag::MsgRecord, rec1, r1.size());

  // Second message
  uint8_t rec2[128];
  TlvWriter r2(rec2, sizeof rec2);
  r2.str(Tag::MsgSender, "You");
  r2.str(Tag::MsgText, "Sure!");
  r2.u64(Tag::MsgTimestamp, 1698076750000ULL);
  r2.u8(Tag::MsgIsOutgoing, 1);
  CHECK(r2.ok());
  w.bytes(Tag::MsgRecord, rec2, r2.size());

  w.str(Tag::QuickReplyOpt, "OK");
  CHECK(w.ok());

  MsgThreadPush mtp;
  CHECK(decodeMsgThreadPush(body, w.size(), mtp));
  CHECK(std::strncmp(mtp.convId.data, "com.whatsapp:thread123", mtp.convId.len) == 0);
  CHECK(std::strncmp(mtp.threadTitle.data, "Family Group", mtp.threadTitle.len) == 0);
  CHECK_EQ(mtp.messageCount, 2);
  CHECK(std::strncmp(mtp.messages[0].sender.data, "Mom", mtp.messages[0].sender.len) == 0);
  CHECK(std::strncmp(mtp.messages[0].text.data, "Dinner at 7?", mtp.messages[0].text.len) == 0);
  CHECK_EQ(mtp.messages[0].timestamp, 1698076700000ULL);
  CHECK(!mtp.messages[0].isOutgoing);
  CHECK(mtp.messages[1].isOutgoing);
  CHECK_EQ(mtp.quickReplyCount, 1);
}

TEST(calendar_sync_decode) {
  uint8_t body[1024];
  TlvWriter w(body, sizeof body);

  // First event
  uint8_t ev1[128];
  TlvWriter e1(ev1, sizeof ev1);
  e1.u32(Tag::CalEventId, 1001);
  e1.str(Tag::CalTitle, "Team Meeting");
  e1.u64(Tag::CalStart, 1698080400000ULL);
  e1.u64(Tag::CalEnd, 1698084000000ULL);
  e1.str(Tag::CalLocation, "Conference Room A");
  CHECK(e1.ok());
  w.bytes(Tag::CalRecord, ev1, e1.size());

  // Second event (all-day)
  uint8_t ev2[128];
  TlvWriter e2(ev2, sizeof ev2);
  e2.u32(Tag::CalEventId, 1002);
  e2.str(Tag::CalTitle, "Birthday");
  e2.u64(Tag::CalStart, 1698105600000ULL);
  e2.u64(Tag::CalEnd, 1698192000000ULL);
  e2.u8(Tag::CalAllDay, 1);
  CHECK(e2.ok());
  w.bytes(Tag::CalRecord, ev2, e2.size());

  CHECK(w.ok());

  CalendarSync cs;
  CHECK(decodeCalendarSync(body, w.size(), cs));
  CHECK_EQ(cs.eventCount, 2);
  CHECK_EQ(cs.events[0].eventId, 1001u);
  CHECK(std::strncmp(cs.events[0].title.data, "Team Meeting", cs.events[0].title.len) == 0);
  CHECK(std::strncmp(cs.events[0].location.data, "Conference Room A", cs.events[0].location.len) == 0);
  CHECK(!cs.events[0].allDay);
  CHECK_EQ(cs.events[1].eventId, 1002u);
  CHECK(cs.events[1].allDay);
}

TEST(notif_dismiss_encode) {
  uint8_t body[256];
  size_t len = encodeNotifDismiss(body, sizeof body, "com.example|null|999");
  CHECK(len > 0);

  TlvReader r(body, len);
  TlvItem it;
  CHECK(r.next(it));
  CHECK(it.tag == Tag::NotifKey);
  CHECK(it.len == 20);
  CHECK(std::memcmp(it.value, "com.example|null|999", 20) == 0);
}

TEST(msg_quick_reply_encode) {
  uint8_t body[256];
  size_t len = encodeMsgQuickReply(body, sizeof body, "com.whatsapp:abc123", 2);
  CHECK(len > 0);

  TlvReader r(body, len);
  TlvItem it;

  CHECK(r.next(it));
  CHECK(it.tag == Tag::MsgConvId);
  CHECK(it.len == 19);

  CHECK(r.next(it));
  CHECK(it.tag == Tag::QuickReplyIdx);
  uint8_t idx;
  CHECK(it.asU8(idx));
  CHECK_EQ(idx, 2);
}

TEST(u64_le_encoding) {
  uint8_t body[16];
  TlvWriter w(body, sizeof body);
  w.u64(Tag::TimeUtcMs, 0x0102030405060708ULL);
  CHECK(w.ok());
  CHECK_EQ(w.size(), 11u);  // tag(1) + len(2) + value(8)

  // Little-endian: LSB at lowest address
  CHECK_EQ(body[3], 0x08);
  CHECK_EQ(body[4], 0x07);
  CHECK_EQ(body[5], 0x06);
  CHECK_EQ(body[6], 0x05);
  CHECK_EQ(body[7], 0x04);
  CHECK_EQ(body[8], 0x03);
  CHECK_EQ(body[9], 0x02);
  CHECK_EQ(body[10], 0x01);

  TlvItem it;
  TlvReader r(body, w.size());
  CHECK(r.next(it));
  uint64_t v;
  CHECK(it.asU64(v));
  CHECK_EQ(v, 0x0102030405060708ULL);
}

TEST(i32_le_encoding) {
  uint8_t body[16];
  TlvWriter w(body, sizeof body);
  w.i32(Tag::TimeTzOffset, -240);  // -240 = 0xFFFFFF10
  CHECK(w.ok());
  CHECK_EQ(w.size(), 7u);  // tag(1) + len(2) + value(4)

  TlvItem it;
  TlvReader r(body, w.size());
  CHECK(r.next(it));
  int32_t v;
  CHECK(it.asI32(v));
  CHECK_EQ(v, -240);
}

TEST(empty_body_messages) {
  uint8_t body[8];
  size_t len = encodeNotifClear(body, sizeof body);
  CHECK_EQ(len, 0u);  // Empty body

  len = encodeCalendarRequest(body, sizeof body);
  CHECK_EQ(len, 0u);

  len = encodeTimeRequest(body, sizeof body);
  CHECK_EQ(len, 0u);
}

TEST(quick_replies_capped_at_six) {
  uint8_t body[512];
  TlvWriter w(body, sizeof body);
  w.str(Tag::NotifKey, "test");
  w.u32(Tag::NotifId, 1);
  for (int i = 0; i < 10; ++i) {
    w.str(Tag::QuickReplyOpt, "opt");
  }
  CHECK(w.ok());

  NotifPush np;
  CHECK(decodeNotifPush(body, w.size(), np));
  CHECK_EQ(np.quickReplyCount, kMaxQuickReplies);  // Capped at 6
}
