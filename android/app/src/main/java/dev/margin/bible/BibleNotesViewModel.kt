// SPDX-License-Identifier: MIT
package dev.margin.bible

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import dev.margin.ble.MarginBleClient
import dev.margin.protocol.BibleHighlightColor
import dev.margin.protocol.BibleNoteType
import dev.margin.protocol.MsgType
import dev.margin.protocol.Tag
import dev.margin.protocol.Tlv
import dev.margin.protocol.TlvWriter
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

/**
 * Manages Bible notes and highlights, syncing between device and app.
 *
 * STATUS: stub. Real implementation needs:
 * - Room database for persistent storage
 * - Background sync service
 * - Conflict resolution for bidirectional sync
 * - Proper sequence number management
 *
 * @see docs/bible-mode.md for the design.
 */
class BibleNotesViewModel(
    private val ble: MarginBleClient,
) : ViewModel() {

    private val _notes = MutableStateFlow<List<BibleNote>>(emptyList())
    val notes: StateFlow<List<BibleNote>> = _notes

    private val _syncStatus = MutableStateFlow<SyncStatus>(SyncStatus.Idle)
    val syncStatus: StateFlow<SyncStatus> = _syncStatus

    private var localSeq = 0L
    private var lastSyncSeq = 0L

    init {
        viewModelScope.launch {
            ble.messages.collect { m ->
                when (MsgType.of(m.type)) {
                    MsgType.BIBLE_CONTEXT -> handleBibleContext(m.body)
                    MsgType.BIBLE_NOTE_RECORDS -> handleNoteRecords(m.body)
                    MsgType.BIBLE_NOTE_SYNC_DONE -> handleSyncDone(m.body)
                    MsgType.BIBLE_NOTE_ACK -> handleNoteAck(m.body)
                    else -> {}
                }
            }
        }
    }

    /** Request notes from device changed since last sync. */
    fun syncNotes() {
        _syncStatus.value = SyncStatus.Syncing
        val body = TlvWriter().u32(Tag.SEQ, lastSyncSeq).toByteArray()
        ble.send(MsgType.BIBLE_NOTE_SYNC_REQ, body)
    }

    /** Add a highlight on the device. */
    fun addHighlight(ref: String, color: BibleHighlightColor) {
        val now = System.currentTimeMillis() / 1000
        val seq = ++localSeq
        val note = BibleNote.highlight(ref, color, now, seq)
        _notes.value = _notes.value + note
        pushNote(note)
    }

    /** Add a note on the device. */
    fun addNote(ref: String, text: String) {
        val now = System.currentTimeMillis() / 1000
        val seq = ++localSeq
        val note = BibleNote.note(ref, text, now, seq)
        _notes.value = _notes.value + note
        pushNote(note)
    }

    /** Delete a note (soft delete for sync). */
    fun deleteNote(ref: String) {
        val existing = _notes.value.find { it.ref == ref && !it.deleted } ?: return
        val now = System.currentTimeMillis() / 1000
        val seq = ++localSeq
        val deleted = existing.copy(timestamp = now, seq = seq, deleted = true)
        _notes.value = _notes.value.map { if (it.ref == ref) deleted else it }
        pushNote(deleted)
    }

    /** Get notes for a specific verse. */
    fun notesForVerse(ref: String): List<BibleNote> =
        _notes.value.filter { it.ref == ref && !it.deleted }

    /** Get all highlights (for rendering in Bible view). */
    fun highlights(): List<BibleNote> =
        _notes.value.filter { it.type == BibleNoteType.HIGHLIGHT && !it.deleted }

    private fun pushNote(note: BibleNote) {
        val body = TlvWriter()
            .str(Tag.BIBLE_REF, note.ref)
            .u8(Tag.BIBLE_NOTE_TYPE, note.type.code)
            .apply {
                if (note.color != null) u8(Tag.BIBLE_HIGHLIGHT_COLOR, note.color.code)
                if (note.text != null) str(Tag.NOTE, note.text)
            }
            .u32(Tag.TIMESTAMP, note.timestamp)
            .u32(Tag.SEQ, note.seq)
            .apply { if (note.deleted) u8(Tag.DELETED, 1) }
            .toByteArray()
        ble.send(MsgType.BIBLE_NOTE_PUSH, body)
    }

    private fun handleBibleContext(body: ByteArray) {
        // TODO(M4): Use this to build "ask about this passage" prompts
        // The device sends BIBLE_CONTEXT when user triggers "Ask about this verse"
        var ref = ""; var bookName = ""; var chapter = 0; var verse = 0; var text = ""
        for (it in Tlv.parse(body)) when (it.tag) {
            Tag.BIBLE_REF -> ref = it.str()
            Tag.BIBLE_BOOK_NAME -> bookName = it.str()
            Tag.BIBLE_CHAPTER -> chapter = it.u16()
            Tag.BIBLE_VERSE -> verse = it.u16()
            Tag.TEXT -> text = it.str()
        }
        // TODO: Forward to ChatViewModel to build a study chat prompt
    }

    private fun handleNoteRecords(body: ByteArray) {
        // Parse incoming note records from device
        val newNotes = mutableListOf<BibleNote>()
        // TODO: Parse nested BIBLE_NOTE_RECORD TLVs
        // For now, just merge any new notes
        if (newNotes.isNotEmpty()) {
            _notes.value = mergeNotes(_notes.value, newNotes)
        }
    }

    private fun handleSyncDone(body: ByteArray) {
        var seq = 0L
        for (it in Tlv.parse(body)) when (it.tag) {
            Tag.SEQ -> seq = it.u32()
        }
        lastSyncSeq = seq
        _syncStatus.value = SyncStatus.Idle
    }

    private fun handleNoteAck(body: ByteArray) {
        var seq = 0L
        for (it in Tlv.parse(body)) when (it.tag) {
            Tag.SEQ -> seq = it.u32()
        }
        // Mark notes up to this seq as synced
        // TODO: Track pending sync status per note
    }

    private fun mergeNotes(local: List<BibleNote>, remote: List<BibleNote>): List<BibleNote> {
        // Simple merge: remote wins if seq is higher
        val byRef = local.associateBy { it.ref }.toMutableMap()
        for (note in remote) {
            val existing = byRef[note.ref]
            if (existing == null || note.seq > existing.seq) {
                byRef[note.ref] = note
            }
        }
        return byRef.values.toList()
    }

    sealed class SyncStatus {
        object Idle : SyncStatus()
        object Syncing : SyncStatus()
        data class Error(val message: String) : SyncStatus()
    }
}
