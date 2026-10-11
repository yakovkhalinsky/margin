// SPDX-License-Identifier: MIT
// Same vectors as firmware/test/test_protocol.cpp (golden_chat_delta_frames) and docs/BLE_PROTOCOL.md.
package dev.margin.protocol

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class GoldenVectorsTest {
    private fun ByteArray.hex() = joinToString("") { "%02x".format(it) }

    @Test
    fun chatDeltaBodyAndFrames() {
        val body = Messages.chatDelta(convId = 1, turnId = 2, role = Role.ASSISTANT, text = "Hi")
        assertEquals("0104000100000002040002000000040100010302004869", body.hex())
        val f = Fragmenter(msgId = 7, type = MsgType.CHAT_DELTA, body = body, frameSize = 20, window = 8)
        assertEquals("0107000001100000170000000104000100000002", f.next()!!.hex())
        assertEquals("06070100040002000000040100010302004869", f.next()!!.hex())
        assertNull(f.next())
    }

    @Test
    fun crc32CheckValue() {
        val crc = java.util.zip.CRC32().apply { update("123456789".toByteArray()) }.value
        assertEquals(0xCBF43926L, crc)
    }
}
