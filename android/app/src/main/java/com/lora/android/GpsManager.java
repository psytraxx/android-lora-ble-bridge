package com.lora.android;

import android.Manifest;
import android.annotation.SuppressLint;
import android.content.Context;
import android.content.pm.PackageManager;
import android.location.Location;
import android.location.LocationManager;
import android.util.Log;

import androidx.core.app.ActivityCompat;

public class GpsManager {

    private static final String TAG = "GpsManager";

    private final Context context;
    private final LocationManager locationManager;

    public GpsManager(Context context) {
        this.context = context;
        this.locationManager = (LocationManager) context.getSystemService(Context.LOCATION_SERVICE);
    }

    public boolean hasLocationPermission() {
        return ActivityCompat.checkSelfPermission(context,
                Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED;
    }

    @SuppressLint("MissingPermission")
    public Location getLastKnownLocation() {
        if (!hasLocationPermission()) {
            Log.e(TAG, "Location permission not granted");
            return null;
        }

        Location bestLocation = null;

        // Try GPS provider first (most accurate)
        try {
            Location gpsLocation = locationManager.getLastKnownLocation(LocationManager.GPS_PROVIDER);
            if (gpsLocation != null) {
                Log.d(TAG, "GPS location: " + gpsLocation.getLatitude() + ", " + gpsLocation.getLongitude() + " (age: "
                        + (System.currentTimeMillis() - gpsLocation.getTime()) + "ms)");
                bestLocation = gpsLocation;
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
        }

        return bestLocation;
    }
}