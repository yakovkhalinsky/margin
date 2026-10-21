// SPDX-License-Identifier: MIT
#include "NotificationsActivity.h"

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

static constexpr const char* TAG = "NOTIF_ACT";
static constexpr int kItemsPerPage = 8;

NotificationsActivity::NotificationsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Notifications", renderer, mappedInput) {}

void NotificationsActivity::onEnter() {
  Activity::onEnter();
  mode_ = Mode::List;
  selectedIndex_ = 0;
  scrollOffset_ = 0;
  detailScrollOffset_ = 0;
  requestUpdateAndWait();
}

void NotificationsActivity::onExit() {
  store().saveToSd();
  Activity::onExit();
}

void NotificationsActivity::loop() {
  switch (mode_) {
    case Mode::List: handleListInput(); break;
    case Mode::Detail: handleDetailInput(); break;
  }
}

void NotificationsActivity::handleListInput() {
  const size_t count = store().notificationCount();

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
      if (selectedIndex_ >= scrollOffset_ + kItemsPerPage) scrollOffset_ = selectedIndex_ - kItemsPerPage + 1;
      requestUpdate();
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (count > 0) {
      mode_ = Mode::Detail;
      detailScrollOffset_ = 0;
      requestUpdate();
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::PageBack)) {
    dismissCurrent();
  }

  int x = 0, y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 12;
    const int startY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int tappedIndex = (y - startY) / lineHeight;
    
    if (tappedIndex >= 0 && tappedIndex < kItemsPerPage) {
      const int actualIndex = scrollOffset_ + tappedIndex;
      if (actualIndex < static_cast<int>(count)) {
        selectedIndex_ = actualIndex;
        mode_ = Mode::Detail;
        detailScrollOffset_ = 0;
        requestUpdate();
      }
    }
  }
}

void NotificationsActivity::handleDetailInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    mode_ = Mode::List;
    requestUpdate();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    if (detailScrollOffset_ > 0) {
      detailScrollOffset_--;
      requestUpdate();
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    detailScrollOffset_++;
    requestUpdate();
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::PageBack)) {
    dismissCurrent();
    mode_ = Mode::List;
    requestUpdate();
  }
}

void NotificationsActivity::dismissCurrent() {
  const size_t count = store().notificationCount();
  if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(count)) return;

  const auto* notif = store().getNotification(selectedIndex_);
  if (!notif) return;

  uint8_t body[256];
  size_t len = encodeNotifDismiss(body, sizeof(body), notif->notifKey);
  if (len > 0 || notif->notifKey[0] == '\0') {
    if (ble::service().state() == ble::LinkState::Secured) {
      ble::service().send(MsgType::NotifDismiss, body, len);
    }
  }

  store().dismissNotification(notif->notifKey);
  if (selectedIndex_ >= static_cast<int>(store().notificationCount()) && selectedIndex_ > 0) {
    selectedIndex_--;
  }
  requestUpdate();
}

void NotificationsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  switch (mode_) {
    case Mode::List: renderList(); break;
    case Mode::Detail: renderDetail(); break;
  }

  renderer.displayBuffer();
}

void NotificationsActivity::renderList() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int x = metrics.contentSidePadding;
  const size_t count = store().notificationCount();

  char title[32];
  snprintf(title, sizeof(title), "Notifications (%zu)", count);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title, nullptr, true);

  if (count == 0) {
    const int centerY = renderer.getScreenHeight() / 2;
    renderer.drawCenteredText(UI_10_FONT_ID, centerY, "No notifications");
    renderer.drawCenteredText(UI_10_FONT_ID, centerY + 30, "Pair phone to receive");
    const auto labels = mappedInput.mapLabels("Back", "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 12;
  const int maxY = renderer.getScreenHeight() - metrics.buttonHintsHeight - 10;
  const int contentWidth = pageWidth - 2 * metrics.contentSidePadding;

  for (int i = scrollOffset_; i < static_cast<int>(count) && y + lineHeight <= maxY; ++i) {
    const auto* notif = store().getNotification(i);
    if (!notif) continue;

    const bool selected = (i == selectedIndex_);

    if (selected) {
      renderer.fillRect(0, y - 2, pageWidth, lineHeight, true);
    }

    char line[128];
    snprintf(line, sizeof(line), "%s: %s",
             notif->appName[0] ? notif->appName : notif->app,
             notif->title);
    
    renderer.drawText(UI_10_FONT_ID, x, y + 2, line, !selected);
    
    char preview[64];
    size_t previewLen = std::strlen(notif->body);
    if (previewLen > 50) previewLen = 50;
    std::memcpy(preview, notif->body, previewLen);
    preview[previewLen] = '\0';
    if (previewLen == 50) std::strcpy(preview + 47, "...");
    
    renderer.drawText(UI_10_FONT_ID, x + 10, y + renderer.getLineHeight(UI_10_FONT_ID) + 4, preview, !selected);

    y += lineHeight;
  }

  if (count > static_cast<size_t>(kItemsPerPage)) {
    char scroll[16];
    snprintf(scroll, sizeof(scroll), "%d/%zu", selectedIndex_ + 1, count);
    renderer.drawText(UI_10_FONT_ID, pageWidth - 50, metrics.topPadding + 5, scroll);
  }

  const auto labels = mappedInput.mapLabels("Back", "View", "Dismiss", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void NotificationsActivity::renderDetail() {
  const auto* notif = store().getNotification(selectedIndex_);
  if (!notif) {
    mode_ = Mode::List;
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int x = metrics.contentSidePadding;
  const int contentWidth = pageWidth - 2 * metrics.contentSidePadding;

  const char* appDisplay = notif->appName[0] ? notif->appName : notif->app;
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, appDisplay, nullptr, true);

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing - detailScrollOffset_ * 20;

  renderer.drawText(UI_12_FONT_ID, x, y, notif->title);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 10;

  const char* body = notif->body;
  size_t bodyLen = std::strlen(body);
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int maxY = renderer.getScreenHeight() - metrics.buttonHintsHeight - 10;
  
  size_t pos = 0;
  while (pos < bodyLen && y < maxY) {
    size_t lineEnd = pos;
    while (lineEnd < bodyLen && body[lineEnd] != '\n' && lineEnd - pos < 60) lineEnd++;
    
    char lineBuf[64];
    size_t lineLen = lineEnd - pos;
    if (lineLen > 63) lineLen = 63;
    std::memcpy(lineBuf, body + pos, lineLen);
    lineBuf[lineLen] = '\0';
    
    if (y > metrics.topPadding + metrics.headerHeight) {
      renderer.drawText(UI_10_FONT_ID, x, y, lineBuf);
    }
    
    y += lineHeight + 2;
    pos = lineEnd;
    if (pos < bodyLen && body[pos] == '\n') pos++;
  }

  if (notif->quickReplyCount > 0) {
    y += 10;
    renderer.drawText(UI_10_FONT_ID, x, y, "Quick replies:");
    y += lineHeight;
    for (uint8_t i = 0; i < notif->quickReplyCount && i < proto::kMaxQuickReplies; ++i) {
      char opt[72];
      snprintf(opt, sizeof(opt), "  %d. %s", i + 1, notif->quickReplies[i]);
      renderer.drawText(UI_10_FONT_ID, x, y, opt);
      y += lineHeight;
    }
  }

  const auto labels = mappedInput.mapLabels("Back", "", "Dismiss", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

}  // namespace margin

#endif  // MARGIN_BLE
