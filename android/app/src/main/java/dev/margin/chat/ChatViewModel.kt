// SPDX-License-Identifier: MIT
package dev.margin.chat

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import dev.margin.ble.MarginBleClient
import dev.margin.llm.ChatMessage
import dev.margin.llm.LlmEvent
import dev.margin.llm.LlmProvider
import dev.margin.protocol.FinishReason
import dev.margin.protocol.Messages
import dev.margin.protocol.MsgType
import dev.margin.protocol.PassageIntent
import dev.margin.protocol.Role
import dev.margin.protocol.ThinkingState
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch

/**
 * The phone-side "brain": owns history, runs the LLM, mirrors replies to the reader.
 *
 * STATUS: scaffold. TODO(M4): persistence (Room), system prompt + settings screen, tool calls,
 * a proper outbound queue to the device (BLE send() refuses while a message is in flight), and
 * coalescing deltas into ~200-byte CHAT_DELTA messages so BLE and e-ink are not flooded.
 */
class ChatViewModel(
    private val ble: MarginBleClient,
    private val llm: LlmProvider,
) : ViewModel() {
    data class Line(val role: Role, val text: String)

    private val _transcript = MutableStateFlow<List<Line>>(emptyList())
    val transcript: StateFlow<List<Line>> = _transcript

    private var convId = (System.currentTimeMillis() / 1000) and 0xFFFFFFFFL
    private var turnSeq = 0L
    private var running: Job? = null
    private var columns = 40

    init {
        viewModelScope.launch {
            ble.messages.collect { m ->
                when (MsgType.of(m.type)) {
                    MsgType.HELLO -> columns = Messages.decodeHello(m.body).columns.takeIf { it > 0 } ?: columns
                    MsgType.USER_MESSAGE -> ask(Messages.decodeUserMessage(m.body).text, fromDevice = true)
                    MsgType.PASSAGE_SHARE -> ask(passagePrompt(Messages.decodePassageShare(m.body)), fromDevice = true)
                    MsgType.CHAT_CANCEL -> running?.cancel()
                    else -> {} // TODO(M6/M7): FILE_ACCEPT/FILE_RESULT, SYNC_RECORDS/SYNC_DONE
                }
            }
        }
    }

    fun ask(text: String, fromDevice: Boolean = false) {
        if (text.isBlank()) return
        running?.cancel()
        _transcript.value = _transcript.value + Line(Role.USER, text)
        if (!fromDevice) {
            // Echo phone-typed prompts to the reader so both transcripts match.
            ble.send(MsgType.CHAT_DELTA, Messages.chatDelta(convId, ++turnSeq, Role.USER, TextPrep.forEink(text, columns)))
        }
        val turn = ++turnSeq
        running = viewModelScope.launch {
            ble.send(MsgType.THINKING_STATUS, Messages.thinking(convId, ThinkingState.THINKING, "Thinking"))
            val history = _transcript.value.map { ChatMessage(if (it.role == Role.USER) "user" else "assistant", it.text) }
            val reply = StringBuilder()
            var finish = FinishReason.STOP
            llm.stream(history).collect { ev ->
                when (ev) {
                    is LlmEvent.Delta -> {
                        reply.append(ev.text)
                        ble.send(MsgType.CHAT_DELTA, Messages.chatDelta(convId, turn, Role.ASSISTANT, TextPrep.forEink(ev.text, columns)))
                    }
                    is LlmEvent.Status -> ble.send(MsgType.THINKING_STATUS, Messages.thinking(convId, ThinkingState.TOOL_RUNNING, ev.label))
                    is LlmEvent.Done -> finish = if (ev.finishReason == "length") FinishReason.LENGTH else FinishReason.STOP
                    is LlmEvent.Failed -> { finish = FinishReason.ERROR; reply.append("\n[error: ${ev.message}]") }
                }
            }
            _transcript.value = _transcript.value + Line(Role.ASSISTANT, reply.toString())
            ble.send(MsgType.CHAT_DONE, Messages.chatDone(convId, turn, finish))
            // TODO(M4): let the model propose follow-ups; static examples for now.
            ble.send(MsgType.QUICK_REPLIES, Messages.quickReplies(convId, turn, listOf("Tell me more", "Simpler", "Thanks")))
        }
    }

    private fun passagePrompt(p: Messages.PassageShare): String {
        val where = listOfNotNull(p.bookTitle, p.chapterTitle).joinToString(", ")
        val verb = when (p.intent) {
            PassageIntent.EXPLAIN -> "Explain this passage"
            PassageIntent.SUMMARISE_SELECTION -> "Summarise this passage"
            PassageIntent.SUMMARISE_CHAPTER -> "Summarise this chapter"
            PassageIntent.DEFINE -> "Define the key terms in this passage"
            PassageIntent.TRANSLATE -> "Translate this passage into English"
            PassageIntent.ASK -> "Help me with this passage"
        }
        return "$verb${if (where.isNotEmpty()) " (from $where)" else ""}:\n\n${p.text}"
    }
}
