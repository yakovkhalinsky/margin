// SPDX-License-Identifier: MIT
#include "margin/chat_model.h"

#include <cstring>

namespace margin::chat {

using proto::Role;

namespace {
// Copies at most `cap` bytes plus a NUL, never cutting a UTF-8 sequence in half.
void copyStr(char* dst, size_t cap, const char* s, size_t n) {
  if (s == nullptr) n = 0;
  if (n > cap) {
    n = cap;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
  }
  if (n) std::memcpy(dst, s, n);
  dst[n] = '\0';
}
}  // namespace

void ChatModel::clear(uint32_t newConvId) {
  convId_ = newConvId;
  used_ = 0;
  turnCount_ = 0;
  thinking_ = proto::ThinkingState::Idle;
  label_[0] = '\0';
  optionCount_ = 0;
  layoutInvalid_ = true;
  firstDirty_ = 0;
  ++revision_;
}

void ChatModel::markClean() {
  firstDirty_ = turnCount_;
  layoutInvalid_ = false;
}

void ChatModel::touch(size_t turnIndex) {
  if (turnIndex < firstDirty_) firstDirty_ = turnIndex;
  ++revision_;
}

Turn* ChatModel::findTurn(uint32_t turnId) {
  // Streaming almost always targets the newest turn: search backwards.
  for (size_t i = turnCount_; i-- > 0;) {
    if (turns_[i].turnId == turnId) return &turns_[i];
  }
  return nullptr;
}

void ChatModel::dropOldestTurn() {
  if (turnCount_ == 0) return;
  const uint32_t shift = turns_[0].len + 1;  // + NUL
  std::memmove(arena_, arena_ + shift, used_ - shift);
  used_ -= shift;
  for (size_t i = 1; i < turnCount_; ++i) {
    turns_[i - 1] = turns_[i];
    turns_[i - 1].start -= shift;
  }
  --turnCount_;
  layoutInvalid_ = true;
  firstDirty_ = 0;
}

bool ChatModel::makeRoom(uint32_t needed) {
  while (cap_ - used_ < needed) {
    // Never drop the turn currently being appended (always the last one).
    if (turnCount_ <= 1) return false;
    dropOldestTurn();
  }
  return true;
}

bool ChatModel::append(Turn& t, const char* s, size_t n) {
  // Only the newest turn can grow in place; its NUL terminator is the arena tail.
  const size_t idx = static_cast<size_t>(&t - turns_);
  if (idx + 1 != turnCount_) return false;  // TODO: support late deltas to older turns if the app ever sends them
  if (n == 0) return true;
  const uint32_t turnId = t.turnId;
  if (!makeRoom(static_cast<uint32_t>(n))) return false;
  Turn* cur = findTurn(turnId);  // dropOldestTurn() moved entries
  std::memcpy(arena_ + cur->start + cur->len, s, n);
  cur->len += static_cast<uint32_t>(n);
  arena_[cur->start + cur->len] = '\0';
  used_ += static_cast<uint32_t>(n);
  touch(turnCount_ - 1);
  return true;
}

bool ChatModel::applyDelta(const proto::ChatDelta& d) {
  if (d.convId != convId_) return false;
  Turn* t = findTurn(d.turnId);
  if (t == nullptr) {
    if (turnCount_ == kMaxTurns) dropOldestTurn();
    while (cap_ - used_ < 1 && turnCount_ > 0) dropOldestTurn();
    if (cap_ - used_ < 1) return false;
    Turn& nt = turns_[turnCount_++];
    nt = Turn{d.role, d.turnId, used_, 0, false};
    arena_[used_++] = '\0';
    optionCount_ = 0;  // a new turn retires the previous quick replies
    touch(turnCount_ - 1);
    t = &nt;
  }
  if (t->done) return false;
  return append(*t, d.text.data, d.text.len);
}

bool ChatModel::applyDone(const proto::ChatDone& d) {
  if (d.convId != convId_) return false;
  Turn* t = findTurn(d.turnId);
  if (t == nullptr) return false;
  t->done = true;
  thinking_ = proto::ThinkingState::Idle;
  label_[0] = '\0';
  touch(static_cast<size_t>(t - turns_));
  return true;
}

void ChatModel::applyThinking(const proto::ThinkingStatus& s) {
  if (s.convId != convId_) return;
  thinking_ = s.state;
  copyStr(label_, kMaxLabelLen, s.label.data, s.label.len);
  ++revision_;
}

void ChatModel::applyQuickReplies(const proto::QuickReplies& q) {
  if (q.convId != convId_) return;
  optionCount_ = q.count;
  for (size_t i = 0; i < q.count; ++i) copyStr(options_[i], kMaxOptionLen, q.options[i].data, q.options[i].len);
  ++revision_;
}

bool ChatModel::addLocalUserMessage(const char* text, size_t len) {
  proto::ChatDelta d;
  d.convId = convId_;
  d.turnId = localTurnSeq_++;
  d.role = Role::User;
  d.text = proto::Str{text, static_cast<uint16_t>(len > 0xFFFF ? 0xFFFF : len)};
  if (!applyDelta(d)) return false;
  turns_[turnCount_ - 1].done = true;
  return true;
}

// ------------------------------------------------------------------ wrapText
namespace {
size_t utf8Next(const char* s, size_t len, size_t i) {
  ++i;
  while (i < len && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
  return i;
}
}  // namespace

size_t wrapText(const char* text, size_t len, int maxWidth, MeasureFn measure, void* mctx, LineFn emit, void* ectx) {
  size_t lines = 0;
  size_t pos = 0;
  auto out = [&](const char* s, size_t n) {
    ++lines;
    return emit(ectx, s, n);
  };
  while (pos <= len) {
    // Paragraph: up to '\n' or end.
    size_t end = pos;
    while (end < len && text[end] != '\n') ++end;
    if (end == pos) {
      if (!out(text + pos, 0)) return lines;
    }
    size_t lineStart = pos;
    while (lineStart < end) {
      // Skip leading spaces on continuation lines.
      while (lineStart < end && text[lineStart] == ' ' && lineStart != pos) ++lineStart;
      if (lineStart >= end) break;
      size_t lastBreak = 0;  // end of last word that fits (exclusive), 0 = none
      size_t i = lineStart;
      size_t fitEnd = lineStart;
      while (i < end) {
        size_t nxt = utf8Next(text, end, i);
        if (measure(mctx, text + lineStart, nxt - lineStart) > maxWidth) break;
        fitEnd = nxt;
        if (nxt >= end || text[nxt] == ' ') lastBreak = nxt;
        i = nxt;
      }
      size_t cut;
      if (fitEnd >= end) {
        cut = end;
      } else if (lastBreak > lineStart) {
        cut = lastBreak;
      } else {
        cut = fitEnd > lineStart ? fitEnd : utf8Next(text, end, lineStart);  // hard break, at least 1 code point
      }
      if (!out(text + lineStart, cut - lineStart)) return lines;
      lineStart = cut;
    }
    if (end >= len) break;
    pos = end + 1;
    if (pos == len) {  // trailing newline -> empty last line
      out(text + pos, 0);
      break;
    }
  }
  return lines;
}

// ------------------------------------------------------------- RefreshPolicy
Refresh RefreshPolicy::decide(uint32_t nowMs, bool contentChanged, bool layoutInvalidated, bool streamFinished) {
  if (contentChanged) pending_ = true;
  if (layoutInvalidated) pendingFull_ = pending_ = true;
  if (!pending_) return Refresh::None;
  const bool throttled = lastMs_ != 0 && nowMs - lastMs_ < minIntervalMs_;
  if (throttled && !streamFinished) return Refresh::None;
  pending_ = false;
  lastMs_ = nowMs ? nowMs : 1;
  if (pendingFull_ || fastCount_ >= fastBeforeFull_) {
    pendingFull_ = false;
    fastCount_ = 0;
    return Refresh::Full;
  }
  ++fastCount_;
  return Refresh::Fast;
}

}  // namespace margin::chat
