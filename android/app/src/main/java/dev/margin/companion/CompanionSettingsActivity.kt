// SPDX-License-Identifier: MIT
// Settings screen for companion app configuration.
package dev.margin.companion

import android.Manifest
import android.app.Activity
import android.bluetooth.BluetoothDevice
import android.companion.AssociationRequest
import android.companion.BluetoothDeviceFilter
import android.companion.CompanionDeviceManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.IntentSender
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.util.Log
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.*
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import dev.margin.ble.MarginBleClient
import dev.margin.protocol.CompanionMessages
import dev.margin.protocol.MsgType
import dev.margin.protocol.NotifPushData

class CompanionSettingsActivity : ComponentActivity() {
    private lateinit var deviceAssociationLauncher: ActivityResultLauncher<IntentSenderRequest>
    private lateinit var permissionLauncher: ActivityResultLauncher<Array<String>>

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        deviceAssociationLauncher = registerForActivityResult(
            ActivityResultContracts.StartIntentSenderForResult()
        ) { result ->
            if (result.resultCode == Activity.RESULT_OK) {
                val device: BluetoothDevice? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    result.data?.getParcelableExtra(CompanionDeviceManager.EXTRA_DEVICE, BluetoothDevice::class.java)
                } else {
                    @Suppress("DEPRECATION")
                    result.data?.getParcelableExtra(CompanionDeviceManager.EXTRA_DEVICE)
                }
                device?.let {
                    Log.d(TAG, "Associated with device: ${it.address}")
                    connectToDevice(it)
                }
            }
        }

        permissionLauncher = registerForActivityResult(
            ActivityResultContracts.RequestMultiplePermissions()
        ) { permissions ->
            val allGranted = permissions.all { it.value }
            if (allGranted) {
                Log.d(TAG, "All permissions granted")
            } else {
                Log.w(TAG, "Some permissions denied")
            }
        }

        setContent {
            MaterialTheme {
                CompanionSettingsScreen(
                    onPairDevice = { startDeviceAssociation() },
                    onRequestPermissions = { requestPermissions() },
                    onStartService = { CompanionService.start(this) },
                    onStopService = { CompanionService.stop(this) },
                    onSendTestNotification = { sendTestNotification() }
                )
            }
        }
    }

    private fun sendTestNotification() {
        val bleClient = MarginBleClient.getInstance(this)
        if (!bleClient.isConnected()) {
            Toast.makeText(this, "Not connected to device", Toast.LENGTH_SHORT).show()
            return
        }

        val timestamp = System.currentTimeMillis()
        val notif = NotifPushData(
            notifKey = "dev.margin.companion|test_$timestamp",
            notifId = timestamp % 100000,
            app = "dev.margin.companion",
            appName = "margin Companion",
            title = "Test Notification",
            body = "This is a test notification sent at ${java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.getDefault()).format(java.util.Date(timestamp))}",
            timestamp = timestamp
        )

        val body = CompanionMessages.encodeNotifPush(notif)
        val sent = bleClient.send(MsgType.NOTIF_PUSH, body)
        if (sent) {
            Toast.makeText(this, "Test notification sent", Toast.LENGTH_SHORT).show()
            Log.d(TAG, "Sent test notification: id=${notif.notifId}")
        } else {
            Toast.makeText(this, "Failed to send", Toast.LENGTH_SHORT).show()
        }
    }

    private fun requestPermissions() {
        val permissions = mutableListOf<String>()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT)
                != PackageManager.PERMISSION_GRANTED) {
                permissions.add(Manifest.permission.BLUETOOTH_CONNECT)
            }
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_SCAN)
                != PackageManager.PERMISSION_GRANTED) {
                permissions.add(Manifest.permission.BLUETOOTH_SCAN)
            }
        }

        if (ContextCompat.checkSelfPermission(this, Manifest.permission.READ_CALENDAR)
            != PackageManager.PERMISSION_GRANTED) {
            permissions.add(Manifest.permission.READ_CALENDAR)
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS)
                != PackageManager.PERMISSION_GRANTED) {
                permissions.add(Manifest.permission.POST_NOTIFICATIONS)
            }
        }

        if (permissions.isNotEmpty()) {
            permissionLauncher.launch(permissions.toTypedArray())
        }
    }

    private fun startDeviceAssociation() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT)
                != PackageManager.PERMISSION_GRANTED) {
                requestPermissions()
                return
            }
        }

        val deviceFilter = BluetoothDeviceFilter.Builder()
            .setNamePattern(java.util.regex.Pattern.compile("margin.*"))
            .build()

        val request = AssociationRequest.Builder()
            .addDeviceFilter(deviceFilter)
            .setSingleDevice(true)
            .build()

        val cdm = getSystemService(Context.COMPANION_DEVICE_SERVICE) as CompanionDeviceManager

        cdm.associate(request, object : CompanionDeviceManager.Callback() {
            @Deprecated("Deprecated in Java")
            override fun onDeviceFound(chooserLauncher: IntentSender) {
                deviceAssociationLauncher.launch(
                    IntentSenderRequest.Builder(chooserLauncher).build()
                )
            }

            override fun onFailure(error: CharSequence?) {
                Log.e(TAG, "Association failed: $error")
                Toast.makeText(this@CompanionSettingsActivity, "Pairing failed: $error", Toast.LENGTH_SHORT).show()
            }
        }, null)
    }

    private fun connectToDevice(device: BluetoothDevice) {
        val intent = Intent(this, CompanionService::class.java).apply {
            action = CompanionService.ACTION_CONNECT
            putExtra(CompanionService.EXTRA_DEVICE, device)
        }
        ContextCompat.startForegroundService(this, intent)
    }

    companion object {
        private const val TAG = "CompanionSettings"
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CompanionSettingsScreen(
    onPairDevice: () -> Unit,
    onRequestPermissions: () -> Unit,
    onStartService: () -> Unit,
    onStopService: () -> Unit,
    onSendTestNotification: () -> Unit
) {
    val context = LocalContext.current

    var notificationsEnabled by remember { mutableStateOf(true) }
    var messagesEnabled by remember { mutableStateOf(true) }
    var calendarEnabled by remember { mutableStateOf(true) }

    LaunchedEffect(Unit) {
        val prefs = context.getSharedPreferences(MarginNotificationListener.PREFS_NAME, Context.MODE_PRIVATE)
        notificationsEnabled = prefs.getBoolean("sync_notifications", true)
        messagesEnabled = prefs.getBoolean("sync_messages", true)
        calendarEnabled = prefs.getBoolean("sync_calendar", true)
    }

    Scaffold(
        topBar = {
            TopAppBar(title = { Text("margin Companion Settings") })
        }
    ) { paddingValues ->
        LazyColumn(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .padding(16.dp)
        ) {
            item {
                Text(
                    "Connection",
                    style = MaterialTheme.typography.titleMedium,
                    modifier = Modifier.padding(vertical = 8.dp)
                )
            }

            item {
                Card(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 4.dp)
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Button(
                            onClick = onPairDevice,
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text("Pair margin Reader")
                        }

                        Spacer(modifier = Modifier.height(8.dp))

                        Row(
                            modifier = Modifier.fillMaxWidth(),
                            horizontalArrangement = Arrangement.SpaceEvenly
                        ) {
                            OutlinedButton(onClick = onStartService) {
                                Text("Start Service")
                            }
                            OutlinedButton(onClick = onStopService) {
                                Text("Stop Service")
                            }
                        }
                    }
                }
            }

            item {
                Spacer(modifier = Modifier.height(16.dp))
                Text(
                    "Sync Settings",
                    style = MaterialTheme.typography.titleMedium,
                    modifier = Modifier.padding(vertical = 8.dp)
                )
            }

            item {
                Card(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 4.dp)
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        SwitchRow(
                            label = "Sync Notifications",
                            checked = notificationsEnabled,
                            onCheckedChange = {
                                notificationsEnabled = it
                                savePref(context, "sync_notifications", it)
                            }
                        )

                        SwitchRow(
                            label = "Sync Messages",
                            checked = messagesEnabled,
                            onCheckedChange = {
                                messagesEnabled = it
                                savePref(context, "sync_messages", it)
                            }
                        )

                        SwitchRow(
                            label = "Sync Calendar",
                            checked = calendarEnabled,
                            onCheckedChange = {
                                calendarEnabled = it
                                savePref(context, "sync_calendar", it)
                            }
                        )
                    }
                }
            }

            item {
                Spacer(modifier = Modifier.height(16.dp))
                Text(
                    "Permissions",
                    style = MaterialTheme.typography.titleMedium,
                    modifier = Modifier.padding(vertical = 8.dp)
                )
            }

            item {
                Card(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 4.dp)
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Button(
                            onClick = onRequestPermissions,
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text("Grant Permissions")
                        }

                        Spacer(modifier = Modifier.height(8.dp))

                        OutlinedButton(
                            onClick = {
                                val intent = Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS)
                                context.startActivity(intent)
                            },
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text("Notification Access Settings")
                        }
                    }
                }
            }

            item {
                Spacer(modifier = Modifier.height(16.dp))
                Text(
                    "App Filter",
                    style = MaterialTheme.typography.titleMedium,
                    modifier = Modifier.padding(vertical = 8.dp)
                )
                Text(
                    "Select which apps can send notifications to the reader",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }

            item {
                Card(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 4.dp)
                        .clickable {
                            val intent = Intent(context, AppAllowlistActivity::class.java)
                            context.startActivity(intent)
                        }
                ) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(16.dp),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text("Configure App Filter")
                        Text("→", style = MaterialTheme.typography.titleMedium)
                    }
                }
            }

            item {
                Spacer(modifier = Modifier.height(16.dp))
                Text(
                    "Testing",
                    style = MaterialTheme.typography.titleMedium,
                    modifier = Modifier.padding(vertical = 8.dp)
                )
                Text(
                    "Send a test notification to verify the connection",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }

            item {
                Card(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 4.dp)
                ) {
                    Column(modifier = Modifier.padding(16.dp)) {
                        Button(
                            onClick = onSendTestNotification,
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Text("Send Test Notification")
                        }
                    }
                }
            }
        }
    }
}

@Composable
fun SwitchRow(
    label: String,
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 8.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(label)
        Switch(checked = checked, onCheckedChange = onCheckedChange)
    }
}

private fun savePref(context: Context, key: String, value: Boolean) {
    context.getSharedPreferences(MarginNotificationListener.PREFS_NAME, Context.MODE_PRIVATE)
        .edit()
        .putBoolean(key, value)
        .apply()
}
