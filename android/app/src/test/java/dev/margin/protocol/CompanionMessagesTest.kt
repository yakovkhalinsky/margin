// SPDX-License-Identifier: MIT
// Unit tests for companion message encoding/decoding.
// Test vectors are shared with firmware/test/test_companion.cpp.
package dev.margin.protocol

import org.junit.Assert.*
import org.junit.Test

class CompanionMessagesTest {

    @Test
    fun testTimeSyncEncode() {
        // Golden vector from docs/companion-v1.md
        val data = TimeSyncData(
            utcMs = 1698076800000L,
            tzOffsetMinutes = -240, // EDT, UTC-4
            tzName = "America/New_York"
        )
        val body = CompanionMessages.encodeTimeSync(data)

        // Parse back to verify
        val items = Tlv.parse(body)
        val utcMs = items.first { it.tag == Tag.TIME_UTC_MS }.u64()
        val tzOffset = items.first { it.tag == Tag.TIME_TZ_OFFSET }.i32()
        val tzName = items.first { it.tag == Tag.TIME_TZ_NAME }.str()

        assertEquals(1698076800000L, utcMs)
        assertEquals(-240, tzOffset)
        assertEquals("America/New_York", tzName)
    }

    @Test
    fun testNotifPushEncode() {
        val data = NotifPushData(
            notifKey = "com.whatsapp|null|12345",
            notifId = 12345,
            app = "com.whatsapp",
            appName = "WhatsApp",
            title = "John",
            body = "Hey, are you coming to dinner?",
            timestamp = 1698076800000L,
            icon = NotifIcon.MESSAGE,
            quickReplies = listOf("Yes", "No", "Maybe later")
        )
        val body = CompanionMessages.encodeNotifPush(data)

        val items = Tlv.parse(body)

        assertEquals("com.whatsapp|null|12345", items.first { it.tag == Tag.NOTIF_KEY }.str())
        assertEquals(12345L, items.first { it.tag == Tag.NOTIF_ID }.u32())
        assertEquals("com.whatsapp", items.first { it.tag == Tag.NOTIF_APP }.str())
        assertEquals("WhatsApp", items.first { it.tag == Tag.NOTIF_APP_NAME }.str())
        assertEquals("John", items.first { it.tag == Tag.NOTIF_TITLE }.str())
        assertEquals("Hey, are you coming to dinner?", items.first { it.tag == Tag.NOTIF_BODY }.str())
        assertEquals(1698076800000L, items.first { it.tag == Tag.NOTIF_TIMESTAMP }.u64())
        assertEquals(1, items.first { it.tag == Tag.NOTIF_ICON }.u8())

        val qrItems = items.filter { it.tag == Tag.QUICK_REPLY_OPT }
        assertEquals(3, qrItems.size)
        assertEquals("Yes", qrItems[0].str())
        assertEquals("No", qrItems[1].str())
        assertEquals("Maybe later", qrItems[2].str())
    }

    @Test
    fun testMsgThreadPushEncode() {
        val data = MsgThreadPushData(
            convId = "com.whatsapp:thread123",
            threadTitle = "Family Group",
            app = "com.whatsapp",
            messages = listOf(
                MessageRecord(
                    sender = "Mom",
                    text = "Dinner at 7?",
                    timestamp = 1698076700000L,
                    isOutgoing = false
                ),
                MessageRecord(
                    sender = "You",
                    text = "Sure!",
                    timestamp = 1698076750000L,
                    isOutgoing = true
                )
            ),
            quickReplies = listOf("OK", "👍")
        )
        val body = CompanionMessages.encodeMsgThreadPush(data)

        val items = Tlv.parse(body)

        assertEquals("com.whatsapp:thread123", items.first { it.tag == Tag.MSG_CONV_ID }.str())
        assertEquals("Family Group", items.first { it.tag == Tag.MSG_THREAD_TITLE }.str())
        assertEquals("com.whatsapp", items.first { it.tag == Tag.NOTIF_APP }.str())

        val records = items.filter { it.tag == Tag.MSG_RECORD }
        assertEquals(2, records.size)

        // Parse nested TLV for first message
        val msg1Items = Tlv.parse(records[0].value)
        assertEquals("Mom", msg1Items.first { it.tag == Tag.MSG_SENDER }.str())
        assertEquals("Dinner at 7?", msg1Items.first { it.tag == Tag.MSG_TEXT }.str())
        assertEquals(1698076700000L, msg1Items.first { it.tag == Tag.MSG_TIMESTAMP }.u64())
    }

    @Test
    fun testCalendarSyncEncode() {
        val data = CalendarSyncData(
            events = listOf(
                CalendarEventData(
                    eventId = 1001,
                    title = "Team Meeting",
                    start = 1698080400000L, // 10:00 AM
                    end = 1698084000000L,   // 11:00 AM
                    location = "Conference Room A",
                    allDay = false
                ),
                CalendarEventData(
                    eventId = 1002,
                    title = "Birthday",
                    start = 1698105600000L,
                    end = 1698192000000L,
                    location = "",
                    allDay = true
                )
            )
        )
        val body = CompanionMessages.encodeCalendarSync(data)

        val items = Tlv.parse(body)
        val records = items.filter { it.tag == Tag.CAL_RECORD }
        assertEquals(2, records.size)

        val ev1Items = Tlv.parse(records[0].value)
        assertEquals(1001L, ev1Items.first { it.tag == Tag.CAL_EVENT_ID }.u32())
        assertEquals("Team Meeting", ev1Items.first { it.tag == Tag.CAL_TITLE }.str())
        assertEquals("Conference Room A", ev1Items.first { it.tag == Tag.CAL_LOCATION }.str())

        val ev2Items = Tlv.parse(records[1].value)
        assertEquals(1002L, ev2Items.first { it.tag == Tag.CAL_EVENT_ID }.u32())
        assertEquals(1, ev2Items.first { it.tag == Tag.CAL_ALL_DAY }.u8())
    }

    @Test
    fun testNotifDismissDecode() {
        val w = TlvWriter()
        w.str(Tag.NOTIF_KEY, "com.example|null|999")
        val body = w.toByteArray()

        val dismiss = CompanionMessages.decodeNotifDismiss(body)
        assertNotNull(dismiss)
        assertEquals("com.example|null|999", dismiss!!.notifKey)
    }

    @Test
    fun testMsgQuickReplyDecode() {
        val w = TlvWriter()
        w.str(Tag.MSG_CONV_ID, "com.whatsapp:abc123")
        w.u8(Tag.QUICK_REPLY_IDX, 2)
        val body = w.toByteArray()

        val reply = CompanionMessages.decodeMsgQuickReply(body)
        assertNotNull(reply)
        assertEquals("com.whatsapp:abc123", reply!!.convId)
        assertEquals(2, reply.optionIndex)
    }

    @Test
    fun testU64LeEncoding() {
        // Test that u64 encoding matches expected little-endian format
        val w = TlvWriter()
        w.u64(Tag.TIME_UTC_MS, 0x0102030405060708L)
        val body = w.toByteArray()

        // Tag (1) + len (2) + value (8) = 11 bytes
        assertEquals(11, body.size)
        assertEquals(Tag.TIME_UTC_MS.toByte(), body[0])
        assertEquals(8, Le.getU16(body, 1))
        // Little-endian: LSB first
        assertEquals(0x08.toByte(), body[3])
        assertEquals(0x07.toByte(), body[4])
        assertEquals(0x06.toByte(), body[5])
        assertEquals(0x05.toByte(), body[6])
        assertEquals(0x04.toByte(), body[7])
        assertEquals(0x03.toByte(), body[8])
        assertEquals(0x02.toByte(), body[9])
        assertEquals(0x01.toByte(), body[10])
    }

    @Test
    fun testI32LeEncoding() {
        val w = TlvWriter()
        w.i32(Tag.TIME_TZ_OFFSET, -240) // -240 minutes = -4 hours
        val body = w.toByteArray()

        assertEquals(7, body.size) // tag(1) + len(2) + value(4)
        val items = Tlv.parse(body)
        assertEquals(-240, items.first { it.tag == Tag.TIME_TZ_OFFSET }.i32())
    }
}
