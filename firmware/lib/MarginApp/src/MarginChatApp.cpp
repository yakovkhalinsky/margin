// SPDX-License-Identifier: MIT
// STATUS: scaffold. Compiles only inside the PapyriX tree; not yet built or tested on an X4 Pro.
// What works conceptually: BLE messages -> ChatModel -> wrapped lines -> coalesced refreshes.
// What is TODO is marked TODO(Mx) with the docs/ROADMAP.md milestone.
#include "MarginChatApp.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <esp_heap_caps.h>

#include <cstdio>
#include <cstring>
#include <new>

#include "ThemeManager.h"
#include "core/Core.h"
#include "margin/ble_service.h"
#include "margin/chat_model.h"
#include "margin/messages.h"
#include "ui/Elements.h"

extern GfxRenderer renderer;

namespace papyrix::margin_chat_app {

namespace {

using margin::chat::ChatModel;
using margin::chat::Refresh;
using margin::chat::RefreshPolicy;
namespace proto = margin::proto;

constexpr uint32_t kArenaBytes = 32 * 1024;  // transcript kept on device (PSRAM)
constexpr int kMarginX = 16;
constexpr char kVersion[] = "0.1.0-scaffold";

struct State {
  char* arena = nullptr;
  ChatModel* model = nullptr;
  RefreshPolicy refresh;
  uint32_t drawnRevision = 0;
  int scrollLines = 0;          // 0 = pinned to the newest line
  int selectedReply = -1;       // quick-reply cursor
  bool streamFinished = false;
  uint32_t pendingPasskey = 0;  // non-zero while the pairing prompt is shown
  margin::ble::LinkState link = margin::ble::LinkState::Off;
};
State st;
alignas(ChatModel) uint8_t modelStorage[sizeof(ChatModel)];

class BleHandler final : public margin::ble::Handler {
 public:
  void onMessage(const proto::Message& m) override {
    if (st.model == nullptr) return;
    switch (m.type) {
      case proto::MsgType::Hello: {
        proto::Hello h;
        if (proto::decodeHello(m.body, m.len, h)) sendHello();
        break;
      }
      case proto::MsgType::ChatDelta: {
        proto::ChatDelta d;
        if (proto::decodeChatDelta(m.body, m.len, d)) {
          // The app owns conversations: a new conv_id means it started a new one.
          if (d.convId != st.model->convId()) st.model->clear(d.convId);
          st.model->applyDelta(d);
          st.streamFinished = false;
        }
        break;
      }
      case proto::MsgType::ChatDone: {
        proto::ChatDone d;
        if (proto::decodeChatDone(m.body, m.len, d) && st.model->applyDone(d)) st.streamFinished = true;
        break;
      }
      case proto::MsgType::ThinkingStatus: {
        proto::ThinkingStatus s;
        if (proto::decodeThinkingStatus(m.body, m.len, s)) st.model->applyThinking(s);
        // TODO(M3): optional frontlight "breathing" while Thinking, via core.frontLight, off by default.
        break;
      }
      case proto::MsgType::QuickReplies: {
        proto::QuickReplies q;
        if (proto::decodeQuickReplies(m.body, m.len, q)) {
          st.model->applyQuickReplies(q);
          st.selectedReply = q.count ? 0 : -1;
        }
        break;
      }
      case proto::MsgType::FileOffer:
        // TODO(M6): FileReceiver: validate name, check SD space, write /margin/inbox/<name>.part,
        // verify CRC32 on FILE_COMPLETE, rename, reply FILE_RESULT. Until then refuse politely.
        rejectFileOffer(m);
        break;
      default:
        break;  // Unknown or not-yet-implemented types are ignored (forward compatibility).
    }
  }

  uint32_t onFileChunk(const proto::FileChunk& c) override { return c.offset; }  // TODO(M6)

  void onPairingRequest(uint32_t passkey) override { st.pendingPasskey = passkey; }

  void onLinkState(margin::ble::LinkState s) override {
    st.link = s;
    if (s == margin::ble::LinkState::Secured) sendHello();
  }

 private:
  static void sendHello() {
    uint8_t body[96];
    // TODO(M3): compute columns/rows from the chat font and panel size instead of constants.
    const size_t n = proto::encodeHello(body, sizeof body, "margin", kVersion, proto::kFeatureChat, 40, 20,
                                        proto::kMaxBodySize);
    margin::ble::service().send(proto::MsgType::Hello, body, n);
  }
  static void rejectFileOffer(const proto::Message& m) {
    proto::FileOffer o;
    if (!proto::decodeFileOffer(m.body, m.len, o)) return;
    uint8_t body[32];
    const size_t n = proto::encodeFileAccept(body, sizeof body, o.transferId, proto::FileResultCode::Rejected, 0);
    margin::ble::service().send(proto::MsgType::FileAccept, body, n);
  }
};
BleHandler handler;

void sendUserText(const char* text, size_t len, proto::InputSource src, int optionIndex) {
  if (st.model == nullptr || len == 0) return;
  uint8_t body[512];
  const size_t n = proto::encodeUserMessage(body, sizeof body, st.model->convId(), src, text, len, optionIndex);
  if (n && margin::ble::service().send(proto::MsgType::UserMessage, body, n)) {
    st.model->addLocalUserMessage(text, len);
  }
  // TODO(M3): if send() is busy, queue instead of dropping.
}

// --- rendering --------------------------------------------------------------
struct LineSink {
  int fontId;
  int y;
  int bottom;
  int skip;  // lines still to skip (scrolling)
  bool black;
};

int measure(void* ctx, const char* s, size_t n) {
  // GfxRenderer measures NUL-terminated strings; copy into a scratch buffer.
  char buf[256];
  if (n >= sizeof buf) n = sizeof buf - 1;
  std::memcpy(buf, s, n);
  buf[n] = '\0';
  return renderer.getTextWidth(*static_cast<int*>(ctx), buf);
}

bool drawLine(void* ctx, const char* s, size_t n) {
  auto* sink = static_cast<LineSink*>(ctx);
  const int lh = renderer.getLineHeight(sink->fontId);
  if (sink->skip > 0) {
    --sink->skip;
    return true;
  }
  if (sink->y + lh > sink->bottom) return false;
  char buf[256];
  if (n >= sizeof buf) n = sizeof buf - 1;
  std::memcpy(buf, s, n);
  buf[n] = '\0';
  renderer.drawText(sink->fontId, kMarginX, sink->y, buf, sink->black);
  sink->y += lh;
  return true;
}

const char* linkLabel() {
  switch (st.link) {
    case margin::ble::LinkState::Off: return "Bluetooth off";
    case margin::ble::LinkState::Advertising: return "Waiting for phone";
    case margin::ble::LinkState::Connected: return "Securing link";
    case margin::ble::LinkState::Secured: return "Connected";
  }
  return "";
}

}  // namespace

void enter(Core& core) {
  (void)core;
  if (st.arena == nullptr) {
    st.arena = static_cast<char*>(heap_caps_malloc(kArenaBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (st.arena == nullptr) return;  // TODO(M3): show an error instead of a blank screen
    st.model = new (modelStorage) ChatModel(st.arena, kArenaBytes);
    st.model->clear(0);
  }
  st.refresh.reset();
  margin::ble::Config cfg;
  cfg.deviceName = "margin";
  margin::ble::service().begin(cfg, &handler);
  // TODO(M2): open the pairing window only from an explicit "Pair phone" action, not on enter.
}

bool update(Core& core) {
  (void)core;
  margin::ble::service().poll(millis());
  if (st.model == nullptr) return false;
  const bool changed = st.model->revision() != st.drawnRevision;
  const Refresh r = st.refresh.decide(millis(), changed, st.model->layoutInvalidated(), st.streamFinished);
  return r != Refresh::None;
}

void onButton(Core& core, Button btn) {
  (void)core;
  if (st.pendingPasskey) {
    // Numeric comparison: Center = confirm, Back = reject.
    if (btn == Button::Center || btn == Button::Back) {
      margin::ble::service().confirmPairing(btn == Button::Center);
      st.pendingPasskey = 0;
    }
    return;
  }
  switch (btn) {
    case Button::Up: ++st.scrollLines; break;
    case Button::Down: if (st.scrollLines > 0) --st.scrollLines; break;
    case Button::Left:
    case Button::Right:
      if (st.model && st.model->quickReplyCount()) {
        const int n = static_cast<int>(st.model->quickReplyCount());
        st.selectedReply = (st.selectedReply + (btn == Button::Right ? 1 : n - 1)) % n;
      }
      break;
    case Button::Center:
      if (st.model && st.selectedReply >= 0 && static_cast<size_t>(st.selectedReply) < st.model->quickReplyCount()) {
        const char* r = st.model->quickReply(st.selectedReply);
        sendUserText(r, std::strlen(r), proto::InputSource::QuickReply, st.selectedReply);
      }
      // TODO(M3): otherwise open the touch keyboard.
      break;
    default: break;
  }
  // TODO(M3): touch: tap a quick reply, swipe to scroll, long-press to cancel (CHAT_CANCEL).
}

bool render(Core& core) {
  (void)core;
  const Theme& theme = THEME;
  renderer.clearScreen(theme.backgroundColor);
  ui::title(renderer, theme, theme.screenMarginTop, "Chat");
  const int width = renderer.getScreenWidth() - 2 * kMarginX;
  int fontId = theme.uiFontId;

  if (st.pendingPasskey) {
    char buf[48];
    snprintf(buf, sizeof buf, "Pair with phone? Code %06u", static_cast<unsigned>(st.pendingPasskey));
    renderer.drawCenteredText(theme.uiFontId, renderer.getScreenHeight() / 2, buf, theme.primaryTextBlack);
    ui::ButtonBar buttons("Reject", "Confirm", "", "");
    ui::buttonBar(renderer, theme, buttons);
    return false;
  }

  renderer.drawText(theme.smallFontId, kMarginX, theme.screenMarginTop + 40, linkLabel(), theme.secondaryTextBlack);
  if (st.model == nullptr) return false;

  // TODO(M3): render only from firstDirtyTurn() and cache line breaks per turn; today every
  // frame rewraps the visible transcript, which is fine for a few KB of text.
  LineSink sink{fontId, theme.screenMarginTop + 70, renderer.getScreenHeight() - 120, 0, theme.primaryTextBlack};
  // TODO(M3): bottom-anchored layout so scrollLines counts from the newest line.
  sink.skip = 0;
  for (size_t i = 0; i < st.model->turnCount(); ++i) {
    const auto& t = st.model->turn(i);
    const char* who = t.role == proto::Role::User ? "You" : "Agent";
    renderer.drawText(theme.smallFontId, kMarginX, sink.y, who, theme.secondaryTextBlack);
    sink.y += renderer.getLineHeight(theme.smallFontId);
    margin::chat::wrapText(st.model->text(t), t.len, width, measure, &fontId, drawLine, &sink);
    sink.y += 8;
  }
  if (st.model->thinking() != proto::ThinkingState::Idle) {
    const char* l = st.model->thinkingLabel();
    renderer.drawText(theme.smallFontId, kMarginX, sink.y, *l ? l : "Thinking...", theme.secondaryTextBlack);
  }
  if (st.model->quickReplyCount()) {
    // TODO(M3): proper chips with touch targets; for now show the selected option in the button bar.
    const char* sel = st.selectedReply >= 0 ? st.model->quickReply(st.selectedReply) : "";
    ui::ButtonBar buttons("Back", sel, "<", ">");
    ui::buttonBar(renderer, theme, buttons);
  } else {
    ui::ButtonBar buttons("Back", "Type", "", "");
    ui::buttonBar(renderer, theme, buttons);
  }
  st.drawnRevision = st.model->revision();
  st.model->markClean();
  st.streamFinished = false;
  // Returning false lets AppLauncherState push the frame (it uses FAST_REFRESH for apps).
  // TODO(M3): return true and call renderer.displayBuffer() ourselves so RefreshPolicy can force
  // HALF/FULL refreshes for ghosting cleanup; check the UC8179/UC8279 X4 Pro drivers' modes first.
  return false;
}

void exit(Core& core) {
  (void)core;
  // TODO(M2): keep BLE running in the background (setting) so file push and sync work while reading.
  margin::ble::service().end();
}

}  // namespace papyrix::margin_chat_app
