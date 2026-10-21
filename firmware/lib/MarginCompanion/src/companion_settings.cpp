// SPDX-License-Identifier: MIT
#include "margin/companion_settings.h"

#if defined(ARDUINO)

#include <Logging.h>
#include <Preferences.h>

namespace margin::companion {

static constexpr const char* TAG = "COMP_SET";
static constexpr const char* PREFS_NS = "margin_comp";

SettingsManager& settings() {
  static SettingsManager sm;
  return sm;
}

bool SettingsManager::load() {
  Preferences prefs;
  if (!prefs.begin(PREFS_NS, true)) {
    LOG_INF(TAG, "Failed to open preferences for reading");
    return false;
  }

  settings_.bleEnabled = prefs.getBool("ble_en", false);  // OFF by default
  settings_.syncNotifications = prefs.getBool("sync_notif", true);
  settings_.syncMessages = prefs.getBool("sync_msg", true);
  settings_.syncCalendar = prefs.getBool("sync_cal", true);

  prefs.end();
  loaded_ = true;
  LOG_INF(TAG, "Settings loaded: BLE=%d notif=%d msg=%d cal=%d",
          settings_.bleEnabled, settings_.syncNotifications,
          settings_.syncMessages, settings_.syncCalendar);
  return true;
}

bool SettingsManager::save() {
  Preferences prefs;
  if (!prefs.begin(PREFS_NS, false)) {
    LOG_ERR(TAG, "Failed to open preferences for writing");
    return false;
  }

  prefs.putBool("ble_en", settings_.bleEnabled);
  prefs.putBool("sync_notif", settings_.syncNotifications);
  prefs.putBool("sync_msg", settings_.syncMessages);
  prefs.putBool("sync_cal", settings_.syncCalendar);

  prefs.end();
  LOG_INF(TAG, "Settings saved");
  return true;
}

void SettingsManager::setBleEnabled(bool enabled) {
  if (settings_.bleEnabled != enabled) {
    settings_.bleEnabled = enabled;
    save();
  }
}

void SettingsManager::setSyncNotifications(bool enabled) {
  if (settings_.syncNotifications != enabled) {
    settings_.syncNotifications = enabled;
    save();
  }
}

void SettingsManager::setSyncMessages(bool enabled) {
  if (settings_.syncMessages != enabled) {
    settings_.syncMessages = enabled;
    save();
  }
}

void SettingsManager::setSyncCalendar(bool enabled) {
  if (settings_.syncCalendar != enabled) {
    settings_.syncCalendar = enabled;
    save();
  }
}

}  // namespace margin::companion

#else  // Host stub

namespace margin::companion {

static SettingsManager sStubManager;
SettingsManager& settings() { return sStubManager; }

bool SettingsManager::load() { return false; }
bool SettingsManager::save() { return false; }
void SettingsManager::setBleEnabled(bool) {}
void SettingsManager::setSyncNotifications(bool) {}
void SettingsManager::setSyncMessages(bool) {}
void SettingsManager::setSyncCalendar(bool) {}

}  // namespace margin::companion

#endif
