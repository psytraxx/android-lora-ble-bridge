package com.example.lorabridge.presentation.chat

import android.Manifest
import android.content.Intent
import android.os.Build
import android.widget.Toast
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.CenterAlignedTopAppBar
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextField
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.net.toUri
import androidx.hilt.lifecycle.viewmodel.compose.hiltViewModel
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

    // Auto-scroll to bottom when new message added
    LaunchedEffect(uiState.messages.size) {
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

    Scaffold(
        topBar = {
            CenterAlignedTopAppBar(
                title = { Text("LoRa Chat", fontWeight = FontWeight.Bold) },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.primaryContainer,
                )
            )
        }
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .imePadding()
        ) {
            // Status row
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                val isConnected =
                    uiState.connectionState is com.example.lorabridge.domain.model.BleConnectionState.Connected

                Text(
                    text = uiState.connectionStatusText,
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier.weight(1f),
                    color = if (isConnected) {
                        MaterialTheme.colorScheme.primary
                    } else {
                        MaterialTheme.colorScheme.onSurface
                    }
                )

                // Battery level indicator
                if (uiState.batteryLevel != null) {
                    Text(
                        text = "${uiState.batteryLevel}%",
                        style = MaterialTheme.typography.bodySmall,
                        fontSize = 12.sp,
                        modifier = Modifier.padding(end = 8.dp)
                    )
                }

                Text(
                    text = uiState.gpsText,
                    style = MaterialTheme.typography.bodySmall,
                    fontSize = 12.sp
                )
                
                // Show disconnect button when connected
                if (isConnected) {
                    IconButton(
                        onClick = { viewModel.disconnect() },
                        modifier = Modifier.padding(start = 8.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.Close,
                            contentDescription = "Disconnect",
                            tint = MaterialTheme.colorScheme.error
                        )
                    }
                }
            }

            // Messages list
            Box(modifier = Modifier.weight(1f)) {
                LazyColumn(
                    state = listState,
                    modifier = Modifier
                        .fillMaxSize()
                        .padding(horizontal = 16.dp)
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

            // Character count
            Text(
                text = uiState.charCountText,
                modifier = Modifier
                    .padding(horizontal = 16.dp, vertical = 4.dp),
                style = MaterialTheme.typography.bodySmall,
                color = when {
                    uiState.charCount >= 50 -> MaterialTheme.colorScheme.error
                    uiState.charCount >= 45 -> MaterialTheme.colorScheme.tertiary
                    else -> MaterialTheme.colorScheme.onSurfaceVariant
                }
            )

            // Input row
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(16.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                TextField(
                    value = uiState.messageInput,
                    onValueChange = { viewModel.updateMessageInput(it) },
                    modifier = Modifier.weight(1f),
                    placeholder = { Text("Type message...") },
                    maxLines = 3
                )
                Spacer(modifier = Modifier.width(8.dp))
                IconButton(
                    onClick = {
                        if (uiState.messageInput.isNotBlank()) {
                            viewModel.sendMessage(uiState.messageInput)
                            viewModel.updateMessageInput("")
                        }
                    },
                    enabled = uiState.canSendMessage && uiState.messageInput.isNotBlank()
                ) {
                    Icon(
                        imageVector = Icons.AutoMirrored.Filled.Send,
                        contentDescription = "Send"
                    )
                }
            }
        }
    }
}
