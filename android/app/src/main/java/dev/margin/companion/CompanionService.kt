// SPDX-License-Identifier: MIT
// Foreground service that maintains the BLE connection to the margin reader.
// Coordinates notification and calendar sync.
package dev.margin.companion

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothManager
import android.companion.AssociationInfo
import android.companion.CompanionDeviceManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.os.Binder
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
import dev.margin.MainActivity
import dev.margin.ble.MarginBleClient
import dev.margin.protocol.CompanionMessages
import dev.margin.protocol.MsgThreadPushData
import dev.margin.protocol.MsgType
import dev.margin.protocol.NotifPushData
import dev.margin.protocol.TimeSyncData
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import java.util.TimeZone

class CompanionService : Service() {
    private val binder = LocalBinder()
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private val handler = Handler(Looper.getMainLooper())

    private var bleClient: MarginBleClient? = null
    private var calendarReader: CalendarReader? = null
    private var reconnectJob: Job? = null
    private var timeSyncJob: Job? = null

    private var lastCalendarSync: Long = 0
    private val calendarSyncInterval = 24 * 60 * 60 * 1000L

    inner class LocalBinder : Binder() {
        fun getService(): CompanionService = this@CompanionService
    }

    override fun onBind(intent: Intent?): IBinder = binder

    override fun onCreate() {
        super.onCreate()
        createNotificationChannel()
        calendarReader = CalendarReader(this)
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START -> startForegroundService()
            ACTION_STOP -> stopSelf()
            ACTION_CONNECT -> intent.getParcelableExtra<BluetoothDevice>(EXTRA_DEVICE)?.let { connect(it) }
            ACTION_DISCONNECT -> disconnect()
        }
        return START_STICKY
    }

    override fun onDestroy() {
        scope.cancel()
        disconnect()
        super.onDestroy()
    }

    private fun startForegroundService() {
        val notification = buildNotification("Ready to connect")

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
            startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
        } else {
            startForeground(NOTIFICATION_ID, notification)
        }

        tryAutoConnect()
    }

    private fun tryAutoConnect() {
        if (!hasBluetoothPermission()) return

        val device = getAssociatedDevice() ?: return
        connect(device)
    }

    private fun getAssociatedDevice(): BluetoothDevice? {
        val cdm = getSystemService(Context.COMPANION_DEVICE_SERVICE) as? CompanionDeviceManager ?: return null
        val btAdapter = (getSystemService(Context.BLUETOOTH_SERVICE) as? BluetoothManager)?.adapter ?: return null

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val associations = cdm.myAssociations
            for (assoc in associations) {
                val macAddress = assoc.deviceMacAddress?.toString()
                if (macAddress != null) {
                    return btAdapter.getRemoteDevice(macAddress)
                }
            }
        } else {
            @Suppress("DEPRECATION")
            val associations = cdm.associations
            for (mac in associations) {
                return btAdapter.getRemoteDevice(mac)
            }
        }
        return null
    }

    fun connect(device: BluetoothDevice) {
        if (!hasBluetoothPermission()) {
            Log.w(TAG, "Missing Bluetooth permission")
            return
        }

        disconnect()

        bleClient = MarginBleClient.getInstance(this).also { client ->
            client.connect(device)

            scope.launch {
                client.state.collectLatest { state ->
                    Log.d(TAG, "BLE state: $state")
                    updateNotification(state)

                    when (state) {
                        MarginBleClient.State.READY -> onConnected()
                        MarginBleClient.State.DISCONNECTED -> scheduleReconnect(device)
                        else -> {}
                    }
                }
            }

            scope.launch {
                client.messages.collect { msg ->
                    handleMessage(msg)
                }
            }
        }
    }

    fun disconnect() {
        reconnectJob?.cancel()
        timeSyncJob?.cancel()
        bleClient?.disconnect()
        bleClient = null
    }

    private fun onConnected() {
        reconnectJob?.cancel()

        sendTimeSync()
        startTimeSyncTimer()

        if (System.currentTimeMillis() - lastCalendarSync > calendarSyncInterval) {
            sendCalendarSync()
        }
    }

    private fun scheduleReconnect(device: BluetoothDevice) {
        reconnectJob?.cancel()
        reconnectJob = scope.launch {
            var delayMs = 1000L
            val maxDelay = 30000L

            while (true) {
                delay(delayMs)
                if (bleClient?.state?.value != MarginBleClient.State.DISCONNECTED) break

                Log.d(TAG, "Attempting reconnect...")
                connect(device)

                delayMs = (delayMs * 2).coerceAtMost(maxDelay)
            }
        }
    }

    private fun handleMessage(msg: dev.margin.protocol.Message) {
        when (msg.type) {
            MsgType.NOTIF_DISMISS -> {
                val dismiss = CompanionMessages.decodeNotifDismiss(msg.body)
                dismiss?.let { notifyDismiss(it.notifKey) }
            }
            MsgType.NOTIF_CLEAR -> {
                notifyClearAll()
            }
            MsgType.MSG_MARK_READ -> {
                val markRead = CompanionMessages.decodeMsgMarkRead(msg.body)
                markRead?.let { Log.d(TAG, "Thread marked read: ${it.convId}") }
            }
            MsgType.MSG_QUICK_REPLY -> {
                val reply = CompanionMessages.decodeMsgQuickReply(msg.body)
                reply?.let { handleQuickReply(it.convId, it.optionIndex) }
            }
            MsgType.CALENDAR_REQUEST -> {
                sendCalendarSync()
            }
            MsgType.TIME_REQUEST -> {
                sendTimeSync()
            }
            else -> Log.d(TAG, "Unhandled message type: ${msg.type}")
        }
    }

    private fun notifyDismiss(notifKey: String) {
        Log.d(TAG, "Dismiss notification: $notifKey")
    }

    private fun notifyClearAll() {
        Log.d(TAG, "Clear all notifications")
    }

    private fun handleQuickReply(convId: String, optionIndex: Int) {
        Log.d(TAG, "Quick reply $optionIndex for $convId")
        bleClient?.send(MsgType.MSG_QUICK_REPLY_ACK, CompanionMessages.encodeMsgQuickReplyAck())
    }

    fun sendNotification(data: NotifPushData) {
        val client = bleClient ?: return
        if (client.state.value != MarginBleClient.State.READY) return

        val body = CompanionMessages.encodeNotifPush(data)
        client.send(MsgType.NOTIF_PUSH, body)
    }

    fun sendMessageThread(data: MsgThreadPushData) {
        val client = bleClient ?: return
        if (client.state.value != MarginBleClient.State.READY) return

        val body = CompanionMessages.encodeMsgThreadPush(data)
        client.send(MsgType.MSG_THREAD_PUSH, body)
    }

    fun sendCalendarSync() {
        val client = bleClient ?: return
        if (client.state.value != MarginBleClient.State.READY) return

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.READ_CALENDAR)
            != PackageManager.PERMISSION_GRANTED) {
            Log.w(TAG, "Calendar permission not granted")
            return
        }

        val calData = calendarReader?.readUpcomingEvents(7) ?: return
        val body = CompanionMessages.encodeCalendarSync(calData)
        client.send(MsgType.CALENDAR_SYNC, body)
        lastCalendarSync = System.currentTimeMillis()
    }

    fun sendTimeSync() {
        val client = bleClient ?: return
        if (client.state.value != MarginBleClient.State.READY) return

        val tz = TimeZone.getDefault()
        val data = TimeSyncData(
            utcMs = System.currentTimeMillis(),
            tzOffsetMinutes = tz.rawOffset / 60000 + tz.dstSavings / 60000,
            tzName = tz.id
        )
        val body = CompanionMessages.encodeTimeSync(data)
        client.send(MsgType.TIME_SYNC, body)
    }

    private fun startTimeSyncTimer() {
        timeSyncJob?.cancel()
        timeSyncJob = scope.launch {
            while (true) {
                delay(60 * 60 * 1000L)
                sendTimeSync()
            }
        }
    }

    private fun hasBluetoothPermission(): Boolean {
        return if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT) ==
                    PackageManager.PERMISSION_GRANTED
        } else {
            true
        }
    }

    private fun createNotificationChannel() {
        val channel = NotificationChannel(
            CHANNEL_ID,
            "margin Companion",
            NotificationManager.IMPORTANCE_LOW
        ).apply {
            description = "Connection status for margin reader"
            setShowBadge(false)
        }
        val nm = getSystemService(NotificationManager::class.java)
        nm.createNotificationChannel(channel)
    }

    private fun buildNotification(status: String): Notification {
        val intent = Intent(this, MainActivity::class.java)
        val pendingIntent = PendingIntent.getActivity(
            this, 0, intent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("margin Companion")
            .setContentText(status)
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .build()
    }

    private fun updateNotification(state: MarginBleClient.State) {
        val status = when (state) {
            MarginBleClient.State.DISCONNECTED -> "Disconnected"
            MarginBleClient.State.CONNECTING -> "Connecting..."
            MarginBleClient.State.DISCOVERING -> "Discovering services..."
            MarginBleClient.State.READY -> "Connected to margin reader"
        }
        val notification = buildNotification(status)
        val nm = getSystemService(NotificationManager::class.java)
        nm.notify(NOTIFICATION_ID, notification)
    }

    companion object {
        private const val TAG = "CompanionService"
        private const val CHANNEL_ID = "margin_companion"
        private const val NOTIFICATION_ID = 1001

        const val ACTION_START = "dev.margin.action.START_COMPANION"
        const val ACTION_STOP = "dev.margin.action.STOP_COMPANION"
        const val ACTION_CONNECT = "dev.margin.action.CONNECT"
        const val ACTION_DISCONNECT = "dev.margin.action.DISCONNECT"
        const val EXTRA_DEVICE = "device"

        fun start(context: Context) {
            val intent = Intent(context, CompanionService::class.java).apply {
                action = ACTION_START
            }
            ContextCompat.startForegroundService(context, intent)
        }

        fun stop(context: Context) {
            val intent = Intent(context, CompanionService::class.java).apply {
                action = ACTION_STOP
            }
            context.startService(intent)
        }
    }
}
