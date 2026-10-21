// SPDX-License-Identifier: MIT
// Messages Activity for margin companion on CrossPoint Reader.
// Displays message threads synced from phone via BLE.
#pragma once

#include "activities/Activity.h"

#ifdef MARGIN_BLE

#include "margin/companion_store.h"

namespace margin {

class MessagesActivity final : public Activity {
 public:
  explicit MessagesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~MessagesActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class Mode { ThreadList, ThreadView };

  void renderThreadList();
  void renderThreadView();
  void handleThreadListInput();
  void handleThreadViewInput();
  void sendQuickReply(int index);

  Mode mode_ = Mode::ThreadList;
  int selectedIndex_ = 0;
  int scrollOffset_ = 0;
  int messageScrollOffset_ = 0;
  const companion::StoredThread* currentThread_ = nullptr;
};

}  // namespace margin

#endif  // MARGIN_BLE
