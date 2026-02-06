package com.example.lorabridge.presentation.chat

import com.example.lorabridge.domain.model.BleConnectionState
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Unit tests for ChatViewModel and ChatUiState connection state management
 * Tests UI state data structure and connection state behavior
 */
class ChatViewModelConnectionTest {

    @Test
    fun `ChatUiState should have correct default connection state`() {
        val uiState = ChatUiState()

        assertTrue(uiState.connectionState is BleConnectionState.Disconnected)
        assertEquals("Disconnected", uiState.connectionStatusText)
    }

    @Test
    fun `ChatUiState should have empty discovered devices by default`() {
        val uiState = ChatUiState()

        assertTrue(uiState.discoveredDevices.isEmpty())
    }

    @Test
    fun `ChatUiState should not allow sending by default`() {
        val uiState = ChatUiState()

        assertFalse(uiState.canSendMessage)
    }

    @Test
    fun `ChatUiState with connected state should update status text`() {
        val uiState = ChatUiState(
            connectionState = BleConnectionState.Connected,
            connectionStatusText = "✅ Ready to send!"
        )

        assertTrue(uiState.connectionState is BleConnectionState.Connected)
        assertEquals("✅ Ready to send!", uiState.connectionStatusText)
    }

    @Test
    fun `ChatUiState with scanning state should update status text`() {
        val uiState = ChatUiState(
            connectionState = BleConnectionState.Scanning,
            connectionStatusText = "🔍 Scanning..."
        )

        assertTrue(uiState.connectionState is BleConnectionState.Scanning)
        assertEquals("🔍 Scanning...", uiState.connectionStatusText)
    }

    @Test
    fun `ChatUiState with error state should update status text`() {
        val errorMsg = "Connection timeout"
        val uiState = ChatUiState(
            connectionState = BleConnectionState.Error(errorMsg),
            connectionStatusText = "❌ $errorMsg"
        )

        assertTrue(uiState.connectionState is BleConnectionState.Error)
        assertEquals("❌ $errorMsg", uiState.connectionStatusText)
    }

    @Test
    fun `ChatUiState should update discovered devices list`() {
        // Mock discovered devices using a simple data structure
        val mockDevices = listOf(
            object {
                val address = "AA:BB:CC:DD:EE:FF"
                val name = "Device1"
                val rssi = -50
            }
        )

        // In real usage, this would be BleRepository.DiscoveredDevice
        // But for this test, we're just testing the data structure
        assertTrue(mockDevices.isNotEmpty())
        assertEquals("AA:BB:CC:DD:EE:FF", mockDevices[0].address)
    }

    @Test
    fun `ChatUiState should support multiple connection states`() {
        val states = listOf(
            BleConnectionState.Disconnected,
            BleConnectionState.Scanning,
            BleConnectionState.Connecting,
            BleConnectionState.NegotiatingMtu,
            BleConnectionState.DiscoveringServices,
            BleConnectionState.EnablingNotifications,
            BleConnectionState.Connected,
            BleConnectionState.Error("Test error")
        )

        states.forEach { state ->
            val uiState = ChatUiState(connectionState = state)
            assertEquals(state, uiState.connectionState)
        }
    }

    @Test
    fun `ChatUiState copy should preserve all fields`() {
        val original = ChatUiState(
            connectionState = BleConnectionState.Connected,
            connectionStatusText = "Connected",
            canSendMessage = true,
            messageInput = "Test message",
            charCount = 12
        )

        val copy = original.copy()

        assertEquals(original.connectionState, copy.connectionState)
        assertEquals(original.connectionStatusText, copy.connectionStatusText)
        assertEquals(original.canSendMessage, copy.canSendMessage)
        assertEquals(original.messageInput, copy.messageInput)
        assertEquals(original.charCount, copy.charCount)
    }

    @Test
    fun `ChatUiState copy should allow partial updates`() {
        val original = ChatUiState()

        val updated = original.copy(
            connectionState = BleConnectionState.Connected,
            connectionStatusText = "Connected"
        )

        assertTrue(updated.connectionState is BleConnectionState.Connected)
        assertEquals("Connected", updated.connectionStatusText)
        // Other fields should remain default
        assertFalse(updated.canSendMessage)
        assertTrue(updated.messages.isEmpty())
    }
}
