// SPDX-License-Identifier: MIT
#include <cstring>
#include <string>
#include <vector>

#include "margin/chat_model.h"
#include "test.h"

using namespace margin;
using namespace margin::chat;

namespace {
proto::ChatDelta delta(uint32_t conv, uint32_t turn, const char* s) {
  proto::ChatDelta d;
  d.convId = conv;
  d.turnId = turn;
  d.role = proto::Role::Assistant;
  d.text = proto::Str{s, static_cast<uint16_t>(std::strlen(s))};
  return d;
}

// Monospace: every code point is 10 px.
int mono(void*, const char* s, size_t n) {
  int cps = 0;
  for (size_t i = 0; i < n; ++i)
    if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) ++cps;
  return cps * 10;
}
bool collect(void* ctx, const char* s, size_t n) {
  static_cast<std::vector<std::string>*>(ctx)->emplace_back(s, n);
  return true;
}
std::vector<std::string> wrap(const char* s, int width) {
  std::vector<std::string> out;
  wrapText(s, std::strlen(s), width, mono, nullptr, collect, &out);
  return out;
}
}  // namespace

TEST(chat_streaming_appends_to_turn) {
  char arena[256];
  ChatModel m(arena, sizeof arena);
  m.clear(1);
  CHECK(m.applyDelta(delta(1, 10, "Hel")));
  CHECK(m.applyDelta(delta(1, 10, "lo")));
  CHECK_EQ(m.turnCount(), 1u);
  CHECK(std::strcmp(m.text(m.turn(0)), "Hello") == 0);
  CHECK(!m.applyDelta(delta(2, 10, "other conversation")));
  proto::ChatDone done;
  done.convId = 1;
  done.turnId = 10;
  CHECK(m.applyDone(done));
  CHECK(!m.applyDelta(delta(1, 10, "late")));  // finished turns are immutable
  CHECK(m.addLocalUserMessage("thanks", 6));
  CHECK_EQ(m.turnCount(), 2u);
  CHECK(m.turn(1).role == proto::Role::User);
}

TEST(chat_evicts_oldest_turns) {
  char arena[32];
  ChatModel m(arena, sizeof arena);
  m.clear(1);
  CHECK(m.applyDelta(delta(1, 1, "0123456789")));
  CHECK(m.applyDelta(delta(1, 2, "abcdefghij")));
  m.markClean();
  CHECK(!m.layoutInvalidated());
  CHECK(m.applyDelta(delta(1, 3, "ABCDEFGHIJ")));  // forces turn 1 out
  CHECK(m.layoutInvalidated());
  CHECK_EQ(m.turnCount(), 2u);
  CHECK(std::strcmp(m.text(m.turn(0)), "abcdefghij") == 0);
  CHECK(std::strcmp(m.text(m.turn(1)), "ABCDEFGHIJ") == 0);
}

TEST(chat_dirty_tracking) {
  char arena[128];
  ChatModel m(arena, sizeof arena);
  m.clear(1);
  m.applyDelta(delta(1, 1, "a"));
  m.applyDelta(delta(1, 2, "b"));
  m.markClean();
  CHECK_EQ(m.firstDirtyTurn(), 2u);
  m.applyDelta(delta(1, 2, "c"));
  CHECK_EQ(m.firstDirtyTurn(), 1u);
}

TEST(chat_quick_replies_cleared_by_new_turn) {
  char arena[128];
  ChatModel m(arena, sizeof arena);
  m.clear(1);
  proto::QuickReplies q;
  q.convId = 1;
  q.count = 2;
  q.options[0] = proto::Str{"Yes", 3};
  q.options[1] = proto::Str{"No", 2};
  m.applyQuickReplies(q);
  CHECK_EQ(m.quickReplyCount(), 2u);
  CHECK(std::strcmp(m.quickReply(1), "No") == 0);
  m.applyDelta(delta(1, 5, "next"));
  CHECK_EQ(m.quickReplyCount(), 0u);
}

TEST(wrap_basic) {
  auto l = wrap("the quick brown fox", 100);  // 10 chars per line
  CHECK_EQ(l.size(), 2u);
  CHECK(l[0] == "the quick");
  CHECK(l[1] == "brown fox");
}

TEST(wrap_newlines_and_long_words) {
  auto l = wrap("ab\n\nabcdefghijklmnop", 50);
  const std::vector<std::string> want = {"ab", "", "abcde", "fghij", "klmno", "p"};
  CHECK(l == want);
}

TEST(wrap_utf8_hard_break) {
  auto l = wrap("ПриветМир", 30);  // 9 code points, 3 per line
  CHECK_EQ(l.size(), 3u);
  CHECK(l[0] == "При");
  CHECK(l[2] == "Мир");
}

TEST(refresh_policy_coalesces_and_cleans) {
  RefreshPolicy p(2, 400);
  CHECK(p.decide(1000, true, true, false) == Refresh::Full);  // first draw after layout change
  CHECK(p.decide(1100, true, false, false) == Refresh::None);  // throttled
  CHECK(p.decide(1500, false, false, false) == Refresh::Fast); // pending change flushed
  CHECK(p.decide(1600, true, false, true) == Refresh::Fast);   // stream finished: flush now
  CHECK(p.decide(2100, true, false, false) == Refresh::Full);  // ghosting cleanup after 2 fast
  CHECK(p.decide(3000, false, false, false) == Refresh::None);
}
