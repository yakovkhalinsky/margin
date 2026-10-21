# margin Companion App v1 — Architecture & BLE Protocol Extension

Status: **v1 implementation**. This document extends [BLE_PROTOCOL.md](BLE_PROTOCOL.md) with
the notification, message, calendar, and time sync features for the Android companion app.

## 1. Overview

The margin companion system enables the X4 Pro e-ink reader to display phone notifications,
message threads, and calendar events — synchronized over BLE from an Android app. The reader
remains a "calm terminal" with no network connectivity of its own.

### v1 Feature Scope

| Feature | Direction | Notes |
|---------|-----------|-------|
| Notifications | phone → reader | App-filtered; dismiss syncs back |
| Messages | phone → reader | Threads grouped by conversation; replies are v2 |
| Calendar | phone → reader | Next 7 days of events |
| Time sync | phone → reader | UTC + timezone offset |
| Quick replies | reader → phone | Predefined reply buttons (v2: freeform reply) |

### Roles

- **X4 Pro (GATT server)**: Displays notifications, messages, calendar; stores recent data in
  PSRAM + SD cache; sends dismissals and quick-reply selections.
- **Android app (GATT central)**: Reads notifications via `NotificationListenerService`, calendar
  via `CalendarContract`, handles pairing via `CompanionDeviceManager`, runs as foreground service.

## 2. GATT Layout

The companion features reuse the existing margin service UUID and characteristics:

| Characteristic | UUID | Purpose for Companion |
|----------------|------|----------------------|
| RX | `d0ed0002-...` | App → device: notifications, messages, calendar, time sync |
| TX | `d0ed0003-...` | Device → app: dismiss, quick-reply, sync requests |
| CTRL | `d0ed0004-...` | ACK/NACK flow control (unchanged) |

No new characteristics are added — all data flows through the existing message protocol.

## 3. New Message Types

Extending the message type registry from BLE_PROTOCOL.md:

| Code | Name | Direction | Purpose |
|------|------|-----------|---------|
| 0x70 | NOTIF_PUSH | app → device | Push a notification (or batch) |
| 0x71 | NOTIF_DISMISS | device → app | Dismiss notification on phone |
| 0x72 | NOTIF_CLEAR | device → app | Clear all notifications |
| 0x73 | NOTIF_DISMISS_ACK | app → device | Confirm dismiss processed |
| 0x74 | MSG_THREAD_PUSH | app → device | Push message thread (or update) |
| 0x75 | MSG_MARK_READ | device → app | Mark conversation read |
| 0x76 | MSG_QUICK_REPLY | device → app | Send quick reply |
| 0x77 | MSG_QUICK_REPLY_ACK | app → device | Quick reply sent |
| 0x80 | CALENDAR_SYNC | app → device | Calendar events batch |
| 0x81 | CALENDAR_REQUEST | device → app | Request calendar refresh |
| 0x90 | TIME_SYNC | app → device | Current time + timezone |
| 0x91 | TIME_REQUEST | device → app | Request time sync |

## 4. TLV Tags for Companion

New tags (0x70–0x9F range):

| Tag | Name | Type | Description |
|-----|------|------|-------------|
| 0x70 | NOTIF_ID | u32 | Unique notification ID (stable across updates) |
| 0x71 | NOTIF_KEY | utf8 | Android notification key (package|tag|id) |
| 0x72 | NOTIF_APP | utf8 | App package name |
| 0x73 | NOTIF_APP_NAME | utf8 | Human-readable app name |
| 0x74 | NOTIF_TITLE | utf8 | Notification title |
| 0x75 | NOTIF_BODY | utf8 | Notification body text |
| 0x76 | NOTIF_TIMESTAMP | u64 | Notification post time (ms since epoch) |
| 0x77 | NOTIF_ICON | u8 | Icon category (0=default, 1=message, 2=email, 3=call, 4=calendar) |
| 0x78 | MSG_CONV_ID | utf8 | Conversation ID (package:thread_id) |
| 0x79 | MSG_SENDER | utf8 | Sender name |
| 0x7A | MSG_SENDER_KEY | utf8 | Sender identifier (for avatar/grouping) |
| 0x7B | MSG_TEXT | utf8 | Message text |
| 0x7C | MSG_TIMESTAMP | u64 | Message timestamp (ms since epoch) |
| 0x7D | MSG_IS_OUTGOING | u8 | 1 if sent by user, 0 if received |
| 0x7E | MSG_RECORD | nested TLV | Single message in thread, repeatable |
| 0x7F | MSG_THREAD_TITLE | utf8 | Thread display title (group name or sender) |
| 0x80 | CAL_EVENT_ID | u32 | Calendar event ID |
| 0x81 | CAL_TITLE | utf8 | Event title |
| 0x82 | CAL_START | u64 | Start time (ms since epoch) |
| 0x83 | CAL_END | u64 | End time (ms since epoch) |
| 0x84 | CAL_LOCATION | utf8 | Event location |
| 0x85 | CAL_ALL_DAY | u8 | 1 if all-day event |
| 0x86 | CAL_RECORD | nested TLV | Single calendar event, repeatable |
| 0x87 | QUICK_REPLY_OPT | utf8 | Quick reply option, repeatable (max 6) |
| 0x88 | QUICK_REPLY_IDX | u8 | Selected quick reply index |
| 0x90 | TIME_UTC_MS | u64 | Current UTC time in milliseconds |
| 0x91 | TIME_TZ_OFFSET | i32 | Timezone offset from UTC in minutes |
| 0x92 | TIME_TZ_NAME | utf8 | Timezone identifier (e.g., "America/New_York") |

## 5. Message Body Specifications

### 5.1 NOTIF_PUSH (0x70)

Pushes one or more notifications from phone to reader. Notifications with the same
`NOTIF_KEY` replace previous versions (updates).

```
NOTIF_PUSH := NOTIF_KEY R, NOTIF_ID R, NOTIF_APP R, NOTIF_APP_NAME O,
              NOTIF_TITLE R, NOTIF_BODY O, NOTIF_TIMESTAMP R, NOTIF_ICON O,
              QUICK_REPLY_OPT ×0–6
```

For batching, the app may send multiple NOTIF_PUSH messages or bundle multiple notifications
with repeating TLV groups — simpler to send one message per notification.

### 5.2 NOTIF_DISMISS (0x71)

Device requests dismissal of a notification (user swiped away or tapped clear).

```
NOTIF_DISMISS := NOTIF_KEY R
```

### 5.3 NOTIF_CLEAR (0x72)

Device requests clearing all notifications.

```
NOTIF_CLEAR := (empty body)
```

### 5.4 MSG_THREAD_PUSH (0x74)

Pushes a message thread (conversation). For efficiency, only includes recent messages
(last 50 or 24 hours, whichever is smaller). Updates replace the previous thread.

```
MSG_THREAD_PUSH := MSG_CONV_ID R, MSG_THREAD_TITLE R, NOTIF_APP R,
                   MSG_RECORD ×n (each: MSG_SENDER R, MSG_TEXT R, MSG_TIMESTAMP R,
                                  MSG_IS_OUTGOING O, MSG_SENDER_KEY O),
                   QUICK_REPLY_OPT ×0–6
```

### 5.5 MSG_QUICK_REPLY (0x76)

Device sends a quick reply selection.

```
MSG_QUICK_REPLY := MSG_CONV_ID R, QUICK_REPLY_IDX R
```

### 5.6 CALENDAR_SYNC (0x80)

Syncs calendar events for the next 7 days. Sent on connect, daily, and on user request.

```
CALENDAR_SYNC := CAL_RECORD ×n (each: CAL_EVENT_ID R, CAL_TITLE R, CAL_START R,
                                CAL_END R, CAL_LOCATION O, CAL_ALL_DAY O)
```

### 5.7 TIME_SYNC (0x90)

Syncs current time from phone to reader. Sent on connect and hourly.

```
TIME_SYNC := TIME_UTC_MS R, TIME_TZ_OFFSET R, TIME_TZ_NAME O
```

## 6. Chunking and MTU

The protocol already handles MTU negotiation and fragmentation (BLE_PROTOCOL.md §3).
Companion messages may be large (message threads with history), so:

- **Default body limit**: 65,536 bytes (kMaxBodySize)
- **Practical limits**: Notifications truncated to ~1KB body text; message threads to 50 messages
- **Calendar**: Typically fits in one message; ~200 bytes per event × 50 events = 10KB

## 7. Pairing and Bonding

Uses the existing secure pairing from BLE_PROTOCOL.md §9:

1. User opens Settings → Pair phone on reader (60s window)
2. Android app scans for `margin-XXXX` devices or uses CompanionDeviceManager
3. LE Secure Connections with Numeric Comparison (6-digit code on both screens)
4. Bond stored; subsequent connections auto-reconnect

**Android specifics**:
- `CompanionDeviceManager.associate()` enables background reconnection without broad permissions
- Foreground service with `FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE` for API 34+

## 8. Connection Parameters & Power Budget

E-ink readers are battery-sensitive. Connection parameters balance responsiveness with power:

### Recommended Parameters

| State | Connection Interval | Slave Latency | Supervision Timeout |
|-------|--------------------:|:-------------:|--------------------:|
| Active (screen on) | 15–30 ms | 0 | 4 s |
| Idle (screen off) | 100–200 ms | 4–10 | 6 s |
| Background | 500–1000 ms | 10–30 | 10 s |

### Power Considerations

- **X4 Pro has 8MB PSRAM**: Buffer incoming data without frequent flash writes
- **Notification batching**: App buffers notifications for 2s before pushing (reduces radio wakeups)
- **Lazy sync**: Calendar syncs once on connect + once per day, not on every phone change
- **Reader requests**: TIME_REQUEST only on boot or after long sleep; CALENDAR_REQUEST on user action

### Connection Interval Negotiation

The Android app requests connection parameters via `BluetoothGatt.requestConnectionPriority()`:
- `CONNECTION_PRIORITY_HIGH` (15ms) during active data transfer
- `CONNECTION_PRIORITY_BALANCED` (100ms) during idle
- `CONNECTION_PRIORITY_LOW_POWER` (500ms) when in background

## 9. Reconnection

On disconnect:
1. Reader shows "Phone disconnected" briefly, continues showing cached data
2. Android service attempts reconnect with exponential backoff (1s, 2s, 4s, 8s, max 30s)
3. On reconnect, app sends TIME_SYNC, then CALENDAR_SYNC, then pending notifications
4. Reader sends any pending dismissals

Data persists across disconnects via SD card cache.

## 10. Storage Architecture

### 10.1 PSRAM (Runtime)

```cpp
struct CompanionStore {
    Notification notifications[32];  // ~50KB (1.5KB each)
    MessageThread threads[16];       // ~200KB (12KB each with 50 msgs)
    CalendarEvent events[100];       // ~20KB (200B each)
    uint64_t lastTimeSync;
    int32_t tzOffsetMinutes;
};
```

Total PSRAM budget: ~300KB of the 8MB available.

### 10.2 SD Card Cache

Path: `/margin/companion/`

```
/margin/companion/
  notifications.json     # Current notifications (regenerated on change)
  threads/
    <conv_id_hash>.json  # Per-thread message cache
  calendar.json          # Calendar events
  settings.json          # Last sync time, device state
```

Files are JSON for debuggability. Written:
- On disconnect (full state)
- Every 5 minutes if changed (incremental)
- On shutdown/sleep

## 11. Firmware Activities

### 11.1 NotificationsActivity

- List view of notifications, newest first
- Each item: icon, app name, title, body preview, timestamp
- Tap: expand full notification
- Swipe left: dismiss
- Menu: clear all
- Unread count badge on home screen

### 11.2 MessagesActivity

Two screens:
1. **Thread list**: Conversations grouped by app, newest message first
2. **Thread view**: Full message history with sender names, timestamps

- Shows quick-reply buttons at bottom of thread view
- Unread indicator per thread
- Tap quick-reply → MSG_QUICK_REPLY sent

### 11.3 CalendarActivity

- Agenda view: today + next 7 days
- Day headers with date
- Events: title, time (or "All day"), location
- Tap event: show full details (read-only)
- Pull-to-refresh requests CALENDAR_REQUEST

## 12. Android App Components

### 12.1 CompanionService (Foreground Service)

- Persistent foreground notification ("Connected to margin reader")
- Manages BLE connection lifecycle
- Coordinates NotificationListenerService and CalendarProvider
- Handles reconnection logic

### 12.2 MarginNotificationListener (NotificationListenerService)

- Filters notifications by app allowlist (stored in SharedPreferences)
- Parses `MessagingStyle` for message threads
- Deduplicates rapid notification updates (100ms debounce)
- Handles dismiss requests from reader via bound service connection

### 12.3 CalendarSyncWorker

- Queries `CalendarContract.Events` for next 7 days
- Respects calendar visibility settings
- Runs on connect and daily via WorkManager

### 12.4 Settings UI

- **App allowlist**: Checkbox list of installed apps (messaging apps pre-checked)
- **Sync toggles**: Notifications on/off, Messages on/off, Calendar on/off
- **Connection status**: Shows paired device, connection state
- **Unpair**: Removes bond and association

## 13. Permissions (Android)

```xml
<!-- BLE (API 31+) -->
<uses-permission android:name="android.permission.BLUETOOTH_SCAN"
    android:usesPermissionFlags="neverForLocation" />
<uses-permission android:name="android.permission.BLUETOOTH_CONNECT" />

<!-- Notifications (API 33+) -->
<uses-permission android:name="android.permission.POST_NOTIFICATIONS" />

<!-- NotificationListenerService -->
<uses-permission android:name="android.permission.BIND_NOTIFICATION_LISTENER_SERVICE" />

<!-- Calendar -->
<uses-permission android:name="android.permission.READ_CALENDAR" />

<!-- Foreground service -->
<uses-permission android:name="android.permission.FOREGROUND_SERVICE" />
<uses-permission android:name="android.permission.FOREGROUND_SERVICE_CONNECTED_DEVICE" />
```

Permission request flow:
1. On first launch: explain and request BLUETOOTH_CONNECT/SCAN
2. On pairing: system CompanionDeviceManager dialog
3. On settings toggle: request READ_CALENDAR, POST_NOTIFICATIONS as needed
4. NotificationListenerService: direct user to Settings > Apps > Special access

## 14. Error Handling

| Scenario | Device Behavior | App Behavior |
|----------|-----------------|--------------|
| BLE not paired | Show "Pair phone in Settings" | Start pairing flow |
| SD missing | Use PSRAM only, warn user | N/A |
| Notification parse error | Skip notification | Log, continue |
| Calendar permission denied | Calendar screen shows prompt | Show settings shortcut |
| Disconnect during send | Queue for retry on reconnect | Retry on reconnect |

## 15. Test Vectors

Shared between firmware and Android unit tests.

### NOTIF_PUSH example

```
notif_key = "com.whatsapp|null|12345"
notif_id = 12345
app = "com.whatsapp"
app_name = "WhatsApp"
title = "John"
body = "Hey, are you coming to dinner?"
timestamp = 1698076800000 (2023-10-23 12:00:00 UTC)
icon = 1 (message)
quick_replies = ["Yes", "No", "Maybe later"]
```

TLV body (hex):
```
71 1900 636f6d2e77686174736170707c6e756c6c7c3132333435
70 0400 39300000
72 0d00 636f6d2e7768617473617070
73 0800 5768617473417070
74 0400 4a6f686e
75 1e00 4865792c2061726520796f7520636f6d696e6720746f2064696e6e65723f
76 0800 00e87648c7010000
77 0100 01
87 0300 596573
87 0200 4e6f
87 0b00 4d617962652066726f6d206c61746572
```

### TIME_SYNC example

```
utc_ms = 1698076800000
tz_offset = -240 (EDT, UTC-4)
tz_name = "America/New_York"
```

TLV body (hex):
```
90 0800 00e87648c7010000
91 0400 10ffffff
92 1000 416d65726963612f4e65775f596f726b
```

## 16. Build Flag

Companion features are guarded by `MARGIN_BLE` build flag:

```ini
# platformio.margin.ini
[env:margin_x4pro]
build_flags =
    ${env:x4pro.build_flags}
    -DMARGIN_BLE=1
```

When disabled, the BLE service is not initialized and companion Activities are hidden from
the home menu. This allows building without NimBLE for testing or when RAM is constrained.

## 17. Memory Impact

With `MARGIN_BLE=1`:

| Component | Flash | RAM | PSRAM |
|-----------|------:|----:|------:|
| NimBLE-Arduino | ~180 KB | ~40 KB | — |
| MarginBle service | ~15 KB | ~2 KB | ~130 KB (RX/TX buffers) |
| CompanionStore | ~5 KB | ~1 KB | ~300 KB |
| Activities (3) | ~25 KB | ~3 KB | — |
| **Total** | ~225 KB | ~46 KB | ~430 KB |

The X4 Pro has 16MB flash and 8MB PSRAM; these amounts are well within budget. Standard
RAM usage (~46KB) is the main constraint — upstream CrossPoint disables BLE to save RAM,
but the X4 Pro's PSRAM allows offloading large buffers.

## 18. Future (v2)

- **Freeform reply**: On-device keyboard input sent as typed text
- **Contact photos**: Small grayscale avatars via FILE characteristic
- **Do Not Disturb sync**: Mirror phone DND state
- **Notification actions**: Forward action intents to phone
- **Music controls**: Play/pause/skip via notification media session
- **Find my phone**: Trigger phone ring from reader
