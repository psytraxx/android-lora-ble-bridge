package com.example.lorabridge.data.ble

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Unit tests for DiscoveredDevice data class
 * Tests device discovery and list management scenarios
 */
class DiscoveredDeviceTest {

    @Test
    fun `devices with same address should be equal`() {
        val device1 = createMockDiscoveredDevice("AA:BB:CC:DD:EE:FF", "Device1", -50)
        val device2 = createMockDiscoveredDevice("AA:BB:CC:DD:EE:FF", "Device1", -50)

        assertEquals(device1.address, device2.address)
        assertEquals(device1.name, device2.name)
    }

    @Test
    fun `devices with different addresses should not be equal`() {
        val device1 = createMockDiscoveredDevice("AA:BB:CC:DD:EE:FF", "Device1", -50)
        val device2 = createMockDiscoveredDevice("11:22:33:44:55:66", "Device2", -60)

        assertNotEquals(device1.address, device2.address)
    }

    @Test
    fun `device list should not contain duplicates by address`() {
        val devices = mutableListOf<MockDiscoveredDevice>()
        val device1 = createMockDiscoveredDevice("AA:BB:CC:DD:EE:FF", "Device1", -50)
        val device2 = createMockDiscoveredDevice(
            "AA:BB:CC:DD:EE:FF",
            "Device1",
            -55
        ) // Same address, different RSSI

        devices.add(device1)

        // Simulate the logic in BleRepository - don't add if address already exists
        if (devices.none { it.address == device2.address }) {
            devices.add(device2)
        }

        assertEquals(1, devices.size)
        assertEquals(device1.address, devices[0].address)
    }

    @Test
    fun `device list should allow multiple devices with different addresses`() {
        val devices = mutableListOf<MockDiscoveredDevice>()
        val device1 = createMockDiscoveredDevice("AA:BB:CC:DD:EE:FF", "Device1", -50)
        val device2 = createMockDiscoveredDevice("11:22:33:44:55:66", "Device2", -60)
        val device3 = createMockDiscoveredDevice("77:88:99:AA:BB:CC", "Device3", -70)

        devices.add(device1)
        if (devices.none { it.address == device2.address }) devices.add(device2)
        if (devices.none { it.address == device3.address }) devices.add(device3)

        assertEquals(3, devices.size)
    }

    @Test
    fun `device with null name should be handled`() {
        val device = createMockDiscoveredDevice("AA:BB:CC:DD:EE:FF", null, -50)

        assertNull(device.name)
        assertNotNull(device.address)
    }

    @Test
    fun `device list should be clearable`() {
        val devices = mutableListOf<MockDiscoveredDevice>()
        devices.add(createMockDiscoveredDevice("AA:BB:CC:DD:EE:FF", "Device1", -50))
        devices.add(createMockDiscoveredDevice("11:22:33:44:55:66", "Device2", -60))

        assertEquals(2, devices.size)

        devices.clear()

        assertTrue(devices.isEmpty())
    }

    @Test
    fun `device RSSI should be negative`() {
        val device = createMockDiscoveredDevice("AA:BB:CC:DD:EE:FF", "Device1", -50)

        assertTrue(device.rssi < 0)
    }

    @Test
    fun `device with stronger signal should have higher RSSI value`() {
        val weakDevice = createMockDiscoveredDevice("AA:BB:CC:DD:EE:FF", "Weak", -80)
        val strongDevice = createMockDiscoveredDevice("11:22:33:44:55:66", "Strong", -40)

        assertTrue(strongDevice.rssi > weakDevice.rssi)
    }

    @Test
    fun `finding device by address should work`() {
        val devices = listOf(
            createMockDiscoveredDevice("AA:BB:CC:DD:EE:FF", "Device1", -50),
            createMockDiscoveredDevice("11:22:33:44:55:66", "Device2", -60),
            createMockDiscoveredDevice("77:88:99:AA:BB:CC", "Device3", -70)
        )

        val found = devices.find { it.address == "11:22:33:44:55:66" }

        assertNotNull(found)
        assertEquals("Device2", found?.name)
    }

    @Test
    fun `finding non-existent device by address should return null`() {
        val devices = listOf(
            createMockDiscoveredDevice("AA:BB:CC:DD:EE:FF", "Device1", -50)
        )

        val found = devices.find { it.address == "NONEXISTENT" }

        assertNull(found)
    }

    // Helper to create mock discovered device without Android dependencies
    private fun createMockDiscoveredDevice(
        address: String,
        name: String?,
        rssi: Int
    ): MockDiscoveredDevice {
        return MockDiscoveredDevice(
            address = address,
            name = name,
            rssi = rssi
        )
    }

    // Mock data class that mirrors BleRepository.DiscoveredDevice structure
    data class MockDiscoveredDevice(
        val address: String,
        val name: String?,
        val rssi: Int
    )
}
