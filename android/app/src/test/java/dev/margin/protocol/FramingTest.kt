// SPDX-License-Identifier: MIT
package dev.margin.protocol

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class FramingTest {
    private fun roundTrip(size: Int, frameSize: Int, window: Int) {
        val body = ByteArray(size) { (it * 31 + 7).toByte() }
        val f = Fragmenter(42, MsgType.CHAT_DELTA, body, frameSize, window)
        val r = Reassembler()
        var got: Message? = null
        var guard = 0
        while (!f.done) {
            check(guard++ < 100_000) { "stalled" }
            val frame = f.next() ?: error("window stalled at size=$size frame=$frameSize window=$window")
            assertTrue(frame.size <= frameSize)
            when (val res = r.feed(frame)) {
                is FeedResult.Complete -> got = res.message
                is FeedResult.Error -> error("error $res")
                else -> {}
            }
            if (r.ackRequested) {
                val ack = Ctrl.decode(Ctrl.ack(r.msgId, r.nextFrag, window)) as Ctrl.Ack
                f.onAck(ack.nextFrag)
            }
        }
        assertEquals(MsgType.CHAT_DELTA.code, got!!.type)
        assertArrayEquals(body, got.body)
    }

    @Test
    fun roundTrips() {
        for (s in listOf(0, 1, 8, 9, 100, 233, 1000, 65536))
            for (fs in listOf(20, 23, 100, 244))
                for (w in listOf(1, 2, 8)) roundTrip(s, fs, w)
    }

    @Test
    fun fragmentCounts() {
        assertEquals(1, fragmentCount(0, 20))
        assertEquals(1, fragmentCount(8, 20))
        assertEquals(2, fragmentCount(9, 20))
        assertEquals(3, fragmentCount(25, 20))
        assertEquals(0, fragmentCount(10, 19))
    }

    @Test
    fun gapNacksThenRecovers() {
        val body = ByteArray(200) { it.toByte() }
        val f = Fragmenter(9, MsgType.FILE_OFFER, body, 20, 8)
        val r = Reassembler()
        r.feed(f.next()!!)
        f.next() // lost
        val res = r.feed(f.next()!!)
        assertEquals(FeedResult.Error(NackReason.OUT_OF_ORDER), res)
        f.rewindTo(r.nextFrag)
        var last: FeedResult = FeedResult.NeedMore
        while (!f.allSent) {
            if (!f.canSend) f.onAck(r.nextFrag)
            last = r.feed(f.next()!!)
        }
        assertTrue(last is FeedResult.Complete)
    }

    @Test
    fun duplicateAfterCompleteNotRedelivered() {
        val f = Fragmenter(5, MsgType.CHAT_DONE, byteArrayOf(1, 2, 3, 4), 20)
        val frame = f.next()!!
        val r = Reassembler()
        assertTrue(r.feed(frame) is FeedResult.Complete)
        assertEquals(FeedResult.Duplicate, r.feed(frame))
        assertTrue(r.ackRequested)
    }

    @Test
    fun tlvRoundTrip() {
        val body = Messages.quickReplies(1, 2, List(8) { "opt$it" })
        val opts = Tlv.parse(body).filter { it.tag == Tag.OPTION }
        assertEquals(Protocol.MAX_QUICK_REPLIES, opts.size)
    }
}
