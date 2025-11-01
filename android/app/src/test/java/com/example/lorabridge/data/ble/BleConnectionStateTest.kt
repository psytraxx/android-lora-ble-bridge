package com.example.lorabridge.data.ble

import com.example.lorabridge.domain.model.BleConnectionState
import com.example.lorabridge.domain.model.toDisplayString
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Unit tests for BleConnectionState
 * Tests state representation and display strings
 */
class BleConnectionStateTest {

    @Test
    fun `Disconnected state should have correct display string`() {
        val state = BleConnectionState.Disconnected
        assertEquals("❌ Disconnected", state.toDisplayString())
    }

    @Test
    fun `Scanning state should have correct display string`() {
        val state = BleConnectionState.Scanning
        assertEquals("🔍 Scanning...", state.toDisplayString())
    }

    @Test
    fun `Connecting state should have correct display string`() {
        val state = BleConnectionState.Connecting
        assertEquals("📡 Connecting...", state.toDisplayString())
    }

    @Test
    fun `Connected state should have correct display string`() {
        val state = BleConnectionState.Connected
        assertEquals("✅ Ready to send!", state.toDisplayString())
    }

    @Test
    fun `Error state should include error message in display string`() {
        val state = BleConnectionState.Error("Connection timeout")
        assertEquals("❌ Connection timeout", state.toDisplayString())
    }

    @Test
    fun `Error state with canRetry true should be retryable`() {
        val state = BleConnectionState.Error("Device not found", canRetry = true)
        assertTrue(state.canRetry)
    }

    @Test
    fun `Error state with canRetry false should not be retryable`() {
        val state = BleConnectionState.Error("Fatal error", canRetry = false)
        assertFalse(state.canRetry)
    }

    @Test
    fun `Error state defaults to retryable`() {
        val state = BleConnectionState.Error("Test error")
        assertTrue(state.canRetry)
    }

    @Test
    fun `all intermediate states should have correct display strings`() {
        val states = mapOf(
            BleConnectionState.CheckingPermissions to "🔐 Checking permissions...",
            BleConnectionState.CheckingLocation to "📍 Checking location...",
            BleConnectionState.NegotiatingMtu to "🔗 Negotiating...",
            BleConnectionState.DiscoveringServices to "🔧 Discovering services...",
            BleConnectionState.EnablingNotifications to "🔔 Setting up..."
        )

        states.forEach { (state, expectedString) ->
            assertEquals(expectedString, state.toDisplayString())
        }
    }

    @Test
    fun `Connection states should be distinguishable`() {
        val disconnected = BleConnectionState.Disconnected
        val scanning = BleConnectionState.Scanning
        val connected = BleConnectionState.Connected
        val error = BleConnectionState.Error("Test")

        assertNotEquals(disconnected, scanning)
        assertNotEquals(scanning, connected)
        assertNotEquals(connected, error)
        assertNotEquals(disconnected, connected)
    }

    @Test
    fun `Error states with same message should be equal`() {
        val error1 = BleConnectionState.Error("Same message", canRetry = true)
        val error2 = BleConnectionState.Error("Same message", canRetry = true)

        assertEquals(error1, error2)
    }

    @Test
    fun `Error states with different messages should not be equal`() {
        val error1 = BleConnectionState.Error("Message 1")
        val error2 = BleConnectionState.Error("Message 2")

        assertNotEquals(error1, error2)
    }

    @Test
    fun `Error states with different canRetry should not be equal`() {
        val error1 = BleConnectionState.Error("Same message", canRetry = true)
        val error2 = BleConnectionState.Error("Same message", canRetry = false)

        assertNotEquals(error1, error2)
    }
}
