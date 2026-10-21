// SPDX-License-Identifier: MIT
// margin BLE wire protocol, version 1. See docs/BLE_PROTOCOL.md.
// Keep this file in sync with android/app/src/main/java/dev/margin/protocol/Protocol.kt.
#pragma once

#include <cstddef>
#include <cstdint>

namespace margin::proto {

constexpr uint8_t kProtocolVersion = 1;

// 128-bit UUIDs. Only the first group differs between attributes.
constexpr const char* kServiceUuid = "d0ed0001-b422-469c-8266-d0abd145eb37";
constexpr const char* kRxCharUuid = "d0ed0002-b422-469c-8266-d0abd145eb37";    // app -> device, write / write-no-rsp
constexpr const char* kTxCharUuid = "d0ed0003-b422-469c-8266-d0abd145eb37";    // device -> app, notify
constexpr const char* kCtrlCharUuid = "d0ed0004-b422-469c-8266-d0abd145eb37";  // read / write / notify
constexpr const char* kFileCharUuid = "d0ed0005-b422-469c-8266-d0abd145eb37";  // app -> device, write-no-rsp

// --- Frame (one GATT write or notification) on RX / TX -----------------------
// [flags u8][msg_id u8][frag_idx u16 LE][payload ...]
constexpr size_t kFragHeaderSize = 4;
// First fragment payload starts with the message header:
// [version u8][type u8][reserved u16 LE = 0][body_len u32 LE]
constexpr size_t kMsgHeaderSize = 8;

constexpr size_t kMinFrameSize = 20;    // ATT_MTU 23 - 3 (no MTU exchange)
constexpr size_t kMaxFrameSize = 244;   // ATT_MTU 247 - 3 (what we request)
constexpr uint32_t kMaxBodySize = 65536;  // default device RX limit; files use the FILE characteristic
constexpr uint8_t kDefaultWindow = 8;   // fragments in flight before an ACK is required

enum FragFlags : uint8_t {
  kFlagFirst = 0x01,
  kFlagLast = 0x02,
  kFlagAckReq = 0x04,
};

enum class MsgType : uint8_t {
  Hello = 0x01,           // both
  Error = 0x02,           // both
  DeviceStatus = 0x03,    // device -> app
  ChatDelta = 0x10,       // app -> device
  ChatDone = 0x11,        // app -> device
  ThinkingStatus = 0x12,  // app -> device
  QuickReplies = 0x13,    // app -> device
  UserMessage = 0x14,     // device -> app
  ChatCancel = 0x15,      // device -> app
  ChatNew = 0x16,         // device -> app
  PassageShare = 0x20,    // device -> app
  FileOffer = 0x30,       // app -> device
  FileAccept = 0x31,      // device -> app
  FileComplete = 0x32,    // app -> device
  FileResult = 0x33,      // device -> app
  FileAbort = 0x34,       // both
  SyncRequest = 0x40,     // app -> device
  SyncRecords = 0x41,     // device -> app
  SyncDone = 0x42,        // device -> app
  SyncAck = 0x43,         // app -> device
  BibleContext = 0x50,    // device -> app
  BibleNoteSyncReq = 0x51,    // app -> device
  BibleNoteRecords = 0x52,    // device -> app
  BibleNoteSyncDone = 0x53,   // device -> app
  BibleNotePush = 0x54,       // app -> device
  BibleNoteAck = 0x55,        // device -> app
  // Companion app messages (see docs/companion-v1.md)
  NotifPush = 0x70,           // app -> device
  NotifDismiss = 0x71,        // device -> app
  NotifClear = 0x72,          // device -> app
  NotifDismissAck = 0x73,     // app -> device
  MsgThreadPush = 0x74,       // app -> device
  MsgMarkRead = 0x75,         // device -> app
  MsgQuickReply = 0x76,       // device -> app
  MsgQuickReplyAck = 0x77,    // app -> device
  CalendarSync = 0x80,        // app -> device
  CalendarRequest = 0x81,     // device -> app
  TimeSync = 0x90,            // app -> device
  TimeRequest = 0x91,         // device -> app
};

// --- CTRL characteristic packets (never fragmented) --------------------------
enum class CtrlOp : uint8_t {
  Ack = 0x01,      // [op][msg_id u8][next_frag u16 LE][credits u8]
  Nack = 0x02,     // [op][msg_id u8][reason u8][next_frag u16 LE]
  FileAck = 0x03,  // [op][transfer_id u16 LE][next_offset u32 LE][credits u8]
  Status = 0x10,   // read value: [op][version u8][max_frame u16 LE][window u8][flags u8][max_body u32 LE]
};
constexpr size_t kCtrlAckSize = 5;
constexpr size_t kCtrlNackSize = 5;
constexpr size_t kCtrlFileAckSize = 8;
constexpr size_t kCtrlStatusSize = 10;

enum class NackReason : uint8_t {
  None = 0,
  BadVersion = 1,
  OutOfOrder = 2,
  TooLarge = 3,
  BadLength = 4,
  Busy = 5,
  Unsupported = 6,
  Malformed = 7,
};

// --- FILE characteristic packet ----------------------------------------------
// [transfer_id u16 LE][offset u32 LE][data ...]
constexpr size_t kFileChunkHeaderSize = 6;

// --- TLV tags (global registry; unknown tags MUST be ignored) ---------------
enum class Tag : uint8_t {
  ConvId = 0x01,        // u32
  TurnId = 0x02,        // u32
  Text = 0x03,          // utf8
  Role = 0x04,          // u8 Role
  Status = 0x05,        // u8 ThinkingState
  Label = 0x06,         // utf8
  Option = 0x07,        // utf8, repeatable (max 6)
  Finish = 0x08,        // u8 FinishReason
  Intent = 0x09,        // u8 PassageIntent
  BookId = 0x0A,        // utf8 (content fingerprint)
  BookTitle = 0x0B,     // utf8
  ChapterTitle = 0x0C,  // utf8
  Spine = 0x0D,         // u16
  Offset = 0x0E,        // u32
  Percent = 0x0F,       // u16, basis points 0..10000
  TransferId = 0x10,    // u16
  FileName = 0x11,      // utf8, bare file name only
  FileSize = 0x12,      // u32
  Crc32 = 0x13,         // u32
  Format = 0x14,        // utf8 (epub, fb2, md, txt, html)
  Result = 0x15,        // u8 FileResultCode
  Path = 0x16,          // utf8
  Record = 0x17,        // nested TLV, repeatable
  RecordKind = 0x18,    // u8 RecordKind
  RecordId = 0x19,      // u32
  Seq = 0x1A,           // u32
  Timestamp = 0x1B,     // u32 unix seconds
  Note = 0x1C,          // utf8
  Deleted = 0x1D,       // u8 bool
  ErrorCode = 0x1E,     // u16
  ProtoVersion = 0x20,  // u8
  Name = 0x21,          // utf8
  Version = 0x22,       // utf8
  Features = 0x23,      // u32 bitmask (Feature)
  Columns = 0x24,       // u16 wrap hint
  Rows = 0x25,          // u16
  Battery = 0x26,       // u8 percent
  Charging = 0x27,      // u8 bool
  Source = 0x28,        // u8 InputSource
  OptionIndex = 0x29,   // u8
  FreeKb = 0x2A,        // u32
  MaxBody = 0x2B,       // u32
  ResumeOffset = 0x2C,  // u32
  // Bible study tags (0x60-0x6F)
  BibleRef = 0x60,              // utf8, OSIS reference (e.g. "JHN.3.16")
  BibleBookName = 0x61,         // utf8, full book name (e.g. "John")
  BibleChapter = 0x62,          // u16, 1-based chapter number
  BibleVerse = 0x63,            // u16, 1-based verse number
  BibleNoteType = 0x64,         // u8: 0 highlight, 1 note
  BibleHighlightColor = 0x65,   // u8: 0 yellow, 1 green, 2 blue, 3 pink
  BibleNoteRecord = 0x66,       // nested TLV, repeatable
  // Companion app tags (see docs/companion-v1.md)
  NotifId = 0x70,               // u32
  NotifKey = 0x71,              // utf8, Android notification key
  NotifApp = 0x72,              // utf8, package name
  NotifAppName = 0x73,          // utf8, human-readable app name
  NotifTitle = 0x74,            // utf8
  NotifBody = 0x75,             // utf8
  NotifTimestamp = 0x76,        // u64, ms since epoch
  NotifIcon = 0x77,             // u8, icon category
  MsgConvId = 0x78,             // utf8, conversation ID
  MsgSender = 0x79,             // utf8, sender name
  MsgSenderKey = 0x7A,          // utf8, sender identifier
  MsgText = 0x7B,               // utf8, message text
  MsgTimestamp = 0x7C,          // u64, ms since epoch
  MsgIsOutgoing = 0x7D,         // u8, 1 if sent by user
  MsgRecord = 0x7E,             // nested TLV, repeatable
  MsgThreadTitle = 0x7F,        // utf8, thread display title
  CalEventId = 0x80,            // u32
  CalTitle = 0x81,              // utf8
  CalStart = 0x82,              // u64, ms since epoch
  CalEnd = 0x83,                // u64, ms since epoch
  CalLocation = 0x84,           // utf8
  CalAllDay = 0x85,             // u8, 1 if all-day
  CalRecord = 0x86,             // nested TLV, repeatable
  QuickReplyOpt = 0x87,         // utf8, repeatable (max 6)
  QuickReplyIdx = 0x88,         // u8, selected index
  TimeUtcMs = 0x90,             // u64, current UTC time in ms
  TimeTzOffset = 0x91,          // i32, timezone offset in minutes
  TimeTzName = 0x92,            // utf8, timezone identifier
};

enum Feature : uint32_t {
  kFeatureChat = 1u << 0,
  kFeaturePassage = 1u << 1,
  kFeatureFilePush = 1u << 2,
  kFeatureSync = 1u << 3,
  kFeatureCompanion = 1u << 4,  // notifications, messages, calendar
};

enum class Role : uint8_t { User = 0, Assistant = 1, System = 2, Tool = 3 };
enum class ThinkingState : uint8_t { Idle = 0, Thinking = 1, ToolRunning = 2, Streaming = 3, Error = 4 };
enum class FinishReason : uint8_t { Stop = 0, Length = 1, Error = 2, Cancelled = 3 };
enum class InputSource : uint8_t { Keyboard = 0, QuickReply = 1, Button = 2 };
enum class PassageIntent : uint8_t { Ask = 0, Explain = 1, SummariseSelection = 2, SummariseChapter = 3, Define = 4, Translate = 5 };
enum class RecordKind : uint8_t { Bookmark = 0, Highlight = 1, Progress = 2 };
enum class FileResultCode : uint8_t { Ok = 0, Rejected = 1, NoSpace = 2, CrcMismatch = 3, IoError = 4, Aborted = 5, BadName = 6 };
enum class NotifIcon : uint8_t { Default = 0, Message = 1, Email = 2, Call = 3, Calendar = 4 };

constexpr size_t kMaxQuickReplies = 6;
constexpr size_t kMaxNotifications = 32;
constexpr size_t kMaxMessageThreads = 16;
constexpr size_t kMaxMessagesPerThread = 50;
constexpr size_t kMaxCalendarEvents = 100;

// --- little-endian helpers ---------------------------------------------------
inline void putU16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}
inline void putU32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}
inline uint16_t getU16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
inline uint32_t getU32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
inline void putU64(uint8_t* p, uint64_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
  p[4] = static_cast<uint8_t>(v >> 32);
  p[5] = static_cast<uint8_t>(v >> 40);
  p[6] = static_cast<uint8_t>(v >> 48);
  p[7] = static_cast<uint8_t>(v >> 56);
}
inline uint64_t getU64(const uint8_t* p) {
  return static_cast<uint64_t>(p[0]) | (static_cast<uint64_t>(p[1]) << 8) |
         (static_cast<uint64_t>(p[2]) << 16) | (static_cast<uint64_t>(p[3]) << 24) |
         (static_cast<uint64_t>(p[4]) << 32) | (static_cast<uint64_t>(p[5]) << 40) |
         (static_cast<uint64_t>(p[6]) << 48) | (static_cast<uint64_t>(p[7]) << 56);
}
inline int32_t getI32(const uint8_t* p) { return static_cast<int32_t>(getU32(p)); }
inline void putI32(uint8_t* p, int32_t v) { putU32(p, static_cast<uint32_t>(v)); }

}  // namespace margin::proto
