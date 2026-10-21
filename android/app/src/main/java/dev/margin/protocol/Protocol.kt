// SPDX-License-Identifier: MIT
// margin BLE wire protocol v1. Mirror of firmware/lib/MarginProtocol/include/margin/protocol.h.
// See docs/BLE_PROTOCOL.md. Keep both sides in sync; GoldenVectorsTest guards the byte layout.
package dev.margin.protocol

import java.util.UUID

object Protocol {
    const val VERSION: Int = 1

    val SERVICE_UUID: UUID = UUID.fromString("d0ed0001-b422-469c-8266-d0abd145eb37")
    val RX_CHAR_UUID: UUID = UUID.fromString("d0ed0002-b422-469c-8266-d0abd145eb37")   // app -> device
    val TX_CHAR_UUID: UUID = UUID.fromString("d0ed0003-b422-469c-8266-d0abd145eb37")   // device -> app (notify)
    val CTRL_CHAR_UUID: UUID = UUID.fromString("d0ed0004-b422-469c-8266-d0abd145eb37") // read/write/notify
    val FILE_CHAR_UUID: UUID = UUID.fromString("d0ed0005-b422-469c-8266-d0abd145eb37") // app -> device bulk

    const val FRAG_HEADER_SIZE = 4
    const val MSG_HEADER_SIZE = 8
    const val MIN_FRAME_SIZE = 20
    const val MAX_FRAME_SIZE = 244
    const val MAX_BODY_SIZE = 65536
    const val DEFAULT_WINDOW = 8
    const val FILE_CHUNK_HEADER_SIZE = 6
    const val MAX_QUICK_REPLIES = 6

    const val FLAG_FIRST = 0x01
    const val FLAG_LAST = 0x02
    const val FLAG_ACK_REQ = 0x04
}

enum class MsgType(val code: Int) {
    HELLO(0x01), ERROR(0x02), DEVICE_STATUS(0x03),
    CHAT_DELTA(0x10), CHAT_DONE(0x11), THINKING_STATUS(0x12), QUICK_REPLIES(0x13),
    USER_MESSAGE(0x14), CHAT_CANCEL(0x15), CHAT_NEW(0x16),
    PASSAGE_SHARE(0x20),
    FILE_OFFER(0x30), FILE_ACCEPT(0x31), FILE_COMPLETE(0x32), FILE_RESULT(0x33), FILE_ABORT(0x34),
    SYNC_REQUEST(0x40), SYNC_RECORDS(0x41), SYNC_DONE(0x42), SYNC_ACK(0x43),
    BIBLE_CONTEXT(0x50), BIBLE_NOTE_SYNC_REQ(0x51), BIBLE_NOTE_RECORDS(0x52),
    BIBLE_NOTE_SYNC_DONE(0x53), BIBLE_NOTE_PUSH(0x54), BIBLE_NOTE_ACK(0x55),
    // Companion app messages (see docs/companion-v1.md)
    NOTIF_PUSH(0x70), NOTIF_DISMISS(0x71), NOTIF_CLEAR(0x72), NOTIF_DISMISS_ACK(0x73),
    MSG_THREAD_PUSH(0x74), MSG_MARK_READ(0x75), MSG_QUICK_REPLY(0x76), MSG_QUICK_REPLY_ACK(0x77),
    CALENDAR_SYNC(0x80), CALENDAR_REQUEST(0x81),
    TIME_SYNC(0x90), TIME_REQUEST(0x91);

    companion object {
        private val byCode = entries.associateBy { it.code }
        fun of(code: Int): MsgType? = byCode[code]
    }
}

enum class CtrlOp(val code: Int) { ACK(0x01), NACK(0x02), FILE_ACK(0x03), STATUS(0x10) }

enum class NackReason(val code: Int) {
    NONE(0), BAD_VERSION(1), OUT_OF_ORDER(2), TOO_LARGE(3), BAD_LENGTH(4), BUSY(5), UNSUPPORTED(6), MALFORMED(7);
    companion object { fun of(code: Int) = entries.firstOrNull { it.code == code } ?: MALFORMED }
}

/** Global TLV tag registry. Unknown tags must be ignored. */
object Tag {
    const val CONV_ID = 0x01; const val TURN_ID = 0x02; const val TEXT = 0x03; const val ROLE = 0x04
    const val STATUS = 0x05; const val LABEL = 0x06; const val OPTION = 0x07; const val FINISH = 0x08
    const val INTENT = 0x09; const val BOOK_ID = 0x0A; const val BOOK_TITLE = 0x0B; const val CHAPTER_TITLE = 0x0C
    const val SPINE = 0x0D; const val OFFSET = 0x0E; const val PERCENT = 0x0F; const val TRANSFER_ID = 0x10
    const val FILE_NAME = 0x11; const val FILE_SIZE = 0x12; const val CRC32 = 0x13; const val FORMAT = 0x14
    const val RESULT = 0x15; const val PATH = 0x16; const val RECORD = 0x17; const val RECORD_KIND = 0x18
    const val RECORD_ID = 0x19; const val SEQ = 0x1A; const val TIMESTAMP = 0x1B; const val NOTE = 0x1C
    const val DELETED = 0x1D; const val ERROR_CODE = 0x1E; const val PROTO_VERSION = 0x20; const val NAME = 0x21
    const val VERSION = 0x22; const val FEATURES = 0x23; const val COLUMNS = 0x24; const val ROWS = 0x25
    const val BATTERY = 0x26; const val CHARGING = 0x27; const val SOURCE = 0x28; const val OPTION_INDEX = 0x29
    const val FREE_KB = 0x2A; const val MAX_BODY = 0x2B; const val RESUME_OFFSET = 0x2C
    // Bible study tags (0x60-0x6F)
    const val BIBLE_REF = 0x60; const val BIBLE_BOOK_NAME = 0x61; const val BIBLE_CHAPTER = 0x62
    const val BIBLE_VERSE = 0x63; const val BIBLE_NOTE_TYPE = 0x64; const val BIBLE_HIGHLIGHT_COLOR = 0x65
    const val BIBLE_NOTE_RECORD = 0x66
    // Companion app tags (see docs/companion-v1.md)
    const val NOTIF_ID = 0x70; const val NOTIF_KEY = 0x71; const val NOTIF_APP = 0x72
    const val NOTIF_APP_NAME = 0x73; const val NOTIF_TITLE = 0x74; const val NOTIF_BODY = 0x75
    const val NOTIF_TIMESTAMP = 0x76; const val NOTIF_ICON = 0x77
    const val MSG_CONV_ID = 0x78; const val MSG_SENDER = 0x79; const val MSG_SENDER_KEY = 0x7A
    const val MSG_TEXT = 0x7B; const val MSG_TIMESTAMP = 0x7C; const val MSG_IS_OUTGOING = 0x7D
    const val MSG_RECORD = 0x7E; const val MSG_THREAD_TITLE = 0x7F
    const val CAL_EVENT_ID = 0x80; const val CAL_TITLE = 0x81; const val CAL_START = 0x82
    const val CAL_END = 0x83; const val CAL_LOCATION = 0x84; const val CAL_ALL_DAY = 0x85
    const val CAL_RECORD = 0x86; const val QUICK_REPLY_OPT = 0x87; const val QUICK_REPLY_IDX = 0x88
    const val TIME_UTC_MS = 0x90; const val TIME_TZ_OFFSET = 0x91; const val TIME_TZ_NAME = 0x92
}

object Feature {
    const val CHAT = 1; const val PASSAGE = 2; const val FILE_PUSH = 4; const val SYNC = 8
    const val COMPANION = 16  // notifications, messages, calendar
}

enum class Role(val code: Int) { USER(0), ASSISTANT(1), SYSTEM(2), TOOL(3) }
enum class ThinkingState(val code: Int) { IDLE(0), THINKING(1), TOOL_RUNNING(2), STREAMING(3), ERROR(4) }
enum class FinishReason(val code: Int) { STOP(0), LENGTH(1), ERROR(2), CANCELLED(3) }
enum class InputSource(val code: Int) { KEYBOARD(0), QUICK_REPLY(1), BUTTON(2) }
enum class PassageIntent(val code: Int) {
    ASK(0), EXPLAIN(1), SUMMARISE_SELECTION(2), SUMMARISE_CHAPTER(3), DEFINE(4), TRANSLATE(5);
    companion object { fun of(code: Int) = entries.firstOrNull { it.code == code } ?: ASK }
}

enum class BibleNoteType(val code: Int) { HIGHLIGHT(0), NOTE(1) }
enum class BibleHighlightColor(val code: Int) {
    YELLOW(0), GREEN(1), BLUE(2), PINK(3);
    companion object { fun of(code: Int) = entries.firstOrNull { it.code == code } ?: YELLOW }
}

enum class NotifIcon(val code: Int) {
    DEFAULT(0), MESSAGE(1), EMAIL(2), CALL(3), CALENDAR(4);
    companion object { fun of(code: Int) = entries.firstOrNull { it.code == code } ?: DEFAULT }
}

/** Notification push message (phone -> device). */
data class NotifPush(
    val notifId: Int,
    val notifKey: String,
    val appPkg: String,
    val appName: String,
    val title: String,
    val body: String,
    val timestampMs: Long,
    val category: String = "",
    val priority: Int = 2
)

/** Message record for a conversation. */
data class MessageRecord(
    val sender: String,
    val senderKey: String,
    val text: String,
    val timestampMs: Long,
    val isOutgoing: Boolean
)

/** Message thread push (phone -> device). */
data class MsgThreadPush(
    val convId: String,
    val threadTitle: String,
    val messages: List<MessageRecord>,
    val quickReplies: List<String> = emptyList()
)

/** Calendar event for sync. */
data class CalendarEvent(
    val eventId: Long,
    val title: String,
    val startMs: Long,
    val endMs: Long,
    val location: String = "",
    val allDay: Boolean = false
)

/** Calendar sync message (phone -> device). */
data class CalendarSync(
    val events: List<CalendarEvent>
)

/** Time sync message (phone -> device). */
data class TimeSync(
    val utcMs: Long,
    val tzOffsetMin: Int,
    val tzName: String
)

/** TLV encoder helper. */
class TlvWriter(private val capacity: Int = 4096) {
    private val buffer = ByteArray(capacity)
    private var pos = 0

    fun u8(tag: Int, value: Int): TlvWriter {
        if (pos + 3 > capacity) return this
        buffer[pos++] = tag.toByte()
        buffer[pos++] = 1
        buffer[pos++] = value.toByte()
        return this
    }

    fun u16(tag: Int, value: Int): TlvWriter {
        if (pos + 4 > capacity) return this
        buffer[pos++] = tag.toByte()
        buffer[pos++] = 2
        buffer[pos++] = (value shr 8).toByte()
        buffer[pos++] = value.toByte()
        return this
    }

    fun u32(tag: Int, value: Int): TlvWriter {
        if (pos + 6 > capacity) return this
        buffer[pos++] = tag.toByte()
        buffer[pos++] = 4
        buffer[pos++] = (value shr 24).toByte()
        buffer[pos++] = (value shr 16).toByte()
        buffer[pos++] = (value shr 8).toByte()
        buffer[pos++] = value.toByte()
        return this
    }

    fun u64(tag: Int, value: Long): TlvWriter {
        if (pos + 10 > capacity) return this
        buffer[pos++] = tag.toByte()
        buffer[pos++] = 8
        buffer[pos++] = (value shr 56).toByte()
        buffer[pos++] = (value shr 48).toByte()
        buffer[pos++] = (value shr 40).toByte()
        buffer[pos++] = (value shr 32).toByte()
        buffer[pos++] = (value shr 24).toByte()
        buffer[pos++] = (value shr 16).toByte()
        buffer[pos++] = (value shr 8).toByte()
        buffer[pos++] = value.toByte()
        return this
    }

    fun i32(tag: Int, value: Int): TlvWriter = u32(tag, value)

    fun str(tag: Int, value: String): TlvWriter {
        val bytes = value.toByteArray(Charsets.UTF_8)
        if (bytes.size > 255) {
            val truncated = value.take(250).toByteArray(Charsets.UTF_8)
            return rawBytes(tag, truncated)
        }
        return rawBytes(tag, bytes)
    }

    fun rawBytes(tag: Int, bytes: ByteArray): TlvWriter {
        if (pos + 2 + bytes.size > capacity) return this
        buffer[pos++] = tag.toByte()
        buffer[pos++] = bytes.size.toByte()
        System.arraycopy(bytes, 0, buffer, pos, bytes.size)
        pos += bytes.size
        return this
    }

    fun nested(tag: Int, nested: TlvWriter): TlvWriter {
        val bytes = nested.toByteArray()
        if (pos + 2 + bytes.size > capacity) return this
        buffer[pos++] = tag.toByte()
        buffer[pos++] = bytes.size.toByte()
        System.arraycopy(bytes, 0, buffer, pos, bytes.size)
        pos += bytes.size
        return this
    }

    fun toByteArray(): ByteArray = buffer.copyOf(pos)
}

/** Build a complete BLE frame with message header. */
fun buildFrame(msgType: MsgType, body: ByteArray): ByteArray {
    val seqNo: Short = 0
    val flags: Byte = (Protocol.FLAG_FIRST or Protocol.FLAG_LAST).toByte()
    val totalLen = body.size

    val frame = ByteArray(Protocol.MSG_HEADER_SIZE + body.size)
    frame[0] = Protocol.VERSION.toByte()
    frame[1] = msgType.code.toByte()
    frame[2] = (seqNo.toInt() shr 8).toByte()
    frame[3] = seqNo.toByte()
    frame[4] = flags
    frame[5] = 0 // reserved
    frame[6] = (totalLen shr 8).toByte()
    frame[7] = totalLen.toByte()
    System.arraycopy(body, 0, frame, Protocol.MSG_HEADER_SIZE, body.size)
    return frame
}

/** Encode NotifPush message. */
fun encodeNotifPush(notif: NotifPush): ByteArray {
    val body = TlvWriter()
        .u32(Tag.NOTIF_ID, notif.notifId)
        .str(Tag.NOTIF_KEY, notif.notifKey)
        .str(Tag.NOTIF_APP, notif.appPkg)
        .str(Tag.NOTIF_APP_NAME, notif.appName)
        .str(Tag.NOTIF_TITLE, notif.title)
        .str(Tag.NOTIF_BODY, notif.body)
        .u64(Tag.NOTIF_TIMESTAMP, notif.timestampMs)
        .toByteArray()
    return buildFrame(MsgType.NOTIF_PUSH, body)
}

/** Encode NotifDismiss message (device -> phone). */
fun encodeNotifDismiss(notifKey: String): ByteArray {
    val body = TlvWriter()
        .str(Tag.NOTIF_KEY, notifKey)
        .toByteArray()
    return buildFrame(MsgType.NOTIF_DISMISS, body)
}

/** Encode NotifClear message (phone -> device). */
fun encodeNotifClear(): ByteArray {
    return buildFrame(MsgType.NOTIF_CLEAR, ByteArray(0))
}

/** Encode MsgThreadPush message. */
fun encodeMsgThreadPush(thread: MsgThreadPush): ByteArray {
    val body = TlvWriter()
        .str(Tag.MSG_CONV_ID, thread.convId)
        .str(Tag.MSG_THREAD_TITLE, thread.threadTitle)

    for (msg in thread.messages) {
        val record = TlvWriter()
            .str(Tag.MSG_SENDER, msg.sender)
            .str(Tag.MSG_SENDER_KEY, msg.senderKey)
            .str(Tag.MSG_TEXT, msg.text)
            .u64(Tag.MSG_TIMESTAMP, msg.timestampMs)
            .u8(Tag.MSG_IS_OUTGOING, if (msg.isOutgoing) 1 else 0)
        body.nested(Tag.MSG_RECORD, record)
    }

    for (reply in thread.quickReplies) {
        body.str(Tag.QUICK_REPLY_OPT, reply)
    }

    return buildFrame(MsgType.MSG_THREAD_PUSH, body.toByteArray())
}

/** Encode CalendarSync message. */
fun encodeCalendarSync(sync: CalendarSync): ByteArray {
    val body = TlvWriter()

    for (event in sync.events) {
        val record = TlvWriter()
            .u64(Tag.CAL_EVENT_ID, event.eventId)
            .str(Tag.CAL_TITLE, event.title)
            .u64(Tag.CAL_START, event.startMs)
            .u64(Tag.CAL_END, event.endMs)
            .str(Tag.CAL_LOCATION, event.location)
            .u8(Tag.CAL_ALL_DAY, if (event.allDay) 1 else 0)
        body.nested(Tag.CAL_RECORD, record)
    }

    return buildFrame(MsgType.CALENDAR_SYNC, body.toByteArray())
}

/** Encode TimeSync message. */
fun encodeTimeSync(sync: TimeSync): ByteArray {
    val body = TlvWriter()
        .u64(Tag.TIME_UTC_MS, sync.utcMs)
        .i32(Tag.TIME_TZ_OFFSET, sync.tzOffsetMin)
        .str(Tag.TIME_TZ_NAME, sync.tzName)
        .toByteArray()
    return buildFrame(MsgType.TIME_SYNC, body)
}
