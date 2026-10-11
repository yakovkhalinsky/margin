// SPDX-License-Identifier: MIT
package dev.margin.llm

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.flowOn
import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

/**
 * Streams from any OpenAI-compatible `POST {baseUrl}/chat/completions` endpoint (OpenAI, OpenRouter,
 * LM Studio, llama.cpp server, vLLM, Ollama's /v1 shim...) using Server-Sent Events.
 *
 * STATUS: written against the documented SSE format, not yet run against a live server.
 * TODO(M4): OkHttp + proper cancellation of the socket, retries, tool calls, error bodies.
 */
class OpenAiCompatibleProvider(private val cfg: LlmConfig) : LlmProvider {
    override val id = "openai-compatible"

    override fun stream(messages: List<ChatMessage>): Flow<LlmEvent> = flow {
        val conn = (URL(cfg.baseUrl.trimEnd('/') + "/chat/completions").openConnection() as HttpURLConnection).apply {
            requestMethod = "POST"
            doOutput = true
            setRequestProperty("Content-Type", "application/json")
            setRequestProperty("Accept", "text/event-stream")
            cfg.apiKey?.let { setRequestProperty("Authorization", "Bearer $it") }
        }
        val req = JSONObject()
            .put("model", cfg.model)
            .put("stream", true)
            .put("messages", JSONArray(messages.map { JSONObject().put("role", it.role).put("content", it.content) }))
        cfg.temperature?.let { req.put("temperature", it) }
        try {
            conn.outputStream.use { it.write(req.toString().toByteArray()) }
            if (conn.responseCode !in 200..299) {
                emit(LlmEvent.Failed("HTTP ${conn.responseCode}"))
                return@flow
            }
            var finish: String? = null
            conn.inputStream.bufferedReader().useLines { lines ->
                for (line in lines) {
                    if (!line.startsWith("data:")) continue
                    val data = line.removePrefix("data:").trim()
                    if (data == "[DONE]") break
                    val choice = JSONObject(data).optJSONArray("choices")?.optJSONObject(0) ?: continue
                    val text = choice.optJSONObject("delta")?.optString("content", "") ?: ""
                    if (text.isNotEmpty()) emit(LlmEvent.Delta(text))
                    if (!choice.isNull("finish_reason")) finish = choice.optString("finish_reason")
                }
            }
            emit(LlmEvent.Done(finish))
        } catch (e: Exception) {
            emit(LlmEvent.Failed(e.message ?: e.javaClass.simpleName))
        } finally {
            conn.disconnect()
        }
    }.flowOn(Dispatchers.IO)
}
