// SPDX-License-Identifier: MIT
package dev.margin

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import dev.margin.ble.MarginBleClient
import dev.margin.chat.ChatViewModel
import dev.margin.llm.LlmConfig
import dev.margin.llm.OllamaProvider
import dev.margin.ui.ChatScreen

class MainActivity : ComponentActivity() {
    private lateinit var ble: MarginBleClient

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        ble = MarginBleClient(applicationContext)
        // TODO(M4): settings screen; choose OpenAiCompatibleProvider or OllamaProvider and store the key securely.
        val llm = OllamaProvider(LlmConfig(baseUrl = "http://192.168.1.20:11434", model = "llama3.2"))
        val vm = ViewModelProvider(this, object : ViewModelProvider.Factory {
            @Suppress("UNCHECKED_CAST")
            override fun <T : ViewModel> create(modelClass: Class<T>): T = ChatViewModel(ble, llm) as T
        })[ChatViewModel::class.java]
        // TODO(M2): request BLUETOOTH_SCAN/CONNECT at runtime, scan for Protocol.SERVICE_UUID, connect, bond.
        setContent {
            MaterialTheme {
                val state by ble.state.collectAsState()
                ChatScreen(vm, state)
            }
        }
    }
}
