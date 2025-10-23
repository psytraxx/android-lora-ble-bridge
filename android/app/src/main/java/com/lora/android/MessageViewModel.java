package com.lora.android;

import android.location.Location;
import android.util.Log;

import androidx.lifecycle.LiveData;
import androidx.lifecycle.MutableLiveData;
import androidx.lifecycle.Observer;
import androidx.lifecycle.ViewModel;

import java.util.Locale;

import lora.Protocol;

public class MessageViewModel extends ViewModel {

    private static final String TAG = "MessageViewModel";

    private final MutableLiveData<String> gpsDisplay = new MutableLiveData<>();
    private final MutableLiveData<String> showToast = new MutableLiveData<>();
    private final Observer<String> bleShowToastObserver = showToast::postValue;
    private BleManager bleManager;
    private GpsManager gpsManager;
    private MessageAdapter messageAdapter;
    // Observers for BLE manager
    private final Observer<Protocol.Message> messageReceivedObserver = this::handleReceivedMessage;
    private byte seqCounter = 0;
    private boolean observersRegistered = false;

    public void setManagers(BleManager bleManager, GpsManager gpsManager, MessageAdapter messageAdapter) {
        // Prevent multiple registrations
        if (observersRegistered) {
            Log.w(TAG, "Observers already registered, skipping duplicate registration");
            return;
        }

        this.bleManager = bleManager;
        this.gpsManager = gpsManager;
        this.messageAdapter = messageAdapter;

        // Observe BLE manager LiveData
        if (bleManager != null) {
            bleManager.getMessageReceived().observeForever(messageReceivedObserver);
            bleManager.getShowToast().observeForever(bleShowToastObserver);
            observersRegistered = true;
        } else {
            Log.e(TAG, "BleManager is null, cannot register observers");
        }
    }

    public LiveData<String> getGpsDisplay() {
        return gpsDisplay;
    }

    public LiveData<String> getShowToast() {
        return showToast;
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

        // Enforce maximum text length
        if (text.length() > Protocol.MAX_TEXT_LENGTH) {
            text = text.substring(0, Protocol.MAX_TEXT_LENGTH);
        }

        // Validate characters
        if (!Protocol.isTextSupported(text)) {
            Log.e(TAG, "Invalid characters in message");
            return;
        }

        // Attempt to connect if not connected
        if (bleManager != null && !bleManager.isConnected()) {
            Log.d(TAG, "BLE not connected - attempting to connect...");
            bleManager.connect();
            try {
                Thread.sleep(1500); // wait briefly for connection
            } catch (InterruptedException e) {
                Log.e(TAG, "Interrupted while waiting for BLE connection");
            }
        }

        if (!canSendMessage()) {
            Log.e(TAG, "Cannot send: BLE connection failed");
            return;
        }

        // Update GPS
        updateGps();
        Location location = gpsManager.getLastKnownLocation();

        try {
            // Send unified text message with optional GPS
            final byte textSeq = seqCounter++;
            Protocol.TextMessage textMsg;

            if (location != null) {
                final int lat = (int) (location.getLatitude() * 1_000_000);
                final int lon = (int) (location.getLongitude() * 1_000_000);
                textMsg = new Protocol.TextMessage(textSeq, text, lat, lon);
                messageAdapter.addMessage(text, true, textSeq, true,
                        location.getLatitude(), location.getLongitude());
            } else {
                textMsg = new Protocol.TextMessage(textSeq, text);
                messageAdapter.addMessage(text, true, textSeq);
            }

            bleManager.sendMessage(textMsg);

            // Schedule disconnect after short delay (to await ACK)
            disconnectAfterDelay(5000);

        } catch (Exception e) {
            Log.e(TAG, "Error sending message: " + e.getMessage());
        }
    }

    private void disconnectAfterDelay(long delayMs) {
        new Thread(() -> {
            try {
                Thread.sleep(delayMs);
                if (bleManager != null && bleManager.isConnected()) {
                    Log.d(TAG, "Disconnecting BLE after inactivity timeout");
                    bleManager.disconnect();
                }
            } catch (InterruptedException e) {
                Log.e(TAG, "Disconnect delay interrupted");
            }
        }).start();
    }

    private void handleReceivedMessage(Protocol.Message message) {
        if (message instanceof Protocol.TextMessage textMsg) {
            Log.d(TAG, "Text message received: " + textMsg.text);
            // Display text without GPS coordinates, but store GPS data for Maps click
            if (textMsg.hasGps) {
                double lat = textMsg.lat / 1_000_000.0;
                double lon = textMsg.lon / 1_000_000.0;
                messageAdapter.addMessage(textMsg.text, false, textMsg.seq, true, lat, lon);
            } else {
                messageAdapter.addMessage(textMsg.text, false, textMsg.seq);
            }
        } else if (message instanceof Protocol.AckMessage ackMsg) {
            Log.d(TAG, "ACK received for seq: " + ackMsg.seq);
            messageAdapter.updateAckStatus(ackMsg.seq, MessageAdapter.AckStatus.DELIVERED);
            showToast.postValue("✓ Message delivered (seq " + ackMsg.seq + ")");
        }
    }

    @Override
    protected void onCleared() {
        super.onCleared();
        // Safe cleanup with null checks
        if (bleManager != null && observersRegistered) {
            try {
                bleManager.getMessageReceived().removeObserver(messageReceivedObserver);
                bleManager.getShowToast().removeObserver(bleShowToastObserver);
                observersRegistered = false;
                Log.d(TAG, "Observers successfully removed");
            } catch (Exception e) {
                Log.e(TAG, "Error removing observers: " + e.getMessage());
            }
        }
    }
}
