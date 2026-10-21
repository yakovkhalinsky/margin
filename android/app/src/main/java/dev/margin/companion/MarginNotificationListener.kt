// SPDX-License-Identifier: MIT
// NotificationListenerService that captures notifications and forwards them to the reader.
// Requires the user to grant notification access in Android Settings.
package dev.margin.companion

import android.app.Notification
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Log
import dev.margin.protocol.CompanionMessages
import dev.margin.protocol.MessageRecord
import dev.margin.protocol.MsgThreadPushData
import dev.margin.protocol.NotifIcon
import dev.margin.protocol.NotifPushData

class MarginNotificationListener : NotificationListenerService() {
    private var companionService: CompanionService? = null
    private var bound = false
    private val handler = Handler(Looper.getMainLooper())
    private val pendingNotifs = mutableMapOf<String, StatusBarNotification>()
    private val debounceMs = 100L

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName, binder: IBinder) {
            companionService = (binder as CompanionService.LocalBinder).getService()
            bound = true
            Log.d(TAG, "Bound to CompanionService")
        }
        override fun onServiceDisconnected(name: ComponentName) {
            companionService = null
            bound = false
        }
    }

    override fun onCreate() {
        super.onCreate()
        Intent(this, CompanionService::class.java).also { intent ->
            bindService(intent, connection, Context.BIND_AUTO_CREATE)
        }
    }

    override fun onDestroy() {
        if (bound) {
            unbindService(connection)
            bound = false
        }
        super.onDestroy()
    }

    override fun onNotificationPosted(sbn: StatusBarNotification) {
        if (!isAllowedApp(sbn.packageName)) return

        val key = sbn.key
        pendingNotifs[key] = sbn

        handler.removeCallbacksAndMessages(key)
        handler.postDelayed({
            pendingNotifs.remove(key)?.let { processNotification(it) }
        }, debounceMs)
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification) {
        pendingNotifs.remove(sbn.key)
    }

    private fun processNotification(sbn: StatusBarNotification) {
        val service = companionService ?: return
        val notification = sbn.notification ?: return
        val extras = notification.extras ?: return

        val isMessagingStyle = extras.getParcelableArray(Notification.EXTRA_MESSAGES) != null ||
                extras.containsKey("android.messagingStyleUser")

        if (isMessagingStyle && isMessagingApp(sbn.packageName)) {
            processMessagingNotification(sbn, service)
        } else {
            processRegularNotification(sbn, service)
        }
    }

    private fun processRegularNotification(sbn: StatusBarNotification, service: CompanionService) {
        val notification = sbn.notification
        val extras = notification.extras

        val title = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString() ?: ""
        val body = extras.getCharSequence(Notification.EXTRA_TEXT)?.toString() ?: ""
        val bigText = extras.getCharSequence(Notification.EXTRA_BIG_TEXT)?.toString()

        val appInfo = packageManager.getApplicationInfo(sbn.packageName, 0)
        val appName = packageManager.getApplicationLabel(appInfo).toString()

        val quickReplies = extractQuickReplies(notification)
        val icon = categorizeIcon(sbn.packageName, notification)

        val data = NotifPushData(
            notifKey = sbn.key,
            notifId = sbn.id.toLong(),
            app = sbn.packageName,
            appName = appName,
            title = title,
            body = bigText ?: body,
            timestamp = sbn.postTime,
            icon = icon,
            quickReplies = quickReplies
        )

        service.sendNotification(data)
    }

    private fun processMessagingNotification(sbn: StatusBarNotification, service: CompanionService) {
        val notification = sbn.notification
        val extras = notification.extras

        val messages = mutableListOf<MessageRecord>()
        val conversationTitle = extras.getCharSequence(Notification.EXTRA_CONVERSATION_TITLE)?.toString()
            ?: extras.getCharSequence(Notification.EXTRA_TITLE)?.toString() ?: "Unknown"

        @Suppress("DEPRECATION")
        val msgArray = extras.getParcelableArray(Notification.EXTRA_MESSAGES)
        if (msgArray != null) {
            for (msg in msgArray) {
                if (msg is android.os.Bundle) {
                    val text = msg.getCharSequence("text")?.toString() ?: continue
                    val sender = msg.getCharSequence("sender")?.toString() ?: ""
                    val time = msg.getLong("time", System.currentTimeMillis())
                    val isOutgoing = sender.isBlank() || sender == "You"

                    messages.add(MessageRecord(
                        sender = if (isOutgoing) "You" else sender,
                        text = text,
                        timestamp = time,
                        isOutgoing = isOutgoing
                    ))
                }
            }
        }

        if (messages.isEmpty()) {
            processRegularNotification(sbn, service)
            return
        }

        val quickReplies = extractQuickReplies(notification)
        val appInfo = packageManager.getApplicationInfo(sbn.packageName, 0)
        val appName = packageManager.getApplicationLabel(appInfo).toString()

        val threadData = MsgThreadPushData(
            convId = "${sbn.packageName}:${sbn.tag ?: sbn.id}",
            threadTitle = conversationTitle,
            app = sbn.packageName,
            messages = messages.takeLast(50),
            quickReplies = quickReplies
        )

        service.sendMessageThread(threadData)
    }

    private fun extractQuickReplies(notification: Notification): List<String> {
        val replies = mutableListOf<String>()
        notification.actions?.forEach { action ->
            action.remoteInputs?.forEach { input ->
                if (input.allowFreeFormInput) {
                    action.title?.toString()?.let { replies.add(it) }
                }
            }
            if (action.remoteInputs == null && action.title != null) {
                replies.add(action.title.toString())
            }
        }
        return replies.take(6)
    }

    private fun categorizeIcon(packageName: String, notification: Notification): NotifIcon {
        return when {
            isMessagingApp(packageName) -> NotifIcon.MESSAGE
            isEmailApp(packageName) -> NotifIcon.EMAIL
            isPhoneApp(packageName) -> NotifIcon.CALL
            isCalendarApp(packageName) -> NotifIcon.CALENDAR
            else -> NotifIcon.DEFAULT
        }
    }

    private fun isAllowedApp(packageName: String): Boolean {
        val prefs = getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val allowlist = prefs.getStringSet(KEY_ALLOWLIST, defaultAllowlist) ?: defaultAllowlist
        return packageName in allowlist
    }

    private fun isMessagingApp(packageName: String): Boolean = packageName in messagingApps
    private fun isEmailApp(packageName: String): Boolean = packageName in emailApps
    private fun isPhoneApp(packageName: String): Boolean = packageName in phoneApps
    private fun isCalendarApp(packageName: String): Boolean = packageName in calendarApps

    fun dismissNotification(notifKey: String) {
        try {
            cancelNotification(notifKey)
        } catch (e: Exception) {
            Log.w(TAG, "Failed to dismiss notification: $notifKey", e)
        }
    }

    fun clearAllNotifications() {
        try {
            cancelAllNotifications()
        } catch (e: Exception) {
            Log.w(TAG, "Failed to clear all notifications", e)
        }
    }

    companion object {
        private const val TAG = "MarginNotifListener"
        const val PREFS_NAME = "margin_companion"
        const val KEY_ALLOWLIST = "notification_allowlist"

        private val messagingApps = setOf(
            "com.whatsapp", "com.whatsapp.w4b",
            "org.telegram.messenger", "org.telegram.messenger.web",
            "org.thoughtcrime.securesms",
            "com.facebook.orca", "com.facebook.mlite",
            "com.discord",
            "com.slack",
            "com.google.android.apps.messaging",
            "com.samsung.android.messaging"
        )

        private val emailApps = setOf(
            "com.google.android.gm",
            "com.microsoft.office.outlook",
            "com.samsung.android.email.provider"
        )

        private val phoneApps = setOf(
            "com.google.android.dialer",
            "com.samsung.android.incallui"
        )

        private val calendarApps = setOf(
            "com.google.android.calendar",
            "com.samsung.android.calendar"
        )

        val defaultAllowlist = messagingApps + emailApps + calendarApps
    }
}
