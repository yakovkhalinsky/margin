// SPDX-License-Identifier: MIT
// Companion settings manager. Persists settings via Preferences on NVS.
// BLE is OFF by default to avoid unexpected battery drain or crashes.
#pragma once

#include <cstdint>

namespace margin::companion {

struct Settings {
  bool bleEnabled = false;  // BLE off by default; user must enable in settings
  bool syncNotifications = true;
  bool syncMessages = true;
  bool syncCalendar = true;
};

class SettingsManager {
 public:
  bool load();
  bool save();

  bool bleEnabled() const { return settings_.bleEnabled; }
  void setBleEnabled(bool enabled);

  bool syncNotifications() const { return settings_.syncNotifications; }
  void setSyncNotifications(bool enabled);

  bool syncMessages() const { return settings_.syncMessages; }
  void setSyncMessages(bool enabled);

  bool syncCalendar() const { return settings_.syncCalendar; }
  void setSyncCalendar(bool enabled);

  const Settings& settings() const { return settings_; }

 private:
  Settings settings_;
  bool loaded_ = false;
};

SettingsManager& settings();

}  // namespace margin::companion
