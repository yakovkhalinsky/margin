// SPDX-License-Identifier: MIT
package dev.margin.llm

import kotlinx.coroutines.flow.Flow

data class ChatMessage(val role: String, val content: String) // "system" | "user" | "assistant" | "tool"

sealed interface LlmEvent {
    data class Delta(val text: String) : LlmEvent
    /** Optional status for the e-ink "thinking" line, e.g. "Searching the web". */
    data class Status(val label: String) : LlmEvent
    data class Done(val finishReason: String?) : LlmEvent
    data class Failed(val message: String) : LlmEvent
}

/**
 * A streaming chat backend. Implementations must be cancellable (cancel the collecting coroutine
 * to stop generation, which the device triggers with CHAT_CANCEL).
 *
 * TODO(M4): tool calling (function schemas, tool results) once the agent loop exists.
 */
interface LlmProvider {
    val id: String
    fun stream(messages: List<ChatMessage>): Flow<LlmEvent>
}

data class LlmConfig(
    val baseUrl: String,          // e.g. https://api.openai.com/v1 or http://192.168.1.20:11434
    val model: String,
    val apiKey: String? = null,   // kept on the phone only (EncryptedSharedPreferences, TODO); never sent over BLE
    val temperature: Double? = null,
)
