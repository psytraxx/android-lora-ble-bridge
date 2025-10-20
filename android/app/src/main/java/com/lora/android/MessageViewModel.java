package com.lora.android;

import android.location.Location;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import androidx.lifecycle.LiveData;
import androidx.lifecycle.MutableLiveData;
import androidx.lifecycle.ViewModel;

import java.util.Locale;

import lora.Protocol;

public class MessageViewModel extends ViewModel {

    private static final String TAG = "MessageViewModel";
    private static final int MAX_TEXT_LENGTH = 50;

    private final MutableLiveData<String> connectionStatus = new MutableLiveData<>("Initializing...");
    private final MutableLiveData<String> gpsDisplay = new MutableLiveData<>();

    private BleManager bleManager;
    private GpsManager gpsManager;
    private MessageAdapter messageAdapter;
    private byte seqCounter = 0;

    public void setManagers(BleManager bleManager, GpsManager gpsManager, MessageAdapter messageAdapter) {
        this.bleManager = bleManager;
        this.gpsManager = gpsManager;
        this.messageAdapter = messageAdapter;
    }

    public LiveData<String> getConnectionStatus() {
        return connectionStatus;
    }

    public LiveData<String> getGpsDisplay() {
        return gpsDisplay;
    }

    public void updateConnectionStatus(String status) {
        connectionStatus.postValue(status);
    }

    public void updateGps() {
        Location location = gpsManager.getLastKnownLocation();
        if (location != null) {
            String gpsText = String.format(Locale.US, "%.6f, %.6f (%s)",
                    location.getLatitude(),
                    location.getLongitude(),
                    location.getProvider());
            gpsDisplay.postValue(gpsText);
            Log.d(TAG, "GPS display updated: " + gpsText);
        } else {
            gpsDisplay.postValue("No GPS fix");
            Log.w(TAG, "No GPS location to display");
        }
    }

    public boolean canSendMessage() {
        return bleManager != null && bleManager.isConnected();
    }

    public void sendMessage(String text) {
        Log.d(TAG, "Send message - text: " + text);

        if (!canSendMessage()) {
            Log.e(TAG, "Cannot send: not connected");
            return;
        }

        // Enforce maximum text length
        if (text.length() > MAX_TEXT_LENGTH) {
            text = text.substring(0, MAX_TEXT_LENGTH);
        }

        // Validate characters
        if (!Protocol.isTextSupported(text)) {
            Log.e(TAG, "Invalid characters in message");
            return;
        }

        // Update GPS
        updateGps();
        Location location = gpsManager.getLastKnownLocation();

        try {
            // Send text message
            final byte textSeq = seqCounter++;
            Protocol.TextMessage textMsg = new Protocol.TextMessage(textSeq, text);
            bleManager.sendMessage(textMsg);

            // Add to message adapter
            messageAdapter.addMessage(text, true, textSeq);

            // Send GPS if available
            if (location != null) {
                final int lat = (int) (location.getLatitude() * 1_000_000);
                final int lon = (int) (location.getLongitude() * 1_000_000);
                final byte gpsSeq = seqCounter++;

                // Small delay
                new Handler(Looper.getMainLooper()).postDelayed(() -> {
                    Protocol.GpsMessage gpsMsg = new Protocol.GpsMessage(gpsSeq, lat, lon);
                    bleManager.sendMessage(gpsMsg);

                    double latDisplay = lat / 1_000_000.0;
                    double lonDisplay = lon / 1_000_000.0;
                    messageAdapter.addMessage(String.format(Locale.US, "📍 GPS: %.6f, %.6f", latDisplay, lonDisplay),
                            true, gpsSeq);
                }, 100);
            }

        } catch (Exception e) {
            Log.e(TAG, "Error sending message: " + e.getMessage());
        }
    }

    private void handleReceivedMessage(Protocol.Message message) {
        if (message instanceof Protocol.TextMessage textMsg) {
            Log.d(TAG, "Text message received: " + textMsg.text);
            messageAdapter.addMessage(textMsg.text, false, textMsg.seq);
        } else if (message instanceof Protocol.GpsMessage gpsMsg) {
            Log.d(TAG, "GPS message received: lat=" + gpsMsg.lat + ", lon=" + gpsMsg.lon);
            double lat = gpsMsg.lat / 1_000_000.0;
            double lon = gpsMsg.lon / 1_000_000.0;
            messageAdapter.addMessage(String.format(Locale.US, "📍 GPS: %.6f, %.6f", lat, lon), false, gpsMsg.seq);
        } else if (message instanceof Protocol.AckMessage ackMsg) {
            Log.d(TAG, "ACK received for seq: " + ackMsg.seq);
            messageAdapter.updateAckStatus(ackMsg.seq, MessageAdapter.AckStatus.DELIVERED);
        }
    }
}