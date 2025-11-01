package com.example.lorabridge.presentation.components

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.example.lorabridge.data.ble.BleRepository
import com.example.lorabridge.domain.model.BleConnectionState

/**
 * Material3 dialog for selecting and connecting to BLE devices
 */
@Composable
fun ConnectionDialog(
    connectionState: BleConnectionState,
    discoveredDevices: List<BleRepository.DiscoveredDevice>,
    onDeviceConnect: (String) -> Unit,
    onDismiss: () -> Unit
) {
    AlertDialog(
        onDismissRequest = { /* Prevent dismissal by clicking outside */ },
        icon = {
            Icon(
                imageVector = Icons.Default.Bluetooth,
                contentDescription = "Bluetooth",
                tint = MaterialTheme.colorScheme.primary
            )
        },
        title = {
            Text(
                text = "Connect to Device",
                style = MaterialTheme.typography.headlineSmall,
                fontWeight = FontWeight.Bold
            )
        },
        text = {
            Column(
                modifier = Modifier.fillMaxWidth()
            ) {
                // Connection status
                Text(
                    text = when (connectionState) {
                        is BleConnectionState.Scanning -> "🔍 Scanning for devices..."
                        is BleConnectionState.Connecting -> "📡 Connecting..."
                        is BleConnectionState.NegotiatingMtu -> "🔗 Negotiating..."
                        is BleConnectionState.DiscoveringServices -> "🔧 Discovering services..."
                        is BleConnectionState.EnablingNotifications -> "🔔 Setting up..."
                        is BleConnectionState.Error -> "❌ ${connectionState.message}"
                        else -> "Searching for ESP32S3-LoRa devices"
                    },
                    style = MaterialTheme.typography.bodyMedium,
                    color = if (connectionState is BleConnectionState.Error) {
                        MaterialTheme.colorScheme.error
                    } else {
                        MaterialTheme.colorScheme.onSurface
                    }
                )

                Spacer(modifier = Modifier.height(16.dp))

                // Device list
                if (discoveredDevices.isEmpty() && connectionState is BleConnectionState.Scanning) {
                    // Show loading indicator while scanning
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(32.dp),
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        CircularProgressIndicator()
                        Spacer(modifier = Modifier.height(16.dp))
                        Text(
                            text = "Looking for devices...",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant
                        )
                    }
                } else if (discoveredDevices.isEmpty()) {
                    // No devices found
                    Text(
                        text = "No devices found",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.padding(16.dp)
                    )
                } else {
                    // Show device list
                    LazyColumn(
                        modifier = Modifier.fillMaxWidth(),
                        verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        items(
                            items = discoveredDevices,
                            key = { it.address }
                        ) { device ->
                            DeviceItem(
                                device = device,
                                isConnecting = connectionState is BleConnectionState.Connecting ||
                                        connectionState is BleConnectionState.NegotiatingMtu ||
                                        connectionState is BleConnectionState.DiscoveringServices ||
                                        connectionState is BleConnectionState.EnablingNotifications,
                                onConnect = { onDeviceConnect(device.address) }
                            )
                        }
                    }
                }
            }
        },
        confirmButton = {
            // No confirm button needed - dialog dismisses on connection
        },
        dismissButton = {
            if (connectionState is BleConnectionState.Error) {
                TextButton(onClick = onDismiss) {
                    Text("Retry")
                }
            }
        }
    )
}

/**
 * Individual device item in the list
 */
@Composable
private fun DeviceItem(
    device: BleRepository.DiscoveredDevice,
    isConnecting: Boolean,
    onConnect: () -> Unit
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.surfaceVariant
        )
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(12.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Column(
                modifier = Modifier.weight(1f)
            ) {
                Text(
                    text = device.name ?: "Unknown Device",
                    style = MaterialTheme.typography.bodyLarge,
                    fontWeight = FontWeight.Medium
                )
                Text(
                    text = device.address,
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
                Text(
                    text = "Signal: ${device.rssi} dBm",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant
                )
            }

            Spacer(modifier = Modifier.width(8.dp))

            Button(
                onClick = onConnect,
                enabled = !isConnecting
            ) {
                Text("Connect")
            }
        }
    }
}
