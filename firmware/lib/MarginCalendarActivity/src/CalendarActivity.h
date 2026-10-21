// SPDX-License-Identifier: MIT
// Calendar Activity for margin companion on CrossPoint Reader.
// Displays calendar events synced from phone via BLE.
#pragma once

#include "activities/Activity.h"

#ifdef MARGIN_BLE

#include "margin/companion_store.h"

namespace margin {

class CalendarActivity final : public Activity {
 public:
  explicit CalendarActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~CalendarActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Mode { Agenda, EventDetail };

  void renderAgenda();
  void renderEventDetail();
  void handleAgendaInput();
  void handleEventDetailInput();
  void requestRefresh();

  void formatTime(char* buf, size_t bufSize, uint64_t timestampMs, bool includeDate);
  void formatDate(char* buf, size_t bufSize, uint64_t timestampMs);
  bool isSameDay(uint64_t ts1, uint64_t ts2);

  Mode mode_ = Mode::Agenda;
  int selectedIndex_ = 0;
  int scrollOffset_ = 0;
  int detailScrollOffset_ = 0;
};

}  // namespace margin

#endif  // MARGIN_BLE
