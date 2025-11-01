package com.example.lorabridge.data.location

import android.annotation.SuppressLint
import android.content.Context
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.util.Log
import com.example.lorabridge.domain.model.LocationData
import com.google.android.gms.location.FusedLocationProviderClient
import com.google.android.gms.location.LocationServices
import com.google.android.gms.location.Priority
import com.google.android.gms.tasks.CancellationTokenSource
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.tasks.await
import javax.inject.Inject
import javax.inject.Singleton

/**
 * Repository for GPS/Location operations
 * Uses FusedLocationProviderClient for best accuracy
 */
@Singleton
class LocationRepository @Inject constructor(
    @param:ApplicationContext private val context: Context
) {
    private val scope = CoroutineScope(Dispatchers.IO + Job())

    private val locationManager =
        context.getSystemService(Context.LOCATION_SERVICE) as LocationManager
    private val fusedLocationClient: FusedLocationProviderClient =
        LocationServices.getFusedLocationProviderClient(context)

    private val _currentLocation = MutableStateFlow<LocationData?>(null)
    val currentLocation: StateFlow<LocationData?> = _currentLocation.asStateFlow()

    // Track if single update is in progress
    private var singleUpdateJob: Job? = null
    private var gpsListener: LocationListener? = null
    private var networkListener: LocationListener? = null

    companion object {
        private const val TAG = "LocationRepository"
        private const val LOCATION_CACHE_VALIDITY_MS = 60_000L  // 1 minute
    }

    /**
     * Request a single fresh GPS update (event-driven)
     * Used when user sends a message
     * @see UC-2.1: Request Single GPS Update
     */
    @SuppressLint("MissingPermission")
    fun requestSingleUpdate() {
        Log.d(TAG, "Requesting single GPS update")

        // Cancel any pending update
        cancelSingleUpdate()

        singleUpdateJob = scope.launch {
            try {
                // Try Fused Location Provider first (most accurate + efficient)
                val cancellationToken = CancellationTokenSource()
                val location = fusedLocationClient.getCurrentLocation(
                    Priority.PRIORITY_HIGH_ACCURACY,
                    cancellationToken.token
                ).await()

                if (location != null) {
                    updateLocation(location, "Fused")
                    Log.d(TAG, "Got location from Fused provider")
                } else {
                    Log.w(
                        TAG,
                        "Fused provider returned null, falling back to traditional providers"
                    )
                    requestFromTraditionalProviders()
                }
            } catch (e: Exception) {
                Log.e(TAG, "Error getting fused location", e)
                requestFromTraditionalProviders()
            }
        }
    }

    /**
     * Fallback to traditional GPS/Network providers
     */
    @SuppressLint("MissingPermission")
    private fun requestFromTraditionalProviders() {
        // GPS provider
        if (locationManager.isProviderEnabled(LocationManager.GPS_PROVIDER)) {
            gpsListener = LocationListener { location ->
                updateLocation(location, "GPS")
                removeListeners()
            }
            locationManager.requestSingleUpdate(LocationManager.GPS_PROVIDER, gpsListener!!, null)
            Log.d(TAG, "Requested single update from GPS")
        }

        // Network provider (fallback)
        if (locationManager.isProviderEnabled(LocationManager.NETWORK_PROVIDER)) {
            networkListener = LocationListener { location ->
                updateLocation(location, "Network")
                removeListeners()
            }
            locationManager.requestSingleUpdate(
                LocationManager.NETWORK_PROVIDER,
                networkListener!!,
                null
            )
            Log.d(TAG, "Requested single update from Network")
        }
    }

    /**
     * Get last known location (from cache or system)
     * @see UC-2.2: Get Last Known Location
     */
    @SuppressLint("MissingPermission")
    suspend fun getLastKnownLocation(): LocationData? {
        // Return cached if recent
        _currentLocation.value?.let { cached ->
            if (System.currentTimeMillis() - cached.timestamp < LOCATION_CACHE_VALIDITY_MS) {
                Log.d(
                    TAG,
                    "Returning cached location (${System.currentTimeMillis() - cached.timestamp}ms old)"
                )
                return cached
            }
        }

        // Try Fused provider
        try {
            val fusedLocation = fusedLocationClient.lastLocation.await()
            if (fusedLocation != null) {
                val locationData = fusedLocation.toLocationData("Fused")
                _currentLocation.value = locationData
                Log.d(TAG, "Got last known from Fused provider")
                return locationData
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error getting fused last location", e)
        }

        // Fallback to GPS
        val gpsLocation = locationManager.getLastKnownLocation(LocationManager.GPS_PROVIDER)
        if (gpsLocation != null) {
            val locationData = gpsLocation.toLocationData("GPS")
            _currentLocation.value = locationData
            Log.d(TAG, "Got last known from GPS provider")
            return locationData
        }

        // Final fallback to Network
        val networkLocation = locationManager.getLastKnownLocation(LocationManager.NETWORK_PROVIDER)
        if (networkLocation != null) {
            val locationData = networkLocation.toLocationData("Network")
            _currentLocation.value = locationData
            Log.d(TAG, "Got last known from Network provider")
            return locationData
        }

        Log.w(TAG, "No location available")
        return _currentLocation.value
    }

    /**
     * Update current location
     */
    private fun updateLocation(location: Location, provider: String) {
        val locationData = location.toLocationData(provider)
        _currentLocation.value = locationData
        Log.d(TAG, "Location updated: $locationData")
    }

    /**
     * Cancel single update request
     */
    private fun cancelSingleUpdate() {
        singleUpdateJob?.cancel()
        removeListeners()
    }

    /**
     * Remove location listeners to prevent memory leaks
     */
    private fun removeListeners() {
        gpsListener?.let { locationManager.removeUpdates(it) }
        networkListener?.let { locationManager.removeUpdates(it) }
        gpsListener = null
        networkListener = null
        Log.d(TAG, "Location listeners removed")
    }

    /**
     * Check if location services are enabled
     */
    fun isLocationEnabled(): Boolean {
        return locationManager.isProviderEnabled(LocationManager.GPS_PROVIDER) ||
                locationManager.isProviderEnabled(LocationManager.NETWORK_PROVIDER)
    }

    /**
     * Convert Android Location to domain LocationData
     */
    private fun Location.toLocationData(provider: String): LocationData {
        return LocationData(
            latitude = latitude,
            longitude = longitude,
            provider = provider,
            timestamp = time
        )
    }

    /**
     * Clean up when repository is destroyed
     */
    fun onDestroy() {
        cancelSingleUpdate()
        scope.cancel()
    }
}
