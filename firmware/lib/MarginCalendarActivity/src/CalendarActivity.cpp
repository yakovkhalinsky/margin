// SPDX-License-Identifier: MIT
#include "CalendarActivity.h"

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

static constexpr const char* TAG = "CAL_ACT";
static constexpr int kEventsPerPage = 6;
static constexpr uint64_t kMsPerDay = 86400000ULL;

static const char* kDayNames[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char* kMonthNames[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

CalendarActivity::CalendarActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Calendar", renderer, mappedInput) {}

void CalendarActivity::onEnter() {
  Activity::onEnter();
  mode_ = Mode::Agenda;
  selectedIndex_ = 0;
  scrollOffset_ = 0;
  detailScrollOffset_ = 0;
  requestUpdateAndWait();
}

void CalendarActivity::onExit() {
  Activity::onExit();
}

void CalendarActivity::loop() {
  switch (mode_) {
    case Mode::Agenda: handleAgendaInput(); break;
    case Mode::EventDetail: handleEventDetailInput(); break;
  }
}

void CalendarActivity::handleAgendaInput() {
  const size_t count = store().eventCount();

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
      if (selectedIndex_ >= scrollOffset_ + kEventsPerPage) scrollOffset_ = selectedIndex_ - kEventsPerPage + 1;
      requestUpdate();
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (count > 0) {
      mode_ = Mode::EventDetail;
      detailScrollOffset_ = 0;
      requestUpdate();
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::PageBack)) {
    requestRefresh();
  }

  int x = 0, y = 0;
  if (mappedInput.wasScreenTapped(x, y)) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 16;
    const int startY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int tappedIndex = (y - startY) / lineHeight;
    
    if (tappedIndex >= 0 && tappedIndex < kEventsPerPage) {
      const int actualIndex = scrollOffset_ + tappedIndex;
      if (actualIndex < static_cast<int>(count)) {
        selectedIndex_ = actualIndex;
        mode_ = Mode::EventDetail;
        detailScrollOffset_ = 0;
        requestUpdate();
      }
    }
  }
}

void CalendarActivity::handleEventDetailInput() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    mode_ = Mode::Agenda;
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
}

void CalendarActivity::requestRefresh() {
  if (ble::service().state() != ble::LinkState::Secured) return;

  uint8_t body[8];
  size_t len = encodeCalendarRequest(body, sizeof(body));
  ble::service().send(MsgType::CalendarRequest, body, len);
  LOG_INF(TAG, "Calendar refresh requested");
}

void CalendarActivity::formatTime(char* buf, size_t bufSize, uint64_t timestampMs, bool includeDate) {
  uint64_t localMs = timestampMs + static_cast<int64_t>(store().tzOffsetMinutes()) * 60000LL;
  uint64_t secs = localMs / 1000;
  int hours = (secs / 3600) % 24;
  int mins = (secs / 60) % 60;

  if (includeDate) {
    uint64_t days = secs / 86400;
    int dayOfWeek = (days + 4) % 7;
    
    int y = 1970;
    while (true) {
      int daysInYear = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
      if (days < static_cast<uint64_t>(daysInYear)) break;
      days -= daysInYear;
      y++;
    }

    static const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int m = 0;
    bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    while (m < 12) {
      int dim = daysInMonth[m] + (m == 1 && leap ? 1 : 0);
      if (days < static_cast<uint64_t>(dim)) break;
      days -= dim;
      m++;
    }
    int d = static_cast<int>(days) + 1;

    snprintf(buf, bufSize, "%s %s %d, %02d:%02d", kDayNames[dayOfWeek], kMonthNames[m], d, hours, mins);
  } else {
    snprintf(buf, bufSize, "%02d:%02d", hours, mins);
  }
}

void CalendarActivity::formatDate(char* buf, size_t bufSize, uint64_t timestampMs) {
  uint64_t localMs = timestampMs + static_cast<int64_t>(store().tzOffsetMinutes()) * 60000LL;
  uint64_t secs = localMs / 1000;
  uint64_t days = secs / 86400;
  int dayOfWeek = (days + 4) % 7;

  int y = 1970;
  while (true) {
    int daysInYear = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
    if (days < static_cast<uint64_t>(daysInYear)) break;
    days -= daysInYear;
    y++;
  }

  static const int daysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int m = 0;
  bool leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
  while (m < 12) {
    int dim = daysInMonth[m] + (m == 1 && leap ? 1 : 0);
    if (days < static_cast<uint64_t>(dim)) break;
    days -= dim;
    m++;
  }
  int d = static_cast<int>(days) + 1;

  snprintf(buf, bufSize, "%s, %s %d", kDayNames[dayOfWeek], kMonthNames[m], d);
}

bool CalendarActivity::isSameDay(uint64_t ts1, uint64_t ts2) {
  int64_t offset = static_cast<int64_t>(store().tzOffsetMinutes()) * 60000LL;
  uint64_t day1 = (ts1 + offset) / kMsPerDay;
  uint64_t day2 = (ts2 + offset) / kMsPerDay;
  return day1 == day2;
}

void CalendarActivity::render(RenderLock&&) {
  renderer.clearScreen();

  switch (mode_) {
    case Mode::Agenda: renderAgenda(); break;
    case Mode::EventDetail: renderEventDetail(); break;
  }

  renderer.displayBuffer();
}

void CalendarActivity::renderAgenda() {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int x = metrics.contentSidePadding;
  const size_t count = store().eventCount();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Calendar", nullptr, true);

  if (count == 0) {
    const int centerY = renderer.getScreenHeight() / 2;
    renderer.drawCenteredText(UI_10_FONT_ID, centerY, "No upcoming events");
    renderer.drawCenteredText(UI_10_FONT_ID, centerY + 30, "Pair phone to sync calendar");
    const auto labels = mappedInput.mapLabels("Back", "", "Refresh", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    return;
  }

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 16;
  const int dateLineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 8;
  const int maxY = renderer.getScreenHeight() - metrics.buttonHintsHeight - 10;

  uint64_t lastDate = 0;

  for (int i = scrollOffset_; i < static_cast<int>(count) && y + lineHeight <= maxY; ++i) {
    const auto* event = store().getEvent(i);
    if (!event) continue;

    if (lastDate == 0 || !isSameDay(lastDate, event->start)) {
      char dateStr[32];
      formatDate(dateStr, sizeof(dateStr), event->start);
      renderer.drawText(UI_10_FONT_ID, x, y, dateStr);
      y += dateLineHeight;
      lastDate = event->start;
    }

    if (y + lineHeight > maxY) break;

    const bool selected = (i == selectedIndex_);

    if (selected) {
      renderer.fillRect(0, y - 2, pageWidth, lineHeight - 4, true);
    }

    char timeStr[16];
    if (event->allDay) {
      snprintf(timeStr, sizeof(timeStr), "All day");
    } else {
      formatTime(timeStr, sizeof(timeStr), event->start, false);
    }

    char line[128];
    snprintf(line, sizeof(line), "%s  %s", timeStr, event->title);

    renderer.drawText(UI_10_FONT_ID, x + 10, y + 2, line, !selected);

    if (event->location[0]) {
      char loc[48];
      snprintf(loc, sizeof(loc), "  @ %s", event->location);
      size_t locLen = std::strlen(loc);
      if (locLen > 40) { locLen = 40; loc[40] = '\0'; std::strcpy(loc + 37, "..."); }
      renderer.drawText(UI_10_FONT_ID, x + 20, y + renderer.getLineHeight(UI_10_FONT_ID) + 4, loc, !selected);
    }

    y += lineHeight;
  }

  const auto labels = mappedInput.mapLabels("Back", "View", "Refresh", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void CalendarActivity::renderEventDetail() {
  const auto* event = store().getEvent(selectedIndex_);
  if (!event) {
    mode_ = Mode::Agenda;
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int x = metrics.contentSidePadding;

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, "Event", nullptr, true);

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing - detailScrollOffset_ * 20;

  renderer.drawText(UI_12_FONT_ID, x, y, event->title);
  y += renderer.getLineHeight(UI_12_FONT_ID) + 15;

  renderer.drawText(UI_10_FONT_ID, x, y, "When:");
  y += renderer.getLineHeight(UI_10_FONT_ID) + 4;

  char startStr[48], endStr[48];
  if (event->allDay) {
    formatDate(startStr, sizeof(startStr), event->start);
    snprintf(endStr, sizeof(endStr), "All day");
  } else {
    formatTime(startStr, sizeof(startStr), event->start, true);
    formatTime(endStr, sizeof(endStr), event->end, isSameDay(event->start, event->end) ? false : true);
  }

  char whenLine[96];
  if (event->allDay) {
    snprintf(whenLine, sizeof(whenLine), "  %s (All day)", startStr);
  } else if (isSameDay(event->start, event->end)) {
    snprintf(whenLine, sizeof(whenLine), "  %s - %s", startStr, endStr);
  } else {
    snprintf(whenLine, sizeof(whenLine), "  %s", startStr);
    renderer.drawText(UI_10_FONT_ID, x + 10, y, whenLine);
    y += renderer.getLineHeight(UI_10_FONT_ID) + 2;
    snprintf(whenLine, sizeof(whenLine), "  to %s", endStr);
  }
  renderer.drawText(UI_10_FONT_ID, x + 10, y, whenLine);
  y += renderer.getLineHeight(UI_10_FONT_ID) + 15;

  if (event->location[0]) {
    renderer.drawText(UI_10_FONT_ID, x, y, "Where:");
    y += renderer.getLineHeight(UI_10_FONT_ID) + 4;

    const char* loc = event->location;
    size_t locLen = std::strlen(loc);
    size_t pos = 0;
    while (pos < locLen) {
      size_t lineEnd = pos;
      while (lineEnd < locLen && loc[lineEnd] != '\n' && lineEnd - pos < 50) lineEnd++;

      char lineBuf[64];
      size_t lineLen = lineEnd - pos;
      if (lineLen > 63) lineLen = 63;
      std::memcpy(lineBuf, loc + pos, lineLen);
      lineBuf[lineLen] = '\0';

      renderer.drawText(UI_10_FONT_ID, x + 10, y, lineBuf);
      y += renderer.getLineHeight(UI_10_FONT_ID) + 2;

      pos = lineEnd;
      if (pos < locLen && loc[pos] == '\n') pos++;
    }
  }

  const auto labels = mappedInput.mapLabels("Back", "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

}  // namespace margin

#endif  // MARGIN_BLE
