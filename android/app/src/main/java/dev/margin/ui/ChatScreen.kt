// SPDX-License-Identifier: MIT
package dev.margin.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import dev.margin.ble.MarginBleClient
import dev.margin.chat.ChatViewModel
import dev.margin.protocol.Role

/**
 * Phone-side mirror of the conversation, with a text box for typing on the phone.
 * TODO(M4): device picker/pairing screen, LLM settings screen, Markdown rendering, error states.
 */
@Composable
fun ChatScreen(vm: ChatViewModel, bleState: MarginBleClient.State) {
    val transcript by vm.transcript.collectAsState()
    var input by remember { mutableStateOf("") }
    Column(Modifier.fillMaxSize().padding(16.dp)) {
        Text("margin · reader ${bleState.name.lowercase()}", style = MaterialTheme.typography.labelMedium)
        LazyColumn(Modifier.weight(1f).fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            items(transcript) { line ->
                Text(
                    text = (if (line.role == Role.USER) "You: " else "Agent: ") + line.text,
                    style = MaterialTheme.typography.bodyLarge,
                )
            }
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedTextField(value = input, onValueChange = { input = it }, modifier = Modifier.weight(1f))
            Button(onClick = { vm.ask(input); input = "" }) { Text("Send") }
        }
    }
}
