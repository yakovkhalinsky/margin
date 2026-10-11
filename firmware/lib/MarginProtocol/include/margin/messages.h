// SPDX-License-Identifier: MIT
// Typed views over v1 message bodies. Decoders point into the body buffer (no copies),
// so the body must outlive the decoded struct. Strings are UTF-8 and NOT NUL-terminated.
#pragma once

#include <cstddef>
#include <cstdint>

#include "margin/protocol.h"
#include "margin/tlv.h"

namespace margin::proto {

struct Str {
  const char* data = nullptr;
  uint16_t len = 0;
  bool empty() const { return len == 0; }
};

// ---- app -> device -----------------------------------------------------------
struct Hello {
  uint8_t protoVersion = 0;
  Str name;
  Str version;
  uint32_t features = 0;
  uint16_t columns = 0;  // device -> app only: wrap hint for the chat font
  uint16_t rows = 0;
  uint32_t maxBody = 0;
};

struct ChatDelta {
  uint32_t convId = 0;
  uint32_t turnId = 0;
  Role role = Role::Assistant;
  Str text;
};

struct ChatDone {
  uint32_t convId = 0;
  uint32_t turnId = 0;
  FinishReason finish = FinishReason::Stop;
};

struct ThinkingStatus {
  uint32_t convId = 0;
  ThinkingState state = ThinkingState::Idle;
  Str label;
};

struct QuickReplies {
  uint32_t convId = 0;
  uint32_t turnId = 0;
  uint8_t count = 0;
  Str options[kMaxQuickReplies];
};

struct FileOffer {
  uint16_t transferId = 0;
  Str fileName;
  uint32_t fileSize = 0;
  uint32_t crc32 = 0;
  Str format;
};

struct FileComplete {
  uint16_t transferId = 0;
  uint32_t crc32 = 0;
};

struct SyncRequest {
  uint32_t sinceSeq = 0;
};

bool decodeHello(const uint8_t* body, size_t len, Hello& out);
bool decodeChatDelta(const uint8_t* body, size_t len, ChatDelta& out);
bool decodeChatDone(const uint8_t* body, size_t len, ChatDone& out);
bool decodeThinkingStatus(const uint8_t* body, size_t len, ThinkingStatus& out);
bool decodeQuickReplies(const uint8_t* body, size_t len, QuickReplies& out);
bool decodeFileOffer(const uint8_t* body, size_t len, FileOffer& out);
bool decodeFileComplete(const uint8_t* body, size_t len, FileComplete& out);
bool decodeSyncRequest(const uint8_t* body, size_t len, SyncRequest& out);

// ---- device -> app -----------------------------------------------------------
struct PassageShare {
  PassageIntent intent = PassageIntent::Explain;
  const char* bookId = nullptr;
  const char* bookTitle = nullptr;
  const char* chapterTitle = nullptr;
  uint16_t spine = 0;
  uint32_t offset = 0;
  uint16_t percentBp = 0;
  const char* text = nullptr;  // the selected passage (or chapter text for SummariseChapter)
  size_t textLen = 0;
};

// Encoders return the body length, or 0 if `cap` was too small.
size_t encodeHello(uint8_t* out, size_t cap, const char* name, const char* version, uint32_t features,
                   uint16_t columns, uint16_t rows, uint32_t maxBody);
size_t encodeUserMessage(uint8_t* out, size_t cap, uint32_t convId, InputSource source, const char* text,
                         size_t textLen, int optionIndex = -1);
size_t encodeChatCancel(uint8_t* out, size_t cap, uint32_t convId, uint32_t turnId);
size_t encodePassageShare(uint8_t* out, size_t cap, const PassageShare& p);
size_t encodeFileAccept(uint8_t* out, size_t cap, uint16_t transferId, FileResultCode code, uint32_t resumeOffset);
size_t encodeFileResult(uint8_t* out, size_t cap, uint16_t transferId, FileResultCode code, const char* path);
size_t encodeDeviceStatus(uint8_t* out, size_t cap, uint8_t batteryPct, bool charging, uint32_t freeKb);
size_t encodeError(uint8_t* out, size_t cap, uint16_t code, const char* text);

// File names from FILE_OFFER must be a bare name: no path separators, no "..", no control
// characters, no leading dot, at most 96 bytes, and an allowed extension (.epub .fb2 .md .txt .html .htm).
bool isSafeFileName(const char* name, size_t len);

}  // namespace margin::proto
