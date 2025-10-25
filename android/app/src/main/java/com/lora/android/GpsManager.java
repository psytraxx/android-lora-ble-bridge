package com.lora.android;

import android.Manifest;
import android.annotation.SuppressLint;
import android.content.Context;
import android.content.pm.PackageManager;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.util.Log;

import androidx.annotation.NonNull;
import androidx.core.app.ActivityCompat;

import java.util.Locale;

/**
 * Simplified GPS Manager with event-driven location updates
 * Fixes: Memory leaks from anonymous LocationListener instances
 * Removed: Unused continuous update infrastructure
 */
public class GpsManager {

    private static final String TAG = "GpsManager";

    private final Context context;
    private final LocationManager locationManager;
    private Location currentLocation = null;

    // Single reusable listener for GPS updates (prevents memory leaks)
    private final LocationListener gpsListener = new LocationListener() {
        @Override
        public void onLocationChanged(Location location) {
            Log.d(TAG, "GPS update: " + location.getLatitude() + ", " + location.getLongitude());
            updateCurrentLocation(location);
            // Auto-remove after receiving update
            stopLocationUpdates();
        }

        @Override
        public void onProviderEnabled(@NonNull String provider) {
            Log.d(TAG, "GPS provider enabled");
        }

        @Override
        public void onProviderDisabled(@NonNull String provider) {
            Log.d(TAG, "GPS provider disabled");
        }

        @Override
        public void onStatusChanged(String provider, int status, android.os.Bundle extras) {
        }
    };

    // Single reusable listener for Network updates (prevents memory leaks)
    private final LocationListener networkListener = new LocationListener() {
        @Override
        public void onLocationChanged(Location location) {
            Log.d(TAG, "Network update: " + location.getLatitude() + ", " + location.getLongitude());
            updateCurrentLocation(location);
            // Auto-remove after receiving update
            stopLocationUpdates();
        }

        @Override
        public void onProviderEnabled(@NonNull String provider) {
            Log.d(TAG, "Network provider enabled");
        }

        @Override
        public void onProviderDisabled(@NonNull String provider) {
            Log.d(TAG, "Network provider disabled");
        }

        @Override
        public void onStatusChanged(String provider, int status, android.os.Bundle extras) {
        }
    };

    public GpsManager(Context context) {
        this.context = context;
        this.locationManager = (LocationManager) context.getSystemService(Context.LOCATION_SERVICE);
        Log.d(TAG, "GpsManager initialized - event-driven single updates");
    }

    public boolean hasLocationPermission() {
        return ActivityCompat.checkSelfPermission(context,
                Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED;
    }

    /**
     * Update current location only if newer
     */
    private void updateCurrentLocation(Location location) {
        if (currentLocation == null || location.getTime() > currentLocation.getTime()) {
            currentLocation = location;
        }
    }

    /**
     * Request a single location update for event-driven GPS usage.
     * Called when user sends a message.
     * Auto-removes listener after receiving update to prevent memory leaks.
     */
    @SuppressLint("MissingPermission")
    public void requestSingleLocationUpdate() {
        if (!hasLocationPermission()) {
            Log.e(TAG, "Location permission not granted");
            return;
        }

        try {
            // Request single update from GPS (most accurate)
            if (locationManager.isProviderEnabled(LocationManager.GPS_PROVIDER)) {
                locationManager.requestSingleUpdate(LocationManager.GPS_PROVIDER, gpsListener, null);
                Log.d(TAG, "Requested single GPS update");
            }

            // Also request from Network as fallback
            if (locationManager.isProviderEnabled(LocationManager.NETWORK_PROVIDER)) {
                locationManager.requestSingleUpdate(LocationManager.NETWORK_PROVIDER, networkListener, null);
                Log.d(TAG, "Requested single Network update");
            }
        } catch (Exception e) {
            Log.e(TAG, "Error requesting location: " + e.getMessage());
        }
    }

    /**
     * Remove any pending location listeners to prevent memory leaks
     */
    public void stopLocationUpdates() {
        if (locationManager != null) {
            try {
                locationManager.removeUpdates(gpsListener);
                locationManager.removeUpdates(networkListener);
                Log.d(TAG, "Removed location listeners");
            } catch (Exception e) {
                Log.e(TAG, "Error removing listeners: " + e.getMessage());
            }
        }
    }

    /**
     * Get best available location from cache or system
     * Simplified logic: prioritize recent locations
     */
    @SuppressLint("MissingPermission")
    public Location getLastKnownLocation() {
        if (!hasLocationPermission()) {
            Log.e(TAG, "Location permission not granted");
            return null;
        }

        // Use cached location if recent (< 1 minute old)
        if (currentLocation != null) {
            long age = System.currentTimeMillis() - currentLocation.getTime();
            if (age < 60000) {
                Log.d(TAG, "Using cached location (age: " + age + "ms)");
                return currentLocation;
            }
        }

        Location bestLocation = currentLocation;

        // Try Fused provider first (best accuracy + efficiency)
        try {
            Location fusedLocation = locationManager.getLastKnownLocation(LocationManager.FUSED_PROVIDER);
            if (fusedLocation != null && (bestLocation == null || fusedLocation.getTime() > bestLocation.getTime())) {
                bestLocation = fusedLocation;
                Log.d(TAG, "Using fused location");
            }
        } catch (Exception e) {
            Log.e(TAG, "Error getting fused location: " + e.getMessage());
        }

        // Fall back to GPS
        if (bestLocation == null) {
            try {
                Location gpsLocation = locationManager.getLastKnownLocation(LocationManager.GPS_PROVIDER);
                if (gpsLocation != null) {
                    bestLocation = gpsLocation;
                    Log.d(TAG, "Using GPS location");
                }
            } catch (Exception e) {
                Log.e(TAG, "Error getting GPS location: " + e.getMessage());
            }
        }

        // Final fallback to Network
        if (bestLocation == null) {
            try {
                Location networkLocation = locationManager.getLastKnownLocation(LocationManager.NETWORK_PROVIDER);
                if (networkLocation != null) {
                    bestLocation = networkLocation;
                    Log.d(TAG, "Using network location");
                }
            } catch (Exception e) {
                Log.e(TAG, "Error getting network location: " + e.getMessage());
            }
        }

        if (bestLocation == null) {
            Log.w(TAG, "No location available");
        } else {
            currentLocation = bestLocation;
        }

        return bestLocation;
    }
}
