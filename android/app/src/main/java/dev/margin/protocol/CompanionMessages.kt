// SPDX-License-Identifier: MIT
// Companion app message types and encoders. See docs/companion-v1.md.
package dev.margin.protocol

data class NotifPushData(
    val notifKey: String,
    val notifId: Long,
    val app: String,
    val appName: String = "",
    val title: String,
    val body: String = "",
    val timestamp: Long,
    val icon: NotifIcon = NotifIcon.DEFAULT,
    val quickReplies: List<String> = emptyList()
)

data class MessageRecord(
    val sender: String,
    val senderKey: String = "",
    val text: String,
    val timestamp: Long,
    val isOutgoing: Boolean = false
)

data class MsgThreadPushData(
    val convId: String,
    val threadTitle: String,
    val app: String,
    val messages: List<MessageRecord>,
    val quickReplies: List<String> = emptyList()
)

data class CalendarEventData(
    val eventId: Long,
    val title: String,
    val start: Long,
    val end: Long,
    val location: String = "",
    val allDay: Boolean = false
)

data class CalendarSyncData(
    val events: List<CalendarEventData>
)

data class TimeSyncData(
    val utcMs: Long,
    val tzOffsetMinutes: Int,
    val tzName: String = ""
)

object CompanionMessages {
    fun encodeNotifPush(data: NotifPushData): ByteArray {
        val w = TlvWriter()
        w.str(Tag.NOTIF_KEY, data.notifKey)
        w.u32(Tag.NOTIF_ID, data.notifId)
        w.str(Tag.NOTIF_APP, data.app)
        if (data.appName.isNotEmpty()) w.str(Tag.NOTIF_APP_NAME, data.appName)
        w.str(Tag.NOTIF_TITLE, data.title)
        if (data.body.isNotEmpty()) w.str(Tag.NOTIF_BODY, data.body)
        w.u64(Tag.NOTIF_TIMESTAMP, data.timestamp)
        w.u8(Tag.NOTIF_ICON, data.icon.code)
        data.quickReplies.take(Protocol.MAX_QUICK_REPLIES).forEach { w.str(Tag.QUICK_REPLY_OPT, it) }
        return w.toByteArray()
    }

    fun encodeMsgThreadPush(data: MsgThreadPushData): ByteArray {
        val w = TlvWriter()
        w.str(Tag.MSG_CONV_ID, data.convId)
        w.str(Tag.MSG_THREAD_TITLE, data.threadTitle)
        w.str(Tag.NOTIF_APP, data.app)
        for (msg in data.messages.take(50)) {
            val rec = TlvWriter()
            rec.str(Tag.MSG_SENDER, msg.sender)
            if (msg.senderKey.isNotEmpty()) rec.str(Tag.MSG_SENDER_KEY, msg.senderKey)
            rec.str(Tag.MSG_TEXT, msg.text)
            rec.u64(Tag.MSG_TIMESTAMP, msg.timestamp)
            if (msg.isOutgoing) rec.u8(Tag.MSG_IS_OUTGOING, 1)
            w.bytes(Tag.MSG_RECORD, rec.toByteArray())
        }
        data.quickReplies.take(Protocol.MAX_QUICK_REPLIES).forEach { w.str(Tag.QUICK_REPLY_OPT, it) }
        return w.toByteArray()
    }

    fun encodeCalendarSync(data: CalendarSyncData): ByteArray {
        val w = TlvWriter()
        for (ev in data.events.take(100)) {
            val rec = TlvWriter()
            rec.u32(Tag.CAL_EVENT_ID, ev.eventId)
            rec.str(Tag.CAL_TITLE, ev.title)
            rec.u64(Tag.CAL_START, ev.start)
            rec.u64(Tag.CAL_END, ev.end)
            if (ev.location.isNotEmpty()) rec.str(Tag.CAL_LOCATION, ev.location)
            if (ev.allDay) rec.u8(Tag.CAL_ALL_DAY, 1)
            w.bytes(Tag.CAL_RECORD, rec.toByteArray())
        }
        return w.toByteArray()
    }

    fun encodeTimeSync(data: TimeSyncData): ByteArray {
        val w = TlvWriter()
        w.u64(Tag.TIME_UTC_MS, data.utcMs)
        w.i32(Tag.TIME_TZ_OFFSET, data.tzOffsetMinutes)
        if (data.tzName.isNotEmpty()) w.str(Tag.TIME_TZ_NAME, data.tzName)
        return w.toByteArray()
    }

    fun encodeNotifDismissAck(): ByteArray = ByteArray(0)
    fun encodeMsgQuickReplyAck(): ByteArray = ByteArray(0)

    // Decoders for device -> app messages

    data class NotifDismiss(val notifKey: String)
    data class MsgMarkRead(val convId: String)
    data class MsgQuickReply(val convId: String, val optionIndex: Int)

    fun decodeNotifDismiss(body: ByteArray): NotifDismiss? {
        val items = Tlv.parse(body)
        val key = items.firstOrNull { it.tag == Tag.NOTIF_KEY }?.str() ?: return null
        return NotifDismiss(key)
    }

    fun decodeMsgMarkRead(body: ByteArray): MsgMarkRead? {
        val items = Tlv.parse(body)
        val convId = items.firstOrNull { it.tag == Tag.MSG_CONV_ID }?.str() ?: return null
        return MsgMarkRead(convId)
    }

    fun decodeMsgQuickReply(body: ByteArray): MsgQuickReply? {
        val items = Tlv.parse(body)
        val convId = items.firstOrNull { it.tag == Tag.MSG_CONV_ID }?.str() ?: return null
        val idx = items.firstOrNull { it.tag == Tag.QUICK_REPLY_IDX }?.u8() ?: return null
        return MsgQuickReply(convId, idx)
    }
}
