package com.example.lorabridge.domain.model

/**
 * Represents the BLE connection state machine
 * See docs/STATE_DIAGRAM.md for state transitions
 */
sealed class BleConnectionState {
    data object Disconnected : BleConnectionState()
    data object CheckingPermissions : BleConnectionState()
    data object CheckingLocation : BleConnectionState()
    data object Scanning : BleConnectionState()
    data object Connecting : BleConnectionState()
    data object NegotiatingMtu : BleConnectionState()
    data object DiscoveringServices : BleConnectionState()
    data object EnablingNotifications : BleConnectionState()
    data object Connected : BleConnectionState()
    data class Error(val message: String, val canRetry: Boolean = true) : BleConnectionState()
}

/**
 * User-friendly connection status text
 */
fun BleConnectionState.toDisplayString(): String = when (this) {
    is BleConnectionState.Disconnected -> "❌ Disconnected"
    is BleConnectionState.CheckingPermissions -> "🔐 Checking permissions..."
    is BleConnectionState.CheckingLocation -> "📍 Checking location..."
    is BleConnectionState.Scanning -> "🔍 Scanning..."
    is BleConnectionState.Connecting -> "📡 Connecting..."
    is BleConnectionState.NegotiatingMtu -> "🔗 Negotiating..."
    is BleConnectionState.DiscoveringServices -> "🔧 Discovering services..."
    is BleConnectionState.EnablingNotifications -> "🔔 Setting up..."
    is BleConnectionState.Connected -> "✅ Ready to send!"
    is BleConnectionState.Error -> "❌ ${this.message}"
}
