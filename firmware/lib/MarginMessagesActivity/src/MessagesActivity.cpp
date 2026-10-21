// SPDX-License-Identifier: MIT
#include "MessagesActivity.h"

#ifdef MARGIN_BLE

#include <GfxRenderer.h>
#include <Logging.h>

#include "components/UITheme.h"
#include "fontIds.h"
#include "margin/ble_service.h"
#include "margin/messages.h"

namespace margin {

using namespace companion;
using namespace proto;

static constexpr const char* TAG = "MSG_ACT";
static constexpr int kThreadsPerPage = 6;
static constexpr int kMessagesPerPage = 8;

MessagesActivity::MessagesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Messages", renderer, mappedInput) {}

void MessagesActivity::onEnter() {
  Activity::onEnter();
  mode_ = Mode::ThreadList;
  selectedIndex_ = 0;
  scrollOffset_ = 0;
  messageScrollOffset_ = 0;
  currentThread_ = nullptr;
  requestUpdateAndWait();
}

void MessagesActivity::onExit() {
  store().saveToSd();
  Activity::onExit();
}

void MessagesActivity::loop() {
  switch (mode_) {
    case Mode::ThreadList: handleThreadListInput(); break;
    case Mode::ThreadView: handleThreadViewInput(); break;
  }
}

void MessagesActivity::handleThreadListInput() {
  const size_t count = store().threadCount();

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (selectedIndex_ > 0) {
      selectedIndex_--;
      if (selectedIndex_ < scrollOffset_) scrollOffset_ = selectedIndex_;
      requestUpdate();
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (selectedIndex_ < static_cast<int>(count) - 1) {
      selectedIndex_++;
      if (selectedIndex_ >= scrollOffset_ + kThreadsPerPage) scrollOffset_ = selectedIndex_ - kThreadsPerPage + 1;
      requestUpdate();
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (count > 0) {
      currentThread_ = store().getThread(selectedIndex_);
      if (currentThread_) {
        store().markThreadRead(currentThread_->convId);
        mode_ = Mode::ThreadView;
        messageScrollOffset_ = 0;
        requestUpdate();
      }
    }
  }

  int x = 0, y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 16;
    const int startY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int tappedIndex = (y - startY) / lineHeight;
    
    if (tappedIndex >= 0 && tappedIndex < kThreadsPerPage) {
      const int actualIndex = scrollOffset_ + tappedIndex;
      if (actualIndex < static_cast<int>(count)) {
        selectedIndex_ = actualIndex;
        currentThread_ = store().getThread(selectedIndex_);
        if (currentThread_) {
          store().markThreadRead(currentThread_->convId);
          mode_ = Mode::ThreadView;
          messageScrollOffset_ = 0;
          requestUpdate();
        }
      }
    }
  }
}

void MessagesActivity::handleThreadViewInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    mode_ = Mode::ThreadList;
    currentThread_ = nullptr;
    requestUpdate();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (messageScrollOffset_ > 0) {
      messageScrollOffset_--;
      requestUpdate();
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    if (currentThread_ && messageScrollOffset_ < static_cast<int>(currentThread_->messageCount) - kMessagesPerPage) {
      messageScrollOffset_++;
      requestUpdate();
    }
  }

  if (currentThread_ && currentThread_->quickReplyCount > 0) {
    for (int i = 0; i < currentThread_->quickReplyCount && i < 6; ++i) {
      if (mappedInput.wasPressed(static_cast<MappedInputManager::Button>(
              static_cast<int>(MappedInputManager::Button::PageBack) + i))) {
        sendQuickReply(i);
        break;
      }
    }
  }
}

void MessagesActivity::sendQuickReply(int index) {
  if (!currentThread_ || index < 0 || index >= currentThread_->quickReplyCount) return;

  uint8_t body[256];
  size_t len = encodeMsgQuickReply(body, sizeof(body), currentThread_->convId, static_cast<uint8_t>(index));
  if (len > 0 && ble::service().state() == ble::LinkState::Secured) {
    ble::service().send(MsgType::MsgQuickReply, body, len);
  }

  LOG_INF(TAG, "Sent quick reply %d for %s", index, currentThread_->convId);
}

void MessagesActivity::render(RenderLock&&) {
  renderer.clearScreen();

  switch (mode_) {
    case Mode::ThreadList: renderThreadList(); break;
    case Mode::ThreadView: renderThreadView(); break;
  }

  renderer.displayBuffer();
}

void MessagesActivity::renderThreadList() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int x = metrics.contentSidePadding;
  const size_t count = store().threadCount();

  char title[32];
  const int unread = store().unreadThreadCount();
  if (unread > 0) {
    snprintf(title, sizeof(title), "Messages (%d new)", unread);
  } else {
    snprintf(title, sizeof(title), "Messages (%zu)", count);
  }
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title, nullptr, true);

  if (count == 0) {
    const int centerY = renderer.getScreenHeight() / 2;
    renderer.drawCenteredText(UI_10_FONT_ID, centerY, "No messages");
    renderer.drawCenteredText(UI_10_FONT_ID, centerY + 30, "Pair phone to receive");
    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 16;
  const int maxY = renderer.getScreenHeight() - metrics.buttonHintsHeight - 10;

  for (int i = scrollOffset_; i < static_cast<int>(count) && y + lineHeight <= maxY; ++i) {
    const auto* thread = store().getThread(i);
    if (!thread) continue;

    const bool selected = (i == selectedIndex_);

    if (selected) {
      renderer.fillRect(0, y - 2, pageWidth, lineHeight, true);
    }

    if (thread->unread) {
      renderer.fillRect(x - 8, y + 4, 4, lineHeight - 8, !selected);
    }

    renderer.drawText(UI_10_FONT_ID, x, y + 2, thread->threadTitle, !selected);

    if (thread->messageCount > 0) {
      const auto& lastMsg = thread->messages[thread->messageCount - 1];
      char preview[64];
      size_t previewLen = std::strlen(lastMsg.text);
      if (previewLen > 45) previewLen = 45;
      std::memcpy(preview, lastMsg.text, previewLen);
      preview[previewLen] = '\0';
      if (previewLen == 45) std::strcpy(preview + 42, "...");

      renderer.drawText(UI_10_FONT_ID, x + 10, y + renderer.getLineHeight(UI_10_FONT_ID) + 4, preview, !selected);
    }

    y += lineHeight;
  }

  const auto labels = mappedInput.mapLabels("Back", "Open", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void MessagesActivity::renderThreadView() {
  if (!currentThread_) {
    mode_ = Mode::ThreadList;
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int x = metrics.contentSidePadding;
  const int contentWidth = pageWidth - 2 * metrics.contentSidePadding;

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                 currentThread_->threadTitle, nullptr, true);

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 8;
  const int maxY = renderer.getScreenHeight() - metrics.buttonHintsHeight - 
                   (currentThread_->quickReplyCount > 0 ? 40 : 10);

  for (int i = messageScrollOffset_; i < static_cast<int>(currentThread_->messageCount) && y + lineHeight <= maxY; ++i) {
    const auto& msg = currentThread_->messages[i];

    char header[96];
    if (msg.isOutgoing) {
      snprintf(header, sizeof(header), "You:");
    } else {
      snprintf(header, sizeof(header), "%s:", msg.sender);
    }

    const int msgX = msg.isOutgoing ? x + 20 : x;

    renderer.drawText(UI_10_FONT_ID, msgX, y, header);
    y += renderer.getLineHeight(UI_10_FONT_ID) + 2;

    const char* text = msg.text;
    size_t textLen = std::strlen(text);
    size_t pos = 0;
    while (pos < textLen && y + lineHeight <= maxY) {
      size_t lineEnd = pos;
      while (lineEnd < textLen && text[lineEnd] != '\n' && lineEnd - pos < 55) lineEnd++;

      char lineBuf[64];
      size_t lineLen = lineEnd - pos;
      if (lineLen > 63) lineLen = 63;
      std::memcpy(lineBuf, text + pos, lineLen);
      lineBuf[lineLen] = '\0';

      renderer.drawText(UI_10_FONT_ID, msgX + 5, y, lineBuf);
      y += lineHeight - 4;

      pos = lineEnd;
      if (pos < textLen && text[pos] == '\n') pos++;
    }

    y += 6;
  }

  if (currentThread_->quickReplyCount > 0) {
    const int qrY = renderer.getScreenHeight() - metrics.buttonHintsHeight - 35;
    renderer.drawText(UI_10_FONT_ID, x, qrY, "Quick replies:");
    int qrX = x + 90;
    for (uint8_t i = 0; i < currentThread_->quickReplyCount && i < 3; ++i) {
      char btn[32];
      snprintf(btn, sizeof(btn), "[%d]%s", i + 1, currentThread_->quickReplies[i]);
      renderer.drawText(UI_10_FONT_ID, qrX, qrY, btn);
      qrX += renderer.getTextWidth(UI_10_FONT_ID, btn) + 10;
    }
  }

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

}  // namespace margin

#endif  // MARGIN_BLE
