package com.example.lorabridge.presentation.chat

import android.Manifest
import android.content.Intent
import android.os.Build
import android.widget.Toast
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.WindowInsetsSides
import androidx.compose.foundation.layout.ime
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.only
import androidx.compose.foundation.layout.union
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.LocationOff
import androidx.compose.material.icons.filled.LocationOn
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CenterAlignedTopAppBar
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledIconButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.core.net.toUri
import androidx.hilt.lifecycle.viewmodel.compose.hiltViewModel
import com.example.lorabridge.domain.model.DeviceInfo
import com.example.lorabridge.domain.model.Message
import com.example.lorabridge.presentation.components.ConnectionDialog
import com.example.lorabridge.presentation.components.MessageBubble
import com.google.accompanist.permissions.ExperimentalPermissionsApi
import com.google.accompanist.permissions.rememberMultiplePermissionsState

/**
 * Main chat screen with Compose
 * @see UC-7.1: Request BLE Permissions
 * @see UC-7.2: Request Location Permissions
 * @see UC-6.3: Auto-Scroll Chat
 * @see UC-8.1: Dismiss Keyboard on Send
 */
@OptIn(ExperimentalPermissionsApi::class, ExperimentalMaterial3Api::class)
@Composable
fun ChatScreen(
    viewModel: ChatViewModel = hiltViewModel()
) {
    val uiState by viewModel.uiState.collectAsState()
    val context = LocalContext.current
    val listState = rememberLazyListState()

    // Request permissions
    val permissionsState = rememberMultiplePermissionsState(
        permissions = buildList {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                add(Manifest.permission.BLUETOOTH_SCAN)
                add(Manifest.permission.BLUETOOTH_CONNECT)
            }
            add(Manifest.permission.ACCESS_FINE_LOCATION)
        }
    )

    // Request permissions on startup
    LaunchedEffect(Unit) {
        if (!permissionsState.allPermissionsGranted) {
            permissionsState.launchMultiplePermissionRequest()
        } else {
            viewModel.startBleScan()
            viewModel.updateGps()
        }
    }

    // Handle permission result
    LaunchedEffect(permissionsState.allPermissionsGranted) {
        if (permissionsState.allPermissionsGranted) {
            viewModel.startBleScan()
            viewModel.updateGps()
        }
    }

    // Show toast messages
    LaunchedEffect(Unit) {
        viewModel.toastMessage.collect { message ->
            Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
        }
    }

    // Auto-scroll to bottom when a new message arrives, and again when the
    // keyboard opens — otherwise the newest bubble stays hidden behind it.
    val density = LocalDensity.current
    val imeVisible = WindowInsets.ime.getBottom(density) > 0
    LaunchedEffect(uiState.messages.size, imeVisible) {
        if (uiState.messages.isNotEmpty()) {
            listState.animateScrollToItem(uiState.messages.size - 1)
        }
    }

    // Show connection dialog when not connected
    val shouldShowDialog =
        uiState.connectionState !is com.example.lorabridge.domain.model.BleConnectionState.Connected

    // Start scanning whenever dialog appears (when not connected)
    LaunchedEffect(shouldShowDialog) {
        if (shouldShowDialog && permissionsState.allPermissionsGranted) {
            viewModel.startBleScan()
        }
    }

    if (shouldShowDialog) {
        ConnectionDialog(
            connectionState = uiState.connectionState,
            discoveredDevices = uiState.discoveredDevices,
            onDeviceConnect = { deviceAddress ->
                viewModel.connectToDevice(deviceAddress)
            },
            onDismiss = {
                // Retry scanning
                viewModel.startBleScan()
            }
        )
    }

    // Device Info Dialog
    if (uiState.showInfoDialog) {
        DeviceInfoDialog(
            info = uiState.deviceInfo,
            onDismiss = { viewModel.dismissInfoDialog() }
        )
    }

    Scaffold(
        topBar = {
            CenterAlignedTopAppBar(
                title = {
                    Text(
                        text = "LoRa Chat",
                        style = MaterialTheme.typography.titleLarge
                    )
                },
                actions = {
                    val isConnected =
                        uiState.connectionState is com.example.lorabridge.domain.model.BleConnectionState.Connected
                    if (isConnected) {
                        IconButton(onClick = { viewModel.requestDeviceInfo() }) {
                            Icon(
                                imageVector = Icons.Default.Info,
                                contentDescription = "Device Info"
                            )
                        }
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.surface,
                    titleContentColor = MaterialTheme.colorScheme.onSurface,
                    actionIconContentColor = MaterialTheme.colorScheme.onSurfaceVariant
                )
            )
        },
        // The composer applies the bottom insets itself (see below), so the
        // Scaffold must not also reserve space for them.
        contentWindowInsets = WindowInsets(0, 0, 0, 0)
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
        ) {
            // Status strip: connection on the left, GPS fix on the right. Sits on
            // a tinted surface so it reads as chrome rather than as chat content.
            val isConnected =
                uiState.connectionState is com.example.lorabridge.domain.model.BleConnectionState.Connected

            Surface(
                color = MaterialTheme.colorScheme.surfaceVariant,
                contentColor = MaterialTheme.colorScheme.onSurfaceVariant
            ) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(start = 16.dp, end = 4.dp, top = 6.dp, bottom = 6.dp),
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Box(
                        modifier = Modifier
                            .size(8.dp)
                            .background(
                                color = if (isConnected) {
                                    MaterialTheme.colorScheme.primary
                                } else {
                                    MaterialTheme.colorScheme.error
                                },
                                shape = CircleShape
                            )
                    )

                    Text(
                        text = uiState.connectionStatusText,
                        style = MaterialTheme.typography.bodyMedium,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.weight(1f)
                    )

                    Icon(
                        imageVector = if (uiState.hasGpsFix) {
                            Icons.Default.LocationOn
                        } else {
                            Icons.Default.LocationOff
                        },
                        contentDescription = null,
                        modifier = Modifier.size(14.dp),
                        tint = if (uiState.hasGpsFix) {
                            MaterialTheme.colorScheme.primary
                        } else {
                            MaterialTheme.colorScheme.onSurfaceVariant
                        }
                    )

                    Text(
                        text = uiState.gpsText,
                        style = MaterialTheme.typography.bodySmall,
                        maxLines = 1
                    )

                    // Show disconnect button when connected
                    if (isConnected) {
                        IconButton(onClick = { viewModel.disconnect() }) {
                            Icon(
                                imageVector = Icons.Default.Close,
                                contentDescription = "Disconnect",
                                tint = MaterialTheme.colorScheme.error
                            )
                        }
                    }
                }
            }

            // Messages list
            Box(modifier = Modifier.weight(1f)) {
                LazyColumn(
                    state = listState,
                    modifier = Modifier.fillMaxSize(),
                    contentPadding = PaddingValues(horizontal = 12.dp, vertical = 8.dp)
                ) {
                    items(
                        items = uiState.messages,
                        key = { "${it.seq}_${it.timestamp}" }
                    ) { message ->
                        MessageBubble(
                            message = message,
                            onMapClick = { lat, lon ->
                                // Open Google Maps
                                try {
                                    val gmmIntentUri = "geo:$lat,$lon?q=$lat,$lon".toUri()
                                    val mapIntent = Intent(Intent.ACTION_VIEW, gmmIntentUri)
                                    mapIntent.setPackage("com.google.android.apps.maps")

                                    if (mapIntent.resolveActivity(context.packageManager) != null) {
                                        context.startActivity(mapIntent)
                                    } else {
                                        // Fallback to browser
                                        val browserUri =
                                            "https://www.google.com/maps/search/?api=1&query=$lat,$lon".toUri()
                                        context.startActivity(
                                            Intent(
                                                Intent.ACTION_VIEW,
                                                browserUri
                                            )
                                        )
                                    }
                                } catch (e: Exception) {
                                    Toast.makeText(
                                        context,
                                        "Error opening map: ${e.message}",
                                        Toast.LENGTH_SHORT
                                    ).show()
                                }
                            }
                        )
                    }
                }
            }

            // Composer. The keyboard and the gesture bar occupy the same screen
            // edge, so the inset has to be the UNION of the two, not the sum:
            // stacking imePadding() and navigationBarsPadding() double-counted the
            // gesture bar and made the field jump while the keyboard animated.
            HorizontalDivider(color = MaterialTheme.colorScheme.outline.copy(alpha = 0.3f))

            Surface(color = MaterialTheme.colorScheme.surface) {
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .windowInsetsPadding(
                            WindowInsets.ime.union(WindowInsets.navigationBars)
                                .only(WindowInsetsSides.Bottom)
                        )
                        .padding(horizontal = 12.dp, vertical = 8.dp)
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        verticalAlignment = Alignment.Bottom,
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        OutlinedTextField(
                            value = uiState.messageInput,
                            onValueChange = { viewModel.updateMessageInput(it) },
                            modifier = Modifier.weight(1f),
                            placeholder = { Text("Type message\u2026") },
                            maxLines = 4,
                            shape = RoundedCornerShape(24.dp),
                            isError = uiState.charCount > Message.MAX_TEXT_LENGTH,
                            colors = OutlinedTextFieldDefaults.colors(
                                unfocusedBorderColor = MaterialTheme.colorScheme.outline
                                    .copy(alpha = 0.5f)
                            )
                        )

                        val canSend = uiState.canSendMessage && uiState.messageInput.isNotBlank()
                        FilledIconButton(
                            onClick = {
                                if (uiState.messageInput.isNotBlank()) {
                                    viewModel.sendMessage(uiState.messageInput)
                                    viewModel.updateMessageInput("")
                                }
                            },
                            enabled = canSend,
                            modifier = Modifier.size(52.dp),
                            colors = IconButtonDefaults.filledIconButtonColors(
                                containerColor = MaterialTheme.colorScheme.primary,
                                contentColor = MaterialTheme.colorScheme.onPrimary
                            )
                        ) {
                            Icon(
                                imageVector = Icons.AutoMirrored.Filled.Send,
                                contentDescription = "Send"
                            )
                        }
                    }

                    // Character count, right-aligned under the field it describes.
                    Text(
                        text = uiState.charCountText,
                        modifier = Modifier
                            .align(Alignment.End)
                            .padding(end = 64.dp, top = 2.dp),
                        style = MaterialTheme.typography.labelSmall,
                        color = when {
                            uiState.charCount >= Message.MAX_TEXT_LENGTH ->
                                MaterialTheme.colorScheme.error
                            uiState.charCount >= WARN_CHAR_COUNT ->
                                MaterialTheme.colorScheme.tertiary
                            else -> MaterialTheme.colorScheme.onSurfaceVariant
                        }
                    )
                }
            }
        }
    }
}

/** Character count at which the counter turns amber as a soft warning. */
private const val WARN_CHAR_COUNT = 45

@Composable
private fun DeviceInfoDialog(
    info: DeviceInfo?,
    onDismiss: () -> Unit
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Device Info") },
        text = {
            if (info == null) {
                Box(
                    modifier = Modifier.fillMaxWidth(),
                    contentAlignment = Alignment.Center
                ) {
                    CircularProgressIndicator()
                }
            } else {
                val freqMHz = info.frequencyHz / 1_000_000.0
                val bwKHz = info.bandwidthHz / 1_000

                Column {
                    InfoRow("Battery", "${info.batteryLevel}%")
                    InfoRow("RSSI", "${info.rssi} dBm")
                    InfoRow("SNR", "${"%.2f".format(info.snr)} dB")
                    InfoRow("Frequency", "${"%.2f".format(freqMHz)} MHz")
                    InfoRow("Bandwidth", "$bwKHz kHz")
                    InfoRow("Spreading Factor", "SF${info.spreadingFactor}")
                    InfoRow("Coding Rate", "4/${info.codingRate}")
                    InfoRow("TX Power", "${info.txPower} dBm")
                }
            }
        },
        confirmButton = {
            TextButton(onClick = onDismiss) {
                Text("Close")
            }
        }
    )
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = label,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.weight(1f)
        )
        Text(
            text = value,
            style = MaterialTheme.typography.bodyMedium,
            fontWeight = FontWeight.Medium
        )
    }
}
