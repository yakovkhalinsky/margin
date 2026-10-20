// SPDX-License-Identifier: MIT
package dev.margin.bible

import dev.margin.protocol.BibleHighlightColor
import dev.margin.protocol.BibleNoteType

/**
 * A Bible study note or highlight, synced between device and app.
 *
 * STATUS: stub. Data model for Bible notes sync (Phase 2).
 */
data class BibleNote(
    val ref: String,              // OSIS reference, e.g. "JHN.3.16"
    val type: BibleNoteType,
    val color: BibleHighlightColor? = null,  // For highlights
    val text: String? = null,     // For notes
    val timestamp: Long,          // Unix timestamp (seconds)
    val seq: Long,                // Monotonic sequence for sync
    val deleted: Boolean = false, // Soft-delete tombstone
) {
    val bookId: String get() = ref.substringBefore('.')
    val chapter: Int get() = ref.substringAfter('.').substringBefore('.').toIntOrNull() ?: 0
    val verse: Int get() = ref.substringAfterLast('.').substringBefore('-').toIntOrNull() ?: 0

    companion object {
        fun highlight(ref: String, color: BibleHighlightColor, timestamp: Long, seq: Long) =
            BibleNote(ref, BibleNoteType.HIGHLIGHT, color = color, timestamp = timestamp, seq = seq)

        fun note(ref: String, text: String, timestamp: Long, seq: Long) =
            BibleNote(ref, BibleNoteType.NOTE, text = text, timestamp = timestamp, seq = seq)
    }
}

/**
 * Parsed Bible reference.
 */
data class BibleReference(
    val book: String,      // OSIS book ID, e.g. "JHN"
    val chapter: Int,      // 1-based
    val verse: Int,        // 1-based, 0 means whole chapter
    val endVerse: Int = 0, // 0 if single verse, else end of range
) {
    fun toOsis(): String = when {
        verse == 0 -> "$book.$chapter"
        endVerse > 0 -> "$book.$chapter.$verse-$endVerse"
        else -> "$book.$chapter.$verse"
    }

    companion object {
        private val osisPattern = Regex("""([A-Z0-9]{2,3})\.(\d+)(?:\.(\d+)(?:-(\d+))?)?""")

        fun parse(osis: String): BibleReference? {
            val match = osisPattern.matchEntire(osis) ?: return null
            val (book, ch, v, ev) = match.destructured
            return BibleReference(
                book = book,
                chapter = ch.toIntOrNull() ?: return null,
                verse = v.toIntOrNull() ?: 0,
                endVerse = ev.toIntOrNull() ?: 0
            )
        }
    }
}
