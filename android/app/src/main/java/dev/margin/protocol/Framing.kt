// SPDX-License-Identifier: MIT
// Fragmentation / reassembly / CTRL packets. Byte-for-byte compatible with firmware/lib/MarginProtocol.
package dev.margin.protocol

fun fragmentCount(bodyLen: Int, frameSize: Int): Int {
    if (frameSize < Protocol.MIN_FRAME_SIZE) return 0
    val p = frameSize - Protocol.FRAG_HEADER_SIZE
    val firstBody = p - Protocol.MSG_HEADER_SIZE
    var count = 1L
    if (bodyLen > firstBody) count += (bodyLen - firstBody + p - 1L) / p
    return if (count > 0xFFFF) 0 else count.toInt()
}

/** Splits one message into frames and tracks a go-back-N window. Not thread-safe. */
class Fragmenter(
    val msgId: Int,
    private val type: MsgType,
    private val body: ByteArray,
    private val frameSize: Int,
    private val window: Int = Protocol.DEFAULT_WINDOW,
) {
    val count: Int = fragmentCount(body.size, frameSize)
    var sent = 0; private set
    var acked = 0; private set

    init {
        require(count > 0) { "body too large for frame size $frameSize" }
        require(window in 1..255)
    }

    val canSend get() = sent < count && sent - acked < window
    val allSent get() = sent >= count
    val done get() = acked >= count

    fun next(): ByteArray? {
        if (!canSend) return null
        return frame(sent++)
    }

    fun onAck(nextFrag: Int) { if (nextFrag in (acked + 1)..sent) acked = nextFrag }
    fun rewindTo(fragIdx: Int) { val i = maxOf(fragIdx, acked); if (i <= sent) sent = i }

    private fun frame(idx: Int): ByteArray {
        val p = frameSize - Protocol.FRAG_HEADER_SIZE
        val firstBody = p - Protocol.MSG_HEADER_SIZE
        var flags = 0
        if (idx == 0) flags = flags or Protocol.FLAG_FIRST
        if (idx + 1 == count) flags = flags or Protocol.FLAG_LAST
        if ((flags and Protocol.FLAG_LAST) != 0 || idx + 1 - acked >= window) flags = flags or Protocol.FLAG_ACK_REQ
        val header = byteArrayOf(flags.toByte(), msgId.toByte()) + Le.u16(idx)
        return if (idx == 0) {
            val chunk = minOf(body.size, firstBody)
            header + byteArrayOf(Protocol.VERSION.toByte(), type.code.toByte(), 0, 0) + Le.u32(body.size.toLong()) +
                body.copyOfRange(0, chunk)
        } else {
            val off = firstBody + (idx - 1) * p
            header + body.copyOfRange(off, minOf(body.size, off + p))
        }
    }
}

class Message(val msgId: Int, val type: Int, val body: ByteArray)

sealed interface FeedResult {
    data object NeedMore : FeedResult
    data class Complete(val message: Message) : FeedResult
    data object Duplicate : FeedResult
    data class Error(val reason: NackReason) : FeedResult
}

/** Rebuilds messages from frames. Same rules as the C++ Reassembler (see docs/BLE_PROTOCOL.md §5). */
class Reassembler(private val capacity: Int = Protocol.MAX_BODY_SIZE) {
    var msgId = 0; private set
    var nextFrag = 0; private set
    var ackRequested = false; private set
    var inProgress = false; private set

    private var buf = ByteArray(0)
    private var type = 0
    private var received = 0
    private var completedId: Int? = null
    private var completedCount = 0

    fun reset() { inProgress = false; ackRequested = false; nextFrag = 0; completedId = null }

    fun feed(frame: ByteArray): FeedResult {
        ackRequested = false
        if (frame.size < Protocol.FRAG_HEADER_SIZE) return fail(NackReason.MALFORMED, inProgress)
        val flags = frame[0].toInt() and 0xFF
        val id = frame[1].toInt() and 0xFF
        val idx = Le.getU16(frame, 2)
        var off = Protocol.FRAG_HEADER_SIZE

        if (!inProgress && completedId == id) return duplicate(flags, id, completedCount)

        if (flags and Protocol.FLAG_FIRST != 0) {
            msgId = id; nextFrag = 0; inProgress = false
            if (idx != 0 || frame.size - off < Protocol.MSG_HEADER_SIZE) return fail(NackReason.MALFORMED, false)
            if ((frame[off].toInt() and 0xFF) != Protocol.VERSION) return fail(NackReason.BAD_VERSION, false)
            val bodyLen = Le.getU32(frame, off + 4)
            if (bodyLen > capacity) return fail(NackReason.TOO_LARGE, false)
            type = frame[off + 1].toInt() and 0xFF
            buf = ByteArray(bodyLen.toInt())
            received = 0
            inProgress = true
            off += Protocol.MSG_HEADER_SIZE
        } else {
            if (!inProgress || id != msgId) return fail(NackReason.OUT_OF_ORDER, false)
            if (idx < nextFrag) return duplicate(flags, id, nextFrag)
            if (idx > nextFrag) return fail(NackReason.OUT_OF_ORDER, true)
        }
        val plen = frame.size - off
        if (plen > buf.size - received) return fail(NackReason.BAD_LENGTH, false)
        System.arraycopy(frame, off, buf, received, plen)
        received += plen
        nextFrag++
        if (flags and Protocol.FLAG_LAST != 0) {
            inProgress = false
            if (received != buf.size) return fail(NackReason.BAD_LENGTH, false)
            completedId = msgId; completedCount = nextFrag
            ackRequested = true
            return FeedResult.Complete(Message(msgId, type, buf))
        }
        ackRequested = flags and Protocol.FLAG_ACK_REQ != 0
        return FeedResult.NeedMore
    }

    private fun duplicate(flags: Int, id: Int, next: Int): FeedResult {
        msgId = id; nextFrag = next
        ackRequested = flags and (Protocol.FLAG_ACK_REQ or Protocol.FLAG_LAST) != 0
        return FeedResult.Duplicate
    }

    private fun fail(reason: NackReason, keepPartial: Boolean): FeedResult {
        ackRequested = false
        if (!keepPartial) inProgress = false
        return FeedResult.Error(reason)
    }
}

sealed interface Ctrl {
    data class Ack(val msgId: Int, val nextFrag: Int, val credits: Int) : Ctrl
    data class Nack(val msgId: Int, val reason: NackReason, val nextFrag: Int) : Ctrl
    data class FileAck(val transferId: Int, val nextOffset: Long, val credits: Int) : Ctrl
    data class Status(val version: Int, val maxFrame: Int, val window: Int, val flags: Int, val maxBody: Long) : Ctrl

    companion object {
        fun ack(msgId: Int, nextFrag: Int, credits: Int) =
            byteArrayOf(CtrlOp.ACK.code.toByte(), msgId.toByte()) + Le.u16(nextFrag) + byteArrayOf(credits.toByte())

        fun nack(msgId: Int, reason: NackReason, nextFrag: Int) =
            byteArrayOf(CtrlOp.NACK.code.toByte(), msgId.toByte(), reason.code.toByte()) + Le.u16(nextFrag)

        fun decode(b: ByteArray): Ctrl? {
            if (b.isEmpty()) return null
            return when (b[0].toInt() and 0xFF) {
                CtrlOp.ACK.code -> if (b.size >= 5) Ack(b[1].toInt() and 0xFF, Le.getU16(b, 2), b[4].toInt() and 0xFF) else null
                CtrlOp.NACK.code -> if (b.size >= 5) Nack(b[1].toInt() and 0xFF, NackReason.of(b[2].toInt() and 0xFF), Le.getU16(b, 3)) else null
                CtrlOp.FILE_ACK.code -> if (b.size >= 8) FileAck(Le.getU16(b, 1), Le.getU32(b, 3), b[7].toInt() and 0xFF) else null
                CtrlOp.STATUS.code -> if (b.size >= 10) Status(b[1].toInt() and 0xFF, Le.getU16(b, 2), b[4].toInt() and 0xFF, b[5].toInt() and 0xFF, Le.getU32(b, 6)) else null
                else -> null
            }
        }
    }
}

object FileChunk {
    fun encode(transferId: Int, offset: Long, data: ByteArray): ByteArray = Le.u16(transferId) + Le.u32(offset) + data
}
