// SPDX-License-Identifier: MIT
// Device-side chat state. Owns no hardware: the Chat screen renders from it and the BLE layer
// feeds it decoded messages. All storage is a caller-supplied arena (PSRAM on the X4 Pro).
#pragma once

#include <cstddef>
#include <cstdint>

#include "margin/messages.h"

namespace margin::chat {

struct Turn {
  proto::Role role;
  uint32_t turnId;
  uint32_t start;  // offset into the arena
  uint32_t len;
  bool done;
};

class ChatModel {
 public:
  static constexpr size_t kMaxTurns = 64;
  static constexpr size_t kMaxOptionLen = 48;
  static constexpr size_t kMaxLabelLen = 48;

  ChatModel(char* arena, uint32_t capacity) : arena_(arena), cap_(capacity) {}

  void clear(uint32_t newConvId);
  uint32_t convId() const { return convId_; }

  // Appends streamed text to the turn `d.turnId`, creating it if needed. Drops the oldest
  // turns when the arena is full. Deltas for another conversation are ignored (returns false).
  bool applyDelta(const proto::ChatDelta& d);
  bool applyDone(const proto::ChatDone& d);
  void applyThinking(const proto::ThinkingStatus& s);
  void applyQuickReplies(const proto::QuickReplies& q);
  // Records a message typed on the device (shown immediately, before the phone echoes anything).
  bool addLocalUserMessage(const char* text, size_t len);

  size_t turnCount() const { return turnCount_; }
  const Turn& turn(size_t i) const { return turns_[i]; }
  const char* text(const Turn& t) const { return arena_ + t.start; }

  proto::ThinkingState thinking() const { return thinking_; }
  const char* thinkingLabel() const { return label_; }
  size_t quickReplyCount() const { return optionCount_; }
  const char* quickReply(size_t i) const { return options_[i]; }

  // Monotonic counters so the screen can tell what changed since it last drew.
  uint32_t revision() const { return revision_; }
  // Index of the first turn whose content changed since markClean(); == turnCount() if none.
  size_t firstDirtyTurn() const { return firstDirty_; }
  bool layoutInvalidated() const { return layoutInvalid_; }  // turns dropped or conversation reset
  void markClean();

 private:
  Turn* findTurn(uint32_t turnId);
  bool append(Turn& t, const char* s, size_t n);
  bool makeRoom(uint32_t needed);
  void dropOldestTurn();
  void touch(size_t turnIndex);

  char* arena_;
  uint32_t cap_;
  uint32_t used_ = 0;
  Turn turns_[kMaxTurns] = {};
  size_t turnCount_ = 0;
  uint32_t convId_ = 0;
  uint32_t localTurnSeq_ = 0x80000000u;  // device-originated turn ids never collide with the app's
  proto::ThinkingState thinking_ = proto::ThinkingState::Idle;
  char label_[kMaxLabelLen + 1] = {};
  char options_[proto::kMaxQuickReplies][kMaxOptionLen + 1] = {};
  size_t optionCount_ = 0;
  uint32_t revision_ = 0;
  size_t firstDirty_ = 0;
  bool layoutInvalid_ = true;
};

// --- Line wrapping ----------------------------------------------------------
// Measures the pixel width of a UTF-8 run. On device this wraps GfxRenderer::getTextWidth;
// on host tests a fixed-advance measure is used.
using MeasureFn = int (*)(void* ctx, const char* s, size_t len);
// Called per output line (no trailing newline). Return false to stop early.
using LineFn = bool (*)(void* ctx, const char* s, size_t len);

// Greedy word wrap. Breaks at spaces; honours '\n'; hard-breaks words wider than maxWidth on
// UTF-8 code point boundaries. Returns the number of lines emitted.
size_t wrapText(const char* text, size_t len, int maxWidth, MeasureFn measure, void* measureCtx, LineFn emit,
                void* emitCtx);

// --- E-ink refresh policy ---------------------------------------------------
enum class Refresh : uint8_t { None, Fast, Full };

class RefreshPolicy {
 public:
  explicit RefreshPolicy(uint8_t fastBeforeFull = 12, uint32_t minIntervalMs = 400)
      : fastBeforeFull_(fastBeforeFull), minIntervalMs_(minIntervalMs) {}

  // Streaming text arrives faster than the panel can refresh (~0.5 s fast, ~1.3 s full on the
  // X4 Pro per Explorink's measurements), so updates are coalesced: at most one refresh per
  // minIntervalMs, a forced Full after `fastBeforeFull` Fast refreshes (ghosting) or when the
  // layout was invalidated.
  Refresh decide(uint32_t nowMs, bool contentChanged, bool layoutInvalidated, bool streamFinished);
  void reset() { fastCount_ = 0; lastMs_ = 0; pending_ = false; }

 private:
  uint8_t fastBeforeFull_;
  uint32_t minIntervalMs_;
  uint8_t fastCount_ = 0;
  uint32_t lastMs_ = 0;
  bool pending_ = false;
  bool pendingFull_ = false;
};

}  // namespace margin::chat
