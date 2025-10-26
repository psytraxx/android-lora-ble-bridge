package com.example.lorabridge.domain.model

import org.junit.Assert.*
import org.junit.Test

/**
 * Unit tests for LocationData model
 * @see UC-2.2: Get Last Known Location
 */
class LocationDataTest {

    @Test
    fun `toDisplayString should format correctly`() {
        val location = LocationData(
            latitude = 47.123456,
            longitude = 8.987654,
            provider = "GPS"
        )

        val displayString = location.toDisplayString()

        assertEquals("47.123456, 8.987654 (GPS)", displayString)
    }

    @Test
    fun `isRecent should return true for fresh location`() {
        val location = LocationData(
            latitude = 47.0,
            longitude = 8.0,
            provider = "GPS",
            timestamp = System.currentTimeMillis()
        )

        assertTrue(location.isRecent())
    }

    @Test
    fun `isRecent should return false for old location`() {
        val location = LocationData(
            latitude = 47.0,
            longitude = 8.0,
            provider = "GPS",
            timestamp = System.currentTimeMillis() - 120_000 // 2 minutes ago
        )

        assertFalse(location.isRecent())
    }

    @Test
    fun `latitudeMicro should convert to microdegrees`() {
        val location = LocationData(
            latitude = 47.123456,
            longitude = 8.0,
            provider = "GPS"
        )

        assertEquals(47_123_456, location.latitudeMicro())
    }

    @Test
    fun `longitudeMicro should convert to microdegrees`() {
        val location = LocationData(
            latitude = 47.0,
            longitude = 8.987654,
            provider = "GPS"
        )

        assertEquals(8_987_654, location.longitudeMicro())
    }

    @Test
    fun `fromMicro should convert from microdegrees`() {
        val location = LocationData.fromMicro(
            latMicro = 47_123_456,
            lonMicro = 8_987_654,
            provider = "LoRa"
        )

        assertEquals(47.123456, location.latitude, 0.000001)
        assertEquals(8.987654, location.longitude, 0.000001)
        assertEquals("LoRa", location.provider)
    }

    @Test
    fun `round trip micro conversion should preserve precision`() {
        val original = LocationData(
            latitude = 47.123456,
            longitude = 8.987654,
            provider = "GPS"
        )

        val roundTrip = LocationData.fromMicro(
            latMicro = original.latitudeMicro(),
            lonMicro = original.longitudeMicro(),
            provider = original.provider
        )

        assertEquals(original.latitude, roundTrip.latitude, 0.000001)
        assertEquals(original.longitude, roundTrip.longitude, 0.000001)
    }

    @Test
    fun `negative coordinates should work correctly`() {
        val location = LocationData(
            latitude = -33.8688,  // Sydney
            longitude = 151.2093,
            provider = "GPS"
        )

        assertEquals(-33_868_800, location.latitudeMicro())
        assertEquals(151_209_300, location.longitudeMicro())

        val displayString = location.toDisplayString()
        assertTrue(displayString.contains("-33.868800"))
    }
}
