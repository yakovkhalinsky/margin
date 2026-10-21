// SPDX-License-Identifier: MIT
// Activity to configure which apps can send notifications to the reader.
package dev.margin.companion

import android.content.Context
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
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

class AppAllowlistActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        setContent {
            MaterialTheme {
                AppAllowlistScreen()
            }
        }
    }
}

data class AppInfo(
    val packageName: String,
    val label: String,
    var enabled: Boolean
)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AppAllowlistScreen() {
    val context = LocalContext.current
    var apps by remember { mutableStateOf<List<AppInfo>>(emptyList()) }
    var searchQuery by remember { mutableStateOf("") }

    LaunchedEffect(Unit) {
        val prefs = context.getSharedPreferences(MarginNotificationListener.PREFS_NAME, Context.MODE_PRIVATE)
        val allowlist = prefs.getStringSet(MarginNotificationListener.KEY_ALLOWLIST,
            MarginNotificationListener.defaultAllowlist)?.toMutableSet()
            ?: MarginNotificationListener.defaultAllowlist.toMutableSet()

        val pm = context.packageManager
        val installedApps = pm.getInstalledApplications(PackageManager.GET_META_DATA)
            .filter { it.flags and ApplicationInfo.FLAG_SYSTEM == 0 || allowlist.contains(it.packageName) }
            .map { appInfo ->
                AppInfo(
                    packageName = appInfo.packageName,
                    label = pm.getApplicationLabel(appInfo).toString(),
                    enabled = allowlist.contains(appInfo.packageName)
                )
            }
            .sortedWith(compareByDescending<AppInfo> { it.enabled }.thenBy { it.label })

        apps = installedApps
    }

    Scaffold(
        topBar = {
            TopAppBar(title = { Text("App Filter") })
        }
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
        ) {
            OutlinedTextField(
                value = searchQuery,
                onValueChange = { searchQuery = it },
                label = { Text("Search apps") },
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
                singleLine = true
            )

            val filteredApps = apps.filter {
                it.label.contains(searchQuery, ignoreCase = true) ||
                        it.packageName.contains(searchQuery, ignoreCase = true)
            }

            LazyColumn(
                modifier = Modifier.fillMaxSize(),
                contentPadding = PaddingValues(horizontal = 16.dp)
            ) {
                items(filteredApps, key = { it.packageName }) { app ->
                    AppListItem(
                        app = app,
                        onToggle = { enabled ->
                            val newApps = apps.map {
                                if (it.packageName == app.packageName) it.copy(enabled = enabled) else it
                            }
                            apps = newApps
                            saveAllowlist(context, newApps)
                        }
                    )
                }
            }
        }
    }
}

@Composable
fun AppListItem(
    app: AppInfo,
    onToggle: (Boolean) -> Unit
) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(12.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = app.label,
                    style = MaterialTheme.typography.bodyLarge
                )
                Text(
                    text = app.packageName,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }
            Switch(
                checked = app.enabled,
                onCheckedChange = onToggle
            )
        }
    }
}

private fun saveAllowlist(context: Context, apps: List<AppInfo>) {
    val allowlist = apps.filter { it.enabled }.map { it.packageName }.toSet()
    context.getSharedPreferences(MarginNotificationListener.PREFS_NAME, Context.MODE_PRIVATE)
        .edit()
        .putStringSet(MarginNotificationListener.KEY_ALLOWLIST, allowlist)
        .apply()
}
