// SPDX-License-Identifier: MIT
// Notifications Activity for margin companion on CrossPoint Reader.
// Displays phone notifications synced via BLE.
#pragma once

#include "activities/Activity.h"

#ifdef MARGIN_BLE

#include "margin/companion_store.h"

namespace margin {

class NotificationsActivity final : public Activity {
 public:
  explicit NotificationsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~NotificationsActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Mode { List, Detail };

  void renderList();
  void renderDetail();
  void handleListInput();
  void handleDetailInput();
  void dismissCurrent();

  Mode mode_ = Mode::List;
  int selectedIndex_ = 0;
  int scrollOffset_ = 0;
  int detailScrollOffset_ = 0;
};

}  // namespace margin

#endif  // MARGIN_BLE
