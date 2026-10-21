// SPDX-License-Identifier: MIT
// Companion manager: coordinates BLE, store, and message handling.
// BLE is initialized lazily (not at boot) and failures are non-fatal.
#pragma once

#include "margin/ble_service.h"
#include "margin/companion_settings.h"
#include "margin/companion_store.h"
#include "margin/messages.h"

namespace margin::companion {

class CompanionManager : public ble::Handler {
 public:
  bool init();
  void deinit();

  void poll(uint32_t nowMs);

  bool isBleInitialized() const { return bleInitialized_; }
  bool isBleConnected() const;
  bool isBleEnabled() const { return settings().bleEnabled(); }

  bool enableBle(bool enable);
  bool startPairing(uint32_t durationMs = 60000);
  void confirmPairing(bool accept);
  void forgetPhone();

  int unreadNotificationCount() const;
  int unreadMessageCount() const;

  void onMessage(const proto::Message& msg) override;
  uint32_t onFileChunk(const proto::FileChunk& chunk) override;
  void onPairingRequest(uint32_t passkey) override;
  void onLinkState(ble::LinkState state) override;

  uint32_t pendingPasskey() const { return pendingPasskey_; }
  bool hasPendingPairing() const { return pendingPasskey_ != 0; }

 private:
  bool tryInitBle();
  void handleNotifPush(const uint8_t* body, size_t len);
  void handleMsgThreadPush(const uint8_t* body, size_t len);
  void handleCalendarSync(const uint8_t* body, size_t len);
  void handleTimeSync(const uint8_t* body, size_t len);

  bool initialized_ = false;
  bool bleInitialized_ = false;
  bool bleInitFailed_ = false;
  uint32_t pendingPasskey_ = 0;
  uint32_t lastSaveMs_ = 0;
  static constexpr uint32_t kSaveIntervalMs = 300000;  // 5 minutes
};

CompanionManager& manager();

}  // namespace margin::companion
