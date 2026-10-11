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
 * Streams from a local Ollama server's native `POST {baseUrl}/api/chat` (newline-delimited JSON).
 * Point baseUrl at the machine running Ollama on your LAN, e.g. http://192.168.1.20:11434
 * (Ollama must listen on 0.0.0.0, and cleartext HTTP must be allowed for that host, see
 * res/xml/network_security_config.xml TODO).
 *
 * STATUS: written against Ollama's documented API, not yet run against a live server.
 */
class OllamaProvider(private val cfg: LlmConfig) : LlmProvider {
    override val id = "ollama"

    override fun stream(messages: List<ChatMessage>): Flow<LlmEvent> = flow {
        val conn = (URL(cfg.baseUrl.trimEnd('/') + "/api/chat").openConnection() as HttpURLConnection).apply {
            requestMethod = "POST"
            doOutput = true
            setRequestProperty("Content-Type", "application/json")
        }
        val req = JSONObject()
            .put("model", cfg.model)
            .put("stream", true)
            .put("messages", JSONArray(messages.map { JSONObject().put("role", it.role).put("content", it.content) }))
        cfg.temperature?.let { req.put("options", JSONObject().put("temperature", it)) }
        try {
            conn.outputStream.use { it.write(req.toString().toByteArray()) }
            if (conn.responseCode !in 200..299) {
                emit(LlmEvent.Failed("HTTP ${conn.responseCode}"))
                return@flow
            }
            conn.inputStream.bufferedReader().useLines { lines ->
                for (line in lines) {
                    if (line.isBlank()) continue
                    val obj = JSONObject(line)
                    obj.optJSONObject("message")?.optString("content", "")?.let { if (it.isNotEmpty()) emit(LlmEvent.Delta(it)) }
                    if (obj.optBoolean("done", false)) {
                        emit(LlmEvent.Done(obj.optString("done_reason", "stop")))
                        break
                    }
                }
            }
        } catch (e: Exception) {
            emit(LlmEvent.Failed(e.message ?: e.javaClass.simpleName))
        } finally {
            conn.disconnect()
        }
    }.flowOn(Dispatchers.IO)
}
