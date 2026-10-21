// SPDX-License-Identifier: MIT
package dev.margin.protocol

import java.io.ByteArrayOutputStream

/** Builds a TLV body: [tag u8][len u16 LE][value]. */
class TlvWriter {
    private val out = ByteArrayOutputStream()

    fun bytes(tag: Int, value: ByteArray): TlvWriter {
        require(value.size <= 0xFFFF) { "TLV value too long: ${value.size}" }
        out.write(tag)
        out.write(value.size and 0xFF)
        out.write((value.size shr 8) and 0xFF)
        out.write(value)
        return this
    }
    fun str(tag: Int, s: String) = bytes(tag, s.toByteArray(Charsets.UTF_8))
    fun u8(tag: Int, v: Int) = bytes(tag, byteArrayOf(v.toByte()))
    fun u16(tag: Int, v: Int) = bytes(tag, Le.u16(v))
    fun u32(tag: Int, v: Long) = bytes(tag, Le.u32(v))
    fun u64(tag: Int, v: Long) = bytes(tag, Le.u64(v))
    fun i32(tag: Int, v: Int) = bytes(tag, Le.i32(v))
    fun toByteArray(): ByteArray = out.toByteArray()
}

data class TlvItem(val tag: Int, val value: ByteArray) {
    fun u8(): Int { require(value.size == 1); return value[0].toInt() and 0xFF }
    fun u16(): Int { require(value.size == 2); return Le.getU16(value, 0) }
    fun u32(): Long { require(value.size == 4); return Le.getU32(value, 0) }
    fun u64(): Long { require(value.size == 8); return Le.getU64(value, 0) }
    fun i32(): Int { require(value.size == 4); return Le.getI32(value, 0) }
    fun str(): String = value.toString(Charsets.UTF_8)
}

object Tlv {
    /** Parses a TLV body. Throws IllegalArgumentException if truncated. */
    fun parse(body: ByteArray): List<TlvItem> {
        val items = ArrayList<TlvItem>()
        var pos = 0
        while (pos < body.size) {
            require(body.size - pos >= 3) { "truncated TLV header" }
            val tag = body[pos].toInt() and 0xFF
            val len = Le.getU16(body, pos + 1)
            require(body.size - pos - 3 >= len) { "truncated TLV value" }
            items += TlvItem(tag, body.copyOfRange(pos + 3, pos + 3 + len))
            pos += 3 + len
        }
        return items
    }
}

internal object Le {
    fun u16(v: Int) = byteArrayOf((v and 0xFF).toByte(), ((v shr 8) and 0xFF).toByte())
    fun u32(v: Long) = byteArrayOf(
        (v and 0xFF).toByte(), ((v shr 8) and 0xFF).toByte(),
        ((v shr 16) and 0xFF).toByte(), ((v shr 24) and 0xFF).toByte(),
    )
    fun u64(v: Long) = byteArrayOf(
        (v and 0xFF).toByte(), ((v shr 8) and 0xFF).toByte(),
        ((v shr 16) and 0xFF).toByte(), ((v shr 24) and 0xFF).toByte(),
        ((v shr 32) and 0xFF).toByte(), ((v shr 40) and 0xFF).toByte(),
        ((v shr 48) and 0xFF).toByte(), ((v shr 56) and 0xFF).toByte(),
    )
    fun i32(v: Int) = u32(v.toLong() and 0xFFFFFFFFL)
    fun getU16(b: ByteArray, off: Int) = (b[off].toInt() and 0xFF) or ((b[off + 1].toInt() and 0xFF) shl 8)
    fun getU32(b: ByteArray, off: Int): Long =
        (b[off].toLong() and 0xFF) or ((b[off + 1].toLong() and 0xFF) shl 8) or
            ((b[off + 2].toLong() and 0xFF) shl 16) or ((b[off + 3].toLong() and 0xFF) shl 24)
    fun getU64(b: ByteArray, off: Int): Long =
        (b[off].toLong() and 0xFF) or ((b[off + 1].toLong() and 0xFF) shl 8) or
            ((b[off + 2].toLong() and 0xFF) shl 16) or ((b[off + 3].toLong() and 0xFF) shl 24) or
            ((b[off + 4].toLong() and 0xFF) shl 32) or ((b[off + 5].toLong() and 0xFF) shl 40) or
            ((b[off + 6].toLong() and 0xFF) shl 48) or ((b[off + 7].toLong() and 0xFF) shl 56)
    fun getI32(b: ByteArray, off: Int): Int = getU32(b, off).toInt()
}
