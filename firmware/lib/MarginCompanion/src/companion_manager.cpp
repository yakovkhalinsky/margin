// SPDX-License-Identifier: MIT
#include "margin/companion_manager.h"

#if defined(ARDUINO) && defined(MARGIN_BLE)

#include <Logging.h>

namespace margin::companion {

static constexpr const char* TAG = "COMP_MGR";

CompanionManager& manager() {
  static CompanionManager cm;
  return cm;
}

bool CompanionManager::init() {
  if (initialized_) return true;

  LOG_INF(TAG, "Initializing companion manager...");

  if (!settings().load()) {
    LOG_INF(TAG, "Could not load settings, using defaults (BLE off)");
  }

  if (!store().init()) {
    LOG_ERR(TAG, "Failed to initialize companion store");
    return false;
  }

  initialized_ = true;
  LOG_INF(TAG, "Companion manager initialized (BLE %s)",
          settings().bleEnabled() ? "enabled" : "disabled");
  return true;
}

void CompanionManager::deinit() {
  if (!initialized_) return;

  if (bleInitialized_) {
    ble::service().end();
    bleInitialized_ = false;
  }

  store().saveToSd();
  store().deinit();
  initialized_ = false;
}

bool CompanionManager::tryInitBle() {
  if (bleInitialized_) return true;
  if (bleInitFailed_) return false;  // Don't retry after failure
  if (!settings().bleEnabled()) return false;

  LOG_INF(TAG, "Lazy-initializing BLE service...");

  ble::Config cfg;
  cfg.deviceName = "margin";
  cfg.rxCapacity = proto::kMaxBodySize;
  cfg.txCapacity = proto::kMaxBodySize;

  if (!ble::service().begin(cfg, this)) {
    LOG_ERR(TAG, "BLE initialization failed (non-fatal, continuing without BLE)");
    bleInitFailed_ = true;
    return false;
  }

  bleInitialized_ = true;
  LOG_INF(TAG, "BLE service initialized successfully");
  return true;
}

bool CompanionManager::enableBle(bool enable) {
  if (enable == settings().bleEnabled()) return true;

  settings().setBleEnabled(enable);

  if (enable) {
    bleInitFailed_ = false;  // Allow retry
    return tryInitBle();
  } else {
    if (bleInitialized_) {
      ble::service().end();
      bleInitialized_ = false;
    }
    return true;
  }
}

bool CompanionManager::startPairing(uint32_t durationMs) {
  if (!tryInitBle()) return false;
  ble::service().openPairingWindow(durationMs);
  LOG_INF(TAG, "Pairing window opened for %u ms", durationMs);
  return true;
}

void CompanionManager::confirmPairing(bool accept) {
  if (bleInitialized_) {
    ble::service().confirmPairing(accept);
  }
  pendingPasskey_ = 0;
}

void CompanionManager::forgetPhone() {
  if (bleInitialized_) {
    ble::service().forgetAllBonds();
    LOG_INF(TAG, "All phone bonds deleted");
  }
}

bool CompanionManager::isBleConnected() const {
  return bleInitialized_ && ble::service().state() == ble::LinkState::Secured;
}

int CompanionManager::unreadNotificationCount() const {
  return store().unreadNotificationCount();
}

int CompanionManager::unreadMessageCount() const {
  return store().unreadThreadCount();
}

void CompanionManager::poll(uint32_t nowMs) {
  if (!initialized_) return;

  if (settings().bleEnabled() && !bleInitialized_ && !bleInitFailed_) {
    tryInitBle();
  }

  if (bleInitialized_) {
    ble::service().poll(nowMs);
  }

  if (store().isDirty() && nowMs - lastSaveMs_ > kSaveIntervalMs) {
    store().saveToSd();
    lastSaveMs_ = nowMs;
  }
}

void CompanionManager::onMessage(const proto::Message& msg) {
  LOG_DBG(TAG, "Received message type 0x%02X, len %u",
          static_cast<int>(msg.type), msg.len);

  switch (msg.type) {
    case proto::MsgType::NotifPush:
      handleNotifPush(msg.body, msg.len);
      break;
    case proto::MsgType::MsgThreadPush:
      handleMsgThreadPush(msg.body, msg.len);
      break;
    case proto::MsgType::CalendarSync:
      handleCalendarSync(msg.body, msg.len);
      break;
    case proto::MsgType::TimeSync:
      handleTimeSync(msg.body, msg.len);
      break;
    case proto::MsgType::NotifDismissAck:
    case proto::MsgType::MsgQuickReplyAck:
      LOG_DBG(TAG, "ACK received");
      break;
    default:
      LOG_DBG(TAG, "Unhandled message type: 0x%02X", static_cast<int>(msg.type));
      break;
  }
}

uint32_t CompanionManager::onFileChunk(const proto::FileChunk& chunk) {
  return chunk.offset + chunk.len;
}

void CompanionManager::onPairingRequest(uint32_t passkey) {
  pendingPasskey_ = passkey;
  LOG_INF(TAG, "Pairing request received, passkey: %06u", passkey);
}

void CompanionManager::onLinkState(ble::LinkState state) {
  const char* stateStr = "unknown";
  switch (state) {
    case ble::LinkState::Off: stateStr = "off"; break;
    case ble::LinkState::Advertising: stateStr = "advertising"; break;
    case ble::LinkState::Connected: stateStr = "connected"; break;
    case ble::LinkState::Secured: stateStr = "secured"; break;
  }
  LOG_INF(TAG, "BLE link state: %s", stateStr);

  if (state == ble::LinkState::Secured) {
    uint8_t body[8];
    size_t len = proto::encodeTimeRequest(body, sizeof(body));
    ble::service().send(proto::MsgType::TimeRequest, body, len);
  }
}

void CompanionManager::handleNotifPush(const uint8_t* body, size_t len) {
  if (!settings().syncNotifications()) return;

  proto::NotifPush notif;
  if (!proto::decodeNotifPush(body, len, notif)) {
    LOG_INF(TAG, "Failed to decode NotifPush");
    return;
  }

  store().addNotification(notif);
  LOG_INF(TAG, "Notification added: %.*s", static_cast<int>(notif.title.len), notif.title.data);
}

void CompanionManager::handleMsgThreadPush(const uint8_t* body, size_t len) {
  if (!settings().syncMessages()) return;

  proto::MsgThreadPush thread;
  if (!proto::decodeMsgThreadPush(body, len, thread)) {
    LOG_INF(TAG, "Failed to decode MsgThreadPush");
    return;
  }

  store().addOrUpdateThread(thread);
  LOG_INF(TAG, "Message thread updated: %.*s (%u msgs)",
          static_cast<int>(thread.threadTitle.len), thread.threadTitle.data, thread.messageCount);
}

void CompanionManager::handleCalendarSync(const uint8_t* body, size_t len) {
  if (!settings().syncCalendar()) return;

  proto::CalendarSync cal;
  if (!proto::decodeCalendarSync(body, len, cal)) {
    LOG_INF(TAG, "Failed to decode CalendarSync");
    return;
  }

  store().syncCalendar(cal);
  LOG_INF(TAG, "Calendar synced: %u events", cal.eventCount);
}

void CompanionManager::handleTimeSync(const uint8_t* body, size_t len) {
  proto::TimeSync ts;
  if (!proto::decodeTimeSync(body, len, ts)) {
    LOG_INF(TAG, "Failed to decode TimeSync");
    return;
  }

  store().updateTime(ts.utcMs, ts.tzOffsetMinutes,
                     ts.tzName.data ? std::string(ts.tzName.data, ts.tzName.len).c_str() : nullptr);
  LOG_INF(TAG, "Time synced: %llu UTC, offset %d min", ts.utcMs, ts.tzOffsetMinutes);
}

}  // namespace margin::companion

#else  // Stub for non-BLE builds or host tests

namespace margin::companion {

static CompanionManager sStubManager;
CompanionManager& manager() { return sStubManager; }

bool CompanionManager::init() { return false; }
void CompanionManager::deinit() {}
void CompanionManager::poll(uint32_t) {}
bool CompanionManager::isBleConnected() const { return false; }
bool CompanionManager::enableBle(bool) { return false; }
bool CompanionManager::startPairing(uint32_t) { return false; }
void CompanionManager::confirmPairing(bool) {}
void CompanionManager::forgetPhone() {}
int CompanionManager::unreadNotificationCount() const { return 0; }
int CompanionManager::unreadMessageCount() const { return 0; }
void CompanionManager::onMessage(const proto::Message&) {}
uint32_t CompanionManager::onFileChunk(const proto::FileChunk&) { return 0; }
void CompanionManager::onPairingRequest(uint32_t) {}
void CompanionManager::onLinkState(ble::LinkState) {}
void CompanionManager::handleNotifPush(const uint8_t*, size_t) {}
void CompanionManager::handleMsgThreadPush(const uint8_t*, size_t) {}
void CompanionManager::handleCalendarSync(const uint8_t*, size_t) {}
void CompanionManager::handleTimeSync(const uint8_t*, size_t) {}

}  // namespace margin::companion

#endif
