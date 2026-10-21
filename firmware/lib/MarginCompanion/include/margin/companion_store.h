// SPDX-License-Identifier: MIT
// Companion data store for notifications, messages, and calendar.
// Data is stored in PSRAM at runtime and persisted to SD card periodically.
// See docs/companion-v1.md for the storage architecture.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "margin/messages.h"

namespace margin::companion {

static constexpr size_t kMaxNotifKeyLen = 128;
static constexpr size_t kMaxNotifTitleLen = 256;
static constexpr size_t kMaxNotifBodyLen = 1024;
static constexpr size_t kMaxAppNameLen = 64;
static constexpr size_t kMaxConvIdLen = 128;
static constexpr size_t kMaxSenderLen = 64;
static constexpr size_t kMaxMsgTextLen = 512;
static constexpr size_t kMaxLocationLen = 128;
static constexpr size_t kMaxTzNameLen = 48;

struct StoredNotification {
  char notifKey[kMaxNotifKeyLen];
  uint32_t notifId;
  char app[kMaxAppNameLen];
  char appName[kMaxAppNameLen];
  char title[kMaxNotifTitleLen];
  char body[kMaxNotifBodyLen];
  uint64_t timestamp;
  proto::NotifIcon icon;
  uint8_t quickReplyCount;
  char quickReplies[proto::kMaxQuickReplies][64];
  bool dismissed;

  void clear() { std::memset(this, 0, sizeof(*this)); }
  bool empty() const { return notifKey[0] == '\0'; }
};

struct StoredMessage {
  char sender[kMaxSenderLen];
  char senderKey[kMaxSenderLen];
  char text[kMaxMsgTextLen];
  uint64_t timestamp;
  bool isOutgoing;

  void clear() { std::memset(this, 0, sizeof(*this)); }
  bool empty() const { return text[0] == '\0'; }
};

struct StoredThread {
  char convId[kMaxConvIdLen];
  char threadTitle[kMaxConvIdLen];
  char app[kMaxAppNameLen];
  uint8_t messageCount;
  StoredMessage messages[proto::kMaxMessagesPerThread];
  uint8_t quickReplyCount;
  char quickReplies[proto::kMaxQuickReplies][64];
  uint64_t lastUpdate;
  bool unread;

  void clear() { std::memset(this, 0, sizeof(*this)); }
  bool empty() const { return convId[0] == '\0'; }
};

struct StoredCalendarEvent {
  uint32_t eventId;
  char title[kMaxNotifTitleLen];
  uint64_t start;
  uint64_t end;
  char location[kMaxLocationLen];
  bool allDay;

  void clear() { std::memset(this, 0, sizeof(*this)); }
  bool empty() const { return eventId == 0; }
};

class CompanionStore {
 public:
  bool init();
  void deinit();

  // Notifications
  size_t notificationCount() const;
  const StoredNotification* getNotification(size_t index) const;
  bool addNotification(const proto::NotifPush& notif);
  bool dismissNotification(const char* notifKey);
  void clearAllNotifications();
  int unreadNotificationCount() const;

  // Message threads
  size_t threadCount() const;
  const StoredThread* getThread(size_t index) const;
  StoredThread* findThread(const char* convId);
  bool addOrUpdateThread(const proto::MsgThreadPush& thread);
  bool markThreadRead(const char* convId);
  int unreadThreadCount() const;

  // Calendar
  size_t eventCount() const;
  const StoredCalendarEvent* getEvent(size_t index) const;
  bool syncCalendar(const proto::CalendarSync& cal);
  void clearCalendar();

  // Time sync
  uint64_t lastTimeSyncMs() const { return lastTimeSyncMs_; }
  int32_t tzOffsetMinutes() const { return tzOffsetMinutes_; }
  const char* tzName() const { return tzName_; }
  void updateTime(uint64_t utcMs, int32_t tzOffset, const char* tzName);

  // Persistence
  bool loadFromSd();
  bool saveToSd();
  void markDirty() { dirty_ = true; }
  bool isDirty() const { return dirty_; }

 private:
  void copyStr(char* dst, size_t dstCap, const proto::Str& src);
  void copyStr(char* dst, size_t dstCap, const char* src, size_t srcLen);

  StoredNotification* notifications_ = nullptr;
  StoredThread* threads_ = nullptr;
  StoredCalendarEvent* events_ = nullptr;
  size_t notifCount_ = 0;
  size_t threadCount_ = 0;
  size_t eventCount_ = 0;
  uint64_t lastTimeSyncMs_ = 0;
  int32_t tzOffsetMinutes_ = 0;
  char tzName_[kMaxTzNameLen] = {};
  bool initialized_ = false;
  bool dirty_ = false;
};

CompanionStore& store();

}  // namespace margin::companion
