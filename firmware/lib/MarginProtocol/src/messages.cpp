// SPDX-License-Identifier: MIT
#include "margin/messages.h"

#include <cstring>

namespace margin::proto {

namespace {

Str toStr(const TlvItem& it) { return Str{reinterpret_cast<const char*>(it.value), it.len}; }

// Iterates all items; returns false if the TLV stream is malformed or a known tag has the wrong size.
template <typename F>
bool forEach(const uint8_t* body, size_t len, F&& f) {
  TlvReader r(body, len);
  TlvItem it;
  while (r.next(it)) {
    if (!f(it)) return false;
  }
  return !r.malformed();
}

}  // namespace

bool decodeHello(const uint8_t* body, size_t len, Hello& out) {
  out = Hello{};
  bool ok = forEach(body, len, [&](const TlvItem& it) {
    switch (it.tag) {
      case Tag::ProtoVersion: return it.asU8(out.protoVersion);
      case Tag::Name: out.name = toStr(it); return true;
      case Tag::Version: out.version = toStr(it); return true;
      case Tag::Features: return it.asU32(out.features);
      case Tag::Columns: return it.asU16(out.columns);
      case Tag::Rows: return it.asU16(out.rows);
      case Tag::MaxBody: return it.asU32(out.maxBody);
      default: return true;
    }
  });
  return ok && out.protoVersion != 0;
}

bool decodeChatDelta(const uint8_t* body, size_t len, ChatDelta& out) {
  out = ChatDelta{};
  bool haveConv = false;
  bool ok = forEach(body, len, [&](const TlvItem& it) {
    uint8_t v;
    switch (it.tag) {
      case Tag::ConvId: haveConv = true; return it.asU32(out.convId);
      case Tag::TurnId: return it.asU32(out.turnId);
      case Tag::Role:
        if (!it.asU8(v)) return false;
        out.role = static_cast<Role>(v);
        return true;
      case Tag::Text: out.text = toStr(it); return true;
      default: return true;
    }
  });
  return ok && haveConv;
}

bool decodeChatDone(const uint8_t* body, size_t len, ChatDone& out) {
  out = ChatDone{};
  bool haveConv = false;
  bool ok = forEach(body, len, [&](const TlvItem& it) {
    uint8_t v;
    switch (it.tag) {
      case Tag::ConvId: haveConv = true; return it.asU32(out.convId);
      case Tag::TurnId: return it.asU32(out.turnId);
      case Tag::Finish:
        if (!it.asU8(v)) return false;
        out.finish = static_cast<FinishReason>(v);
        return true;
      default: return true;
    }
  });
  return ok && haveConv;
}

bool decodeThinkingStatus(const uint8_t* body, size_t len, ThinkingStatus& out) {
  out = ThinkingStatus{};
  bool haveState = false;
  bool ok = forEach(body, len, [&](const TlvItem& it) {
    uint8_t v;
    switch (it.tag) {
      case Tag::ConvId: return it.asU32(out.convId);
      case Tag::Status:
        if (!it.asU8(v)) return false;
        out.state = static_cast<ThinkingState>(v);
        haveState = true;
        return true;
      case Tag::Label: out.label = toStr(it); return true;
      default: return true;
    }
  });
  return ok && haveState;
}

bool decodeQuickReplies(const uint8_t* body, size_t len, QuickReplies& out) {
  out = QuickReplies{};
  return forEach(body, len, [&](const TlvItem& it) {
    switch (it.tag) {
      case Tag::ConvId: return it.asU32(out.convId);
      case Tag::TurnId: return it.asU32(out.turnId);
      case Tag::Option:
        if (out.count < kMaxQuickReplies) out.options[out.count++] = toStr(it);  // extras ignored
        return true;
      default: return true;
    }
  });
}

bool decodeFileOffer(const uint8_t* body, size_t len, FileOffer& out) {
  out = FileOffer{};
  bool haveId = false, haveSize = false;
  bool ok = forEach(body, len, [&](const TlvItem& it) {
    switch (it.tag) {
      case Tag::TransferId: haveId = true; return it.asU16(out.transferId);
      case Tag::FileName: out.fileName = toStr(it); return true;
      case Tag::FileSize: haveSize = true; return it.asU32(out.fileSize);
      case Tag::Crc32: return it.asU32(out.crc32);
      case Tag::Format: out.format = toStr(it); return true;
      default: return true;
    }
  });
  return ok && haveId && haveSize && !out.fileName.empty();
}

bool decodeFileComplete(const uint8_t* body, size_t len, FileComplete& out) {
  out = FileComplete{};
  bool haveId = false, haveCrc = false;
  bool ok = forEach(body, len, [&](const TlvItem& it) {
    switch (it.tag) {
      case Tag::TransferId: haveId = true; return it.asU16(out.transferId);
      case Tag::Crc32: haveCrc = true; return it.asU32(out.crc32);
      default: return true;
    }
  });
  return ok && haveId && haveCrc;
}

bool decodeSyncRequest(const uint8_t* body, size_t len, SyncRequest& out) {
  out = SyncRequest{};
  return forEach(body, len, [&](const TlvItem& it) {
    if (it.tag == Tag::Seq) return it.asU32(out.sinceSeq);
    return true;
  });
}

size_t encodeHello(uint8_t* out, size_t cap, const char* name, const char* version, uint32_t features,
                   uint16_t columns, uint16_t rows, uint32_t maxBody) {
  TlvWriter w(out, cap);
  w.u8(Tag::ProtoVersion, kProtocolVersion).str(Tag::Name, name).str(Tag::Version, version);
  w.u32(Tag::Features, features).u16(Tag::Columns, columns).u16(Tag::Rows, rows).u32(Tag::MaxBody, maxBody);
  return w.ok() ? w.size() : 0;
}

size_t encodeUserMessage(uint8_t* out, size_t cap, uint32_t convId, InputSource source, const char* text,
                         size_t textLen, int optionIndex) {
  TlvWriter w(out, cap);
  w.u32(Tag::ConvId, convId).u8(Tag::Source, static_cast<uint8_t>(source)).str(Tag::Text, text, textLen);
  if (optionIndex >= 0) w.u8(Tag::OptionIndex, static_cast<uint8_t>(optionIndex));
  return w.ok() ? w.size() : 0;
}

size_t encodeChatCancel(uint8_t* out, size_t cap, uint32_t convId, uint32_t turnId) {
  TlvWriter w(out, cap);
  w.u32(Tag::ConvId, convId).u32(Tag::TurnId, turnId);
  return w.ok() ? w.size() : 0;
}

size_t encodePassageShare(uint8_t* out, size_t cap, const PassageShare& p) {
  TlvWriter w(out, cap);
  w.u8(Tag::Intent, static_cast<uint8_t>(p.intent));
  if (p.bookId) w.str(Tag::BookId, p.bookId);
  if (p.bookTitle) w.str(Tag::BookTitle, p.bookTitle);
  if (p.chapterTitle) w.str(Tag::ChapterTitle, p.chapterTitle);
  w.u16(Tag::Spine, p.spine).u32(Tag::Offset, p.offset).u16(Tag::Percent, p.percentBp);
  w.str(Tag::Text, p.text, p.textLen);
  return w.ok() ? w.size() : 0;
}

size_t encodeFileAccept(uint8_t* out, size_t cap, uint16_t transferId, FileResultCode code, uint32_t resumeOffset) {
  TlvWriter w(out, cap);
  w.u16(Tag::TransferId, transferId).u8(Tag::Result, static_cast<uint8_t>(code)).u32(Tag::ResumeOffset, resumeOffset);
  return w.ok() ? w.size() : 0;
}

size_t encodeFileResult(uint8_t* out, size_t cap, uint16_t transferId, FileResultCode code, const char* path) {
  TlvWriter w(out, cap);
  w.u16(Tag::TransferId, transferId).u8(Tag::Result, static_cast<uint8_t>(code));
  if (path) w.str(Tag::Path, path);
  return w.ok() ? w.size() : 0;
}

size_t encodeDeviceStatus(uint8_t* out, size_t cap, uint8_t batteryPct, bool charging, uint32_t freeKb) {
  TlvWriter w(out, cap);
  w.u8(Tag::Battery, batteryPct).u8(Tag::Charging, charging ? 1 : 0).u32(Tag::FreeKb, freeKb);
  return w.ok() ? w.size() : 0;
}

size_t encodeError(uint8_t* out, size_t cap, uint16_t code, const char* text) {
  TlvWriter w(out, cap);
  w.u16(Tag::ErrorCode, code);
  if (text) w.str(Tag::Text, text);
  return w.ok() ? w.size() : 0;
}

bool isSafeFileName(const char* name, size_t len) {
  if (name == nullptr || len == 0 || len > 96 || name[0] == '.') return false;
  for (size_t i = 0; i < len; ++i) {
    const unsigned char c = static_cast<unsigned char>(name[i]);
    if (c < 0x20 || c == 0x7F || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
        c == '<' || c == '>' || c == '|')
      return false;
    if (c == '.' && i + 1 < len && name[i + 1] == '.') return false;
  }
  static const char* const kExts[] = {".epub", ".fb2", ".md", ".txt", ".html", ".htm"};
  for (const char* ext : kExts) {
    const size_t el = std::strlen(ext);
    if (len > el) {
      bool match = true;
      for (size_t i = 0; i < el; ++i) {
        char a = name[len - el + i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (a != ext[i]) {
          match = false;
          break;
        }
      }
      if (match) return true;
    }
  }
  return false;
}

}  // namespace margin::proto
