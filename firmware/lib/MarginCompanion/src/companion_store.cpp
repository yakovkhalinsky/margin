// SPDX-License-Identifier: MIT
#include "margin/companion_store.h"

#if defined(ARDUINO) && defined(MARGIN_BLE)

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>

namespace margin::companion {

static constexpr const char* TAG = "COMP_STORE";
static constexpr const char* kBasePath = "/margin/companion";
static constexpr const char* kNotifPath = "/margin/companion/notifications.json";
static constexpr const char* kCalPath = "/margin/companion/calendar.json";
static constexpr const char* kSettingsPath = "/margin/companion/settings.json";
static constexpr const char* kThreadsDir = "/margin/companion/threads";

namespace {
uint8_t* psramAlloc(size_t n) {
  return static_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}
}  // namespace

CompanionStore& store() {
  static CompanionStore s;
  return s;
}

bool CompanionStore::init() {
  if (initialized_) return true;

  notifications_ = reinterpret_cast<StoredNotification*>(
      psramAlloc(sizeof(StoredNotification) * proto::kMaxNotifications));
  threads_ = reinterpret_cast<StoredThread*>(
      psramAlloc(sizeof(StoredThread) * proto::kMaxMessageThreads));
  events_ = reinterpret_cast<StoredCalendarEvent*>(
      psramAlloc(sizeof(StoredCalendarEvent) * proto::kMaxCalendarEvents));

  if (!notifications_ || !threads_ || !events_) {
    LOG_ERR(TAG, "Failed to allocate PSRAM for companion store");
    deinit();
    return false;
  }

  for (size_t i = 0; i < proto::kMaxNotifications; ++i) notifications_[i].clear();
  for (size_t i = 0; i < proto::kMaxMessageThreads; ++i) threads_[i].clear();
  for (size_t i = 0; i < proto::kMaxCalendarEvents; ++i) events_[i].clear();

  notifCount_ = 0;
  threadCount_ = 0;
  eventCount_ = 0;
  initialized_ = true;
  dirty_ = false;

  Storage.mkdir(kBasePath);
  Storage.mkdir(kThreadsDir);

  loadFromSd();

  LOG_INF(TAG, "Companion store initialized: %zu notifs, %zu threads, %zu events",
          notifCount_, threadCount_, eventCount_);
  return true;
}

void CompanionStore::deinit() {
  if (notifications_) { heap_caps_free(notifications_); notifications_ = nullptr; }
  if (threads_) { heap_caps_free(threads_); threads_ = nullptr; }
  if (events_) { heap_caps_free(events_); events_ = nullptr; }
  initialized_ = false;
}

void CompanionStore::copyStr(char* dst, size_t dstCap, const proto::Str& src) {
  copyStr(dst, dstCap, src.data, src.len);
}

void CompanionStore::copyStr(char* dst, size_t dstCap, const char* src, size_t srcLen) {
  size_t n = srcLen < dstCap - 1 ? srcLen : dstCap - 1;
  if (src && n > 0) std::memcpy(dst, src, n);
  dst[n] = '\0';
}

size_t CompanionStore::notificationCount() const { return notifCount_; }

const StoredNotification* CompanionStore::getNotification(size_t index) const {
  if (index >= notifCount_ || !notifications_) return nullptr;
  return &notifications_[index];
}

bool CompanionStore::addNotification(const proto::NotifPush& notif) {
  if (!initialized_ || !notifications_) return false;

  char keyBuf[kMaxNotifKeyLen];
  copyStr(keyBuf, sizeof(keyBuf), notif.notifKey);

  StoredNotification* existing = nullptr;
  for (size_t i = 0; i < notifCount_; ++i) {
    if (std::strcmp(notifications_[i].notifKey, keyBuf) == 0) {
      existing = &notifications_[i];
      break;
    }
  }

  if (!existing) {
    if (notifCount_ >= proto::kMaxNotifications) {
      std::memmove(&notifications_[0], &notifications_[1],
                   sizeof(StoredNotification) * (proto::kMaxNotifications - 1));
      existing = &notifications_[proto::kMaxNotifications - 1];
    } else {
      existing = &notifications_[notifCount_++];
    }
  }

  existing->clear();
  copyStr(existing->notifKey, sizeof(existing->notifKey), notif.notifKey);
  existing->notifId = notif.notifId;
  copyStr(existing->app, sizeof(existing->app), notif.app);
  copyStr(existing->appName, sizeof(existing->appName), notif.appName);
  copyStr(existing->title, sizeof(existing->title), notif.title);
  copyStr(existing->body, sizeof(existing->body), notif.body);
  existing->timestamp = notif.timestamp;
  existing->icon = notif.icon;
  existing->quickReplyCount = notif.quickReplyCount;
  for (uint8_t i = 0; i < notif.quickReplyCount && i < proto::kMaxQuickReplies; ++i) {
    copyStr(existing->quickReplies[i], sizeof(existing->quickReplies[i]),
            notif.quickReplies[i]);
  }
  existing->dismissed = false;

  dirty_ = true;
  return true;
}

bool CompanionStore::dismissNotification(const char* notifKey) {
  if (!initialized_ || !notifications_ || !notifKey) return false;

  for (size_t i = 0; i < notifCount_; ++i) {
    if (std::strcmp(notifications_[i].notifKey, notifKey) == 0) {
      if (i < notifCount_ - 1) {
        std::memmove(&notifications_[i], &notifications_[i + 1],
                     sizeof(StoredNotification) * (notifCount_ - i - 1));
      }
      notifications_[notifCount_ - 1].clear();
      notifCount_--;
      dirty_ = true;
      return true;
    }
  }
  return false;
}

void CompanionStore::clearAllNotifications() {
  if (!initialized_ || !notifications_) return;
  for (size_t i = 0; i < proto::kMaxNotifications; ++i) notifications_[i].clear();
  notifCount_ = 0;
  dirty_ = true;
}

int CompanionStore::unreadNotificationCount() const {
  int count = 0;
  for (size_t i = 0; i < notifCount_; ++i) {
    if (!notifications_[i].dismissed) count++;
  }
  return count;
}

size_t CompanionStore::threadCount() const { return threadCount_; }

const StoredThread* CompanionStore::getThread(size_t index) const {
  if (index >= threadCount_ || !threads_) return nullptr;
  return &threads_[index];
}

StoredThread* CompanionStore::findThread(const char* convId) {
  if (!initialized_ || !threads_ || !convId) return nullptr;
  for (size_t i = 0; i < threadCount_; ++i) {
    if (std::strcmp(threads_[i].convId, convId) == 0) {
      return &threads_[i];
    }
  }
  return nullptr;
}

bool CompanionStore::addOrUpdateThread(const proto::MsgThreadPush& thread) {
  if (!initialized_ || !threads_) return false;

  char convIdBuf[kMaxConvIdLen];
  copyStr(convIdBuf, sizeof(convIdBuf), thread.convId);

  StoredThread* existing = findThread(convIdBuf);
  if (!existing) {
    if (threadCount_ >= proto::kMaxMessageThreads) {
      uint64_t oldest = UINT64_MAX;
      size_t oldestIdx = 0;
      for (size_t i = 0; i < threadCount_; ++i) {
        if (threads_[i].lastUpdate < oldest) {
          oldest = threads_[i].lastUpdate;
          oldestIdx = i;
        }
      }
      existing = &threads_[oldestIdx];
    } else {
      existing = &threads_[threadCount_++];
    }
  }

  existing->clear();
  copyStr(existing->convId, sizeof(existing->convId), thread.convId);
  copyStr(existing->threadTitle, sizeof(existing->threadTitle), thread.threadTitle);
  copyStr(existing->app, sizeof(existing->app), thread.app);

  existing->messageCount = thread.messageCount;
  for (uint8_t i = 0; i < thread.messageCount && i < proto::kMaxMessagesPerThread; ++i) {
    const auto& src = thread.messages[i];
    auto& dst = existing->messages[i];
    copyStr(dst.sender, sizeof(dst.sender), src.sender);
    copyStr(dst.senderKey, sizeof(dst.senderKey), src.senderKey);
    copyStr(dst.text, sizeof(dst.text), src.text);
    dst.timestamp = src.timestamp;
    dst.isOutgoing = src.isOutgoing;
  }

  existing->quickReplyCount = thread.quickReplyCount;
  for (uint8_t i = 0; i < thread.quickReplyCount && i < proto::kMaxQuickReplies; ++i) {
    copyStr(existing->quickReplies[i], sizeof(existing->quickReplies[i]),
            thread.quickReplies[i]);
  }

  if (existing->messageCount > 0) {
    existing->lastUpdate = existing->messages[existing->messageCount - 1].timestamp;
  }
  existing->unread = true;

  dirty_ = true;
  return true;
}

bool CompanionStore::markThreadRead(const char* convId) {
  StoredThread* t = findThread(convId);
  if (!t) return false;
  t->unread = false;
  dirty_ = true;
  return true;
}

int CompanionStore::unreadThreadCount() const {
  int count = 0;
  for (size_t i = 0; i < threadCount_; ++i) {
    if (threads_[i].unread) count++;
  }
  return count;
}

size_t CompanionStore::eventCount() const { return eventCount_; }

const StoredCalendarEvent* CompanionStore::getEvent(size_t index) const {
  if (index >= eventCount_ || !events_) return nullptr;
  return &events_[index];
}

bool CompanionStore::syncCalendar(const proto::CalendarSync& cal) {
  if (!initialized_ || !events_) return false;

  eventCount_ = cal.eventCount;
  for (uint8_t i = 0; i < cal.eventCount && i < proto::kMaxCalendarEvents; ++i) {
    const auto& src = cal.events[i];
    auto& dst = events_[i];
    dst.eventId = src.eventId;
    copyStr(dst.title, sizeof(dst.title), src.title);
    dst.start = src.start;
    dst.end = src.end;
    copyStr(dst.location, sizeof(dst.location), src.location);
    dst.allDay = src.allDay;
  }

  std::sort(events_, events_ + eventCount_,
            [](const StoredCalendarEvent& a, const StoredCalendarEvent& b) {
              return a.start < b.start;
            });

  dirty_ = true;
  return true;
}

void CompanionStore::clearCalendar() {
  if (!initialized_ || !events_) return;
  for (size_t i = 0; i < proto::kMaxCalendarEvents; ++i) events_[i].clear();
  eventCount_ = 0;
  dirty_ = true;
}

void CompanionStore::updateTime(uint64_t utcMs, int32_t tzOffset, const char* tzName) {
  lastTimeSyncMs_ = utcMs;
  tzOffsetMinutes_ = tzOffset;
  if (tzName) {
    copyStr(tzName_, sizeof(tzName_), tzName, std::strlen(tzName));
  } else {
    tzName_[0] = '\0';
  }
  dirty_ = true;
}

bool CompanionStore::loadFromSd() {
  if (!initialized_) return false;

  HalFile file = Storage.open(kNotifPath, O_RDONLY);
  if (file) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();

    if (!err && doc.is<JsonArray>()) {
      JsonArray arr = doc.as<JsonArray>();
      notifCount_ = 0;
      for (JsonObject obj : arr) {
        if (notifCount_ >= proto::kMaxNotifications) break;
        auto& n = notifications_[notifCount_++];
        n.clear();
        strlcpy(n.notifKey, obj["key"] | "", sizeof(n.notifKey));
        n.notifId = obj["id"] | 0;
        strlcpy(n.app, obj["app"] | "", sizeof(n.app));
        strlcpy(n.appName, obj["appName"] | "", sizeof(n.appName));
        strlcpy(n.title, obj["title"] | "", sizeof(n.title));
        strlcpy(n.body, obj["body"] | "", sizeof(n.body));
        n.timestamp = obj["ts"] | 0ULL;
        n.icon = static_cast<proto::NotifIcon>(obj["icon"] | 0);
      }
    }
  }

  file = Storage.open(kCalPath, O_RDONLY);
  if (file) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();

    if (!err && doc.is<JsonArray>()) {
      JsonArray arr = doc.as<JsonArray>();
      eventCount_ = 0;
      for (JsonObject obj : arr) {
        if (eventCount_ >= proto::kMaxCalendarEvents) break;
        auto& e = events_[eventCount_++];
        e.clear();
        e.eventId = obj["id"] | 0;
        strlcpy(e.title, obj["title"] | "", sizeof(e.title));
        e.start = obj["start"] | 0ULL;
        e.end = obj["end"] | 0ULL;
        strlcpy(e.location, obj["loc"] | "", sizeof(e.location));
        e.allDay = obj["allDay"] | false;
      }
    }
  }

  file = Storage.open(kSettingsPath, O_RDONLY);
  if (file) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();

    if (!err) {
      lastTimeSyncMs_ = doc["lastTimeSync"] | 0ULL;
      tzOffsetMinutes_ = doc["tzOffset"] | 0;
      strlcpy(tzName_, doc["tzName"] | "", sizeof(tzName_));
    }
  }

  dirty_ = false;
  return true;
}

bool CompanionStore::saveToSd() {
  if (!initialized_ || !dirty_) return true;

  {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (size_t i = 0; i < notifCount_; ++i) {
      const auto& n = notifications_[i];
      JsonObject obj = arr.add<JsonObject>();
      obj["key"] = n.notifKey;
      obj["id"] = n.notifId;
      obj["app"] = n.app;
      obj["appName"] = n.appName;
      obj["title"] = n.title;
      obj["body"] = n.body;
      obj["ts"] = n.timestamp;
      obj["icon"] = static_cast<int>(n.icon);
    }
    HalFile file = Storage.open(kNotifPath, O_WRONLY | O_CREAT | O_TRUNC);
    if (file) {
      serializeJson(doc, file);
      file.close();
    }
  }

  {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (size_t i = 0; i < eventCount_; ++i) {
      const auto& e = events_[i];
      JsonObject obj = arr.add<JsonObject>();
      obj["id"] = e.eventId;
      obj["title"] = e.title;
      obj["start"] = e.start;
      obj["end"] = e.end;
      obj["loc"] = e.location;
      obj["allDay"] = e.allDay;
    }
    HalFile file = Storage.open(kCalPath, O_WRONLY | O_CREAT | O_TRUNC);
    if (file) {
      serializeJson(doc, file);
      file.close();
    }
  }

  {
    JsonDocument doc;
    doc["lastTimeSync"] = lastTimeSyncMs_;
    doc["tzOffset"] = tzOffsetMinutes_;
    doc["tzName"] = tzName_;
    HalFile file = Storage.open(kSettingsPath, O_WRONLY | O_CREAT | O_TRUNC);
    if (file) {
      serializeJson(doc, file);
      file.close();
    }
  }

  dirty_ = false;
  LOG_INF(TAG, "Companion store saved to SD");
  return true;
}

}  // namespace margin::companion

#else  // Stub for host tests

namespace margin::companion {

static CompanionStore sStubStore;
CompanionStore& store() { return sStubStore; }

bool CompanionStore::init() { return false; }
void CompanionStore::deinit() {}
size_t CompanionStore::notificationCount() const { return 0; }
const StoredNotification* CompanionStore::getNotification(size_t) const { return nullptr; }
bool CompanionStore::addNotification(const proto::NotifPush&) { return false; }
bool CompanionStore::dismissNotification(const char*) { return false; }
void CompanionStore::clearAllNotifications() {}
int CompanionStore::unreadNotificationCount() const { return 0; }
size_t CompanionStore::threadCount() const { return 0; }
const StoredThread* CompanionStore::getThread(size_t) const { return nullptr; }
StoredThread* CompanionStore::findThread(const char*) { return nullptr; }
bool CompanionStore::addOrUpdateThread(const proto::MsgThreadPush&) { return false; }
bool CompanionStore::markThreadRead(const char*) { return false; }
int CompanionStore::unreadThreadCount() const { return 0; }
size_t CompanionStore::eventCount() const { return 0; }
const StoredCalendarEvent* CompanionStore::getEvent(size_t) const { return nullptr; }
bool CompanionStore::syncCalendar(const proto::CalendarSync&) { return false; }
void CompanionStore::clearCalendar() {}
void CompanionStore::updateTime(uint64_t, int32_t, const char*) {}
bool CompanionStore::loadFromSd() { return false; }
bool CompanionStore::saveToSd() { return false; }
void CompanionStore::copyStr(char*, size_t, const proto::Str&) {}
void CompanionStore::copyStr(char*, size_t, const char*, size_t) {}

}  // namespace margin::companion

#endif
