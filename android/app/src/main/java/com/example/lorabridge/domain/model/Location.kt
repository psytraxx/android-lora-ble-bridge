package com.example.lorabridge.domain.model

/**
 * Domain model for GPS location
 */
data class LocationData(
    val latitude: Double,
    val longitude: Double,
    val provider: String,
    val timestamp: Long = System.currentTimeMillis()
) {
    /**
     * Format for display: "lat, lon (provider)"
     */
    fun toDisplayString(): String =
        "%.6f, %.6f (%s)".format(latitude, longitude, provider)

    /**
     * Check if location is recent (< 1 minute old)
     */
    fun isRecent(): Boolean {
        val ageMs = System.currentTimeMillis() - timestamp
        return ageMs < 60_000 // 1 minute
    }

    /**
     * Convert to microdegrees for protocol serialization
     */
    fun latitudeMicro(): Int = (latitude * 1_000_000).toInt()
    fun longitudeMicro(): Int = (longitude * 1_000_000).toInt()

    companion object {
        /**
         * Create from microdegrees (protocol deserialization)
         */
        fun fromMicro(latMicro: Int, lonMicro: Int, provider: String = "LoRa"): LocationData {
            return LocationData(
                latitude = latMicro / 1_000_000.0,
                longitude = lonMicro / 1_000_000.0,
                provider = provider
            )
        }
    }
}
