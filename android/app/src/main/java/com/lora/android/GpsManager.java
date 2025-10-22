package com.lora.android;

import android.Manifest;
import android.annotation.SuppressLint;
import android.content.Context;
import android.content.pm.PackageManager;
import android.location.Location;
import android.location.LocationListener;
import android.location.LocationManager;
import android.os.Bundle;
import android.util.Log;

import androidx.core.app.ActivityCompat;

import java.util.Locale;

public class GpsManager {

    private static final String TAG = "GpsManager";
    private static final long MIN_TIME_BETWEEN_UPDATES = 5000; // 5 seconds
    private static final float MIN_DISTANCE_CHANGE = 10; // 10 meters

    private final Context context;
    private final LocationManager locationManager;
    private Location currentLocation = null;
    private final LocationListener locationListener;

    public GpsManager(Context context) {
        this.context = context;
        this.locationManager = (LocationManager) context.getSystemService(Context.LOCATION_SERVICE);

        this.locationListener = new LocationListener() {
            @Override
            public void onLocationChanged(Location location) {
                Log.d(TAG, "Location updated: " + location.getLatitude() + ", " + location.getLongitude() +
                        " from " + location.getProvider());
                currentLocation = location;
            }

            @Override
            public void onStatusChanged(String provider, int status, Bundle extras) {
                Log.d(TAG, "Location provider status changed: " + provider + " status=" + status);
            }

            @Override
            public void onProviderEnabled(String provider) {
                Log.d(TAG, "Location provider enabled: " + provider);
            }

            @Override
            public void onProviderDisabled(String provider) {
                Log.d(TAG, "Location provider disabled: " + provider);
            }
        };

        // Start listening for location updates
        startLocationUpdates();
    }

    public boolean hasLocationPermission() {
        return ActivityCompat.checkSelfPermission(context,
                Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED;
    }

    @SuppressLint("MissingPermission")
    public void startLocationUpdates() {
        if (!hasLocationPermission()) {
            Log.e(TAG, "Location permission not granted, cannot start updates");
            return;
        }

        try {
            // Request updates from both GPS and Network providers
            if (locationManager.isProviderEnabled(LocationManager.GPS_PROVIDER)) {
                locationManager.requestLocationUpdates(
                        LocationManager.GPS_PROVIDER,
                        MIN_TIME_BETWEEN_UPDATES,
                        MIN_DISTANCE_CHANGE,
                        locationListener);
                Log.d(TAG, "Started GPS location updates");
            }

            if (locationManager.isProviderEnabled(LocationManager.NETWORK_PROVIDER)) {
                locationManager.requestLocationUpdates(
                        LocationManager.NETWORK_PROVIDER,
                        MIN_TIME_BETWEEN_UPDATES,
                        MIN_DISTANCE_CHANGE,
                        locationListener);
                Log.d(TAG, "Started Network location updates");
            }
        } catch (Exception e) {
            Log.e(TAG, "Error starting location updates: " + e.getMessage());
        }
    }

    public void stopLocationUpdates() {
        if (locationManager != null && locationListener != null) {
            try {
                locationManager.removeUpdates(locationListener);
                android.util.Log.d(TAG, "Stopped location updates");
            } catch (SecurityException e) {
                android.util.Log.e(TAG, "Security exception stopping location updates: " + e.getMessage());
            } catch (Exception e) {
                android.util.Log.e(TAG, "Error stopping location updates: " + e.getMessage());
            }
        }
    }

    @SuppressLint("MissingPermission")
    public Location getLastKnownLocation() {
        if (!hasLocationPermission()) {
            Log.e(TAG, "Location permission not granted");
            return null;
        }

        // First check if we have a recent location from our listener
        if (currentLocation != null) {
            long age = System.currentTimeMillis() - currentLocation.getTime();
            if (age < 60000) { // Less than 1 minute old
                Log.d(TAG, "Using current location from listener: " + currentLocation.getLatitude() + ", " +
                        currentLocation.getLongitude() + " (age: " + age + "ms)");
                return currentLocation;
            }
        }

        Location bestLocation = currentLocation; // Start with what we have

        // Try GPS provider first (most accurate)
        try {
            Location gpsLocation = locationManager.getLastKnownLocation(LocationManager.GPS_PROVIDER);
            if (gpsLocation != null) {
                Log.d(TAG, "GPS location: " + gpsLocation.getLatitude() + ", " + gpsLocation.getLongitude() + " (age: "
                        + (System.currentTimeMillis() - gpsLocation.getTime()) + "ms)");
                if (bestLocation == null || gpsLocation.getTime() > bestLocation.getTime()) {
                    bestLocation = gpsLocation;
                }
            } else {
                Log.d(TAG, "GPS location is null");
            }
        } catch (Exception e) {
            Log.e(TAG, "Error getting GPS location: " + e.getMessage());
        }

        // Try Network provider as fallback
        try {
            Location networkLocation = locationManager.getLastKnownLocation(LocationManager.NETWORK_PROVIDER);
            if (networkLocation != null) {
                Log.d(TAG, "Network location: " + networkLocation.getLatitude() + ", " + networkLocation.getLongitude()
                        + " (age: " + (System.currentTimeMillis() - networkLocation.getTime()) + "ms)");
                if (bestLocation == null || networkLocation.getTime() > bestLocation.getTime()) {
                    bestLocation = networkLocation;
                }
            } else {
                Log.d(TAG, "Network location is null");
            }
        } catch (Exception e) {
            Log.e(TAG, "Error getting network location: " + e.getMessage());
        }

        // Try Fused provider (if available)
        try {
            Location fusedLocation = locationManager.getLastKnownLocation(LocationManager.FUSED_PROVIDER);
            if (fusedLocation != null) {
                Log.d(TAG, "Fused location: " + fusedLocation.getLatitude() + ", " + fusedLocation.getLongitude()
                        + " (age: " + (System.currentTimeMillis() - fusedLocation.getTime()) + "ms)");
                if (bestLocation == null || fusedLocation.getTime() > bestLocation.getTime()) {
                    bestLocation = fusedLocation;
                }
            } else {
                Log.d(TAG, "Fused location is null");
            }
        } catch (Exception e) {
            Log.e(TAG, "Error getting fused location: " + e.getMessage());
        }

        if (bestLocation == null) {
            Log.w(TAG, "No location available from any provider");
        } else {
            Log.i(TAG, "Using location: " + bestLocation.getLatitude() + ", " + bestLocation.getLongitude()
                    + " from provider: " + bestLocation.getProvider());
            currentLocation = bestLocation; // Update our cache
        }

        return bestLocation;
    }

    public String getLastKnownLocationString() {
        Location location = getLastKnownLocation();
        if (location != null) {
            return String.format(Locale.US, "%.6f, %.6f", location.getLatitude(), location.getLongitude());
        }
        return "No GPS fix";
    }
}