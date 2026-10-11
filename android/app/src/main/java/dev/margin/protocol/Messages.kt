// SPDX-License-Identifier: MIT
// Typed encoders/decoders for v1 message bodies (TLV). See docs/BLE_PROTOCOL.md §7.
package dev.margin.protocol

object Messages {
    fun chatDelta(convId: Long, turnId: Long, role: Role, text: String): ByteArray =
        TlvWriter().u32(Tag.CONV_ID, convId).u32(Tag.TURN_ID, turnId).u8(Tag.ROLE, role.code).str(Tag.TEXT, text)
            .toByteArray()

    fun chatDone(convId: Long, turnId: Long, finish: FinishReason): ByteArray =
        TlvWriter().u32(Tag.CONV_ID, convId).u32(Tag.TURN_ID, turnId).u8(Tag.FINISH, finish.code).toByteArray()

    fun thinking(convId: Long, state: ThinkingState, label: String = ""): ByteArray =
        TlvWriter().u32(Tag.CONV_ID, convId).u8(Tag.STATUS, state.code).str(Tag.LABEL, label).toByteArray()

    fun quickReplies(convId: Long, turnId: Long, options: List<String>): ByteArray {
        val w = TlvWriter().u32(Tag.CONV_ID, convId).u32(Tag.TURN_ID, turnId)
        options.take(Protocol.MAX_QUICK_REPLIES).forEach { w.str(Tag.OPTION, it) }
        return w.toByteArray()
    }

    fun hello(name: String, version: String, features: Int): ByteArray =
        TlvWriter().u8(Tag.PROTO_VERSION, Protocol.VERSION).str(Tag.NAME, name).str(Tag.VERSION, version)
            .u32(Tag.FEATURES, features.toLong()).toByteArray()

    fun fileOffer(transferId: Int, fileName: String, size: Long, crc32: Long, format: String): ByteArray =
        TlvWriter().u16(Tag.TRANSFER_ID, transferId).str(Tag.FILE_NAME, fileName).u32(Tag.FILE_SIZE, size)
            .u32(Tag.CRC32, crc32).str(Tag.FORMAT, format).toByteArray()

    fun fileComplete(transferId: Int, crc32: Long): ByteArray =
        TlvWriter().u16(Tag.TRANSFER_ID, transferId).u32(Tag.CRC32, crc32).toByteArray()

    fun syncRequest(sinceSeq: Long): ByteArray = TlvWriter().u32(Tag.SEQ, sinceSeq).toByteArray()

    // ---- device -> app
    data class DeviceHello(val protoVersion: Int, val name: String, val version: String, val features: Int,
                           val columns: Int, val rows: Int, val maxBody: Long)

    data class UserMessage(val convId: Long, val source: Int, val text: String, val optionIndex: Int?)

    data class PassageShare(
        val intent: PassageIntent, val bookId: String?, val bookTitle: String?, val chapterTitle: String?,
        val spine: Int, val offset: Long, val percentBp: Int, val text: String,
    )

    fun decodeHello(body: ByteArray): DeviceHello {
        var v = 0; var name = ""; var ver = ""; var feat = 0; var cols = 0; var rows = 0; var maxBody = 0L
        for (it in Tlv.parse(body)) when (it.tag) {
            Tag.PROTO_VERSION -> v = it.u8()
            Tag.NAME -> name = it.str()
            Tag.VERSION -> ver = it.str()
            Tag.FEATURES -> feat = it.u32().toInt()
            Tag.COLUMNS -> cols = it.u16()
            Tag.ROWS -> rows = it.u16()
            Tag.MAX_BODY -> maxBody = it.u32()
        }
        return DeviceHello(v, name, ver, feat, cols, rows, maxBody)
    }

    fun decodeUserMessage(body: ByteArray): UserMessage {
        var conv = 0L; var src = 0; var text = ""; var opt: Int? = null
        for (it in Tlv.parse(body)) when (it.tag) {
            Tag.CONV_ID -> conv = it.u32()
            Tag.SOURCE -> src = it.u8()
            Tag.TEXT -> text = it.str()
            Tag.OPTION_INDEX -> opt = it.u8()
        }
        return UserMessage(conv, src, text, opt)
    }

    fun decodePassageShare(body: ByteArray): PassageShare {
        var intent = PassageIntent.ASK; var id: String? = null; var title: String? = null; var ch: String? = null
        var spine = 0; var off = 0L; var pct = 0; var text = ""
        for (it in Tlv.parse(body)) when (it.tag) {
            Tag.INTENT -> intent = PassageIntent.of(it.u8())
            Tag.BOOK_ID -> id = it.str()
            Tag.BOOK_TITLE -> title = it.str()
            Tag.CHAPTER_TITLE -> ch = it.str()
            Tag.SPINE -> spine = it.u16()
            Tag.OFFSET -> off = it.u32()
            Tag.PERCENT -> pct = it.u16()
            Tag.TEXT -> text = it.str()
        }
        return PassageShare(intent, id, title, ch, spine, off, pct, text)
    }
}
