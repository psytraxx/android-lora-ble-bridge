package com.lora.android;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.IBinder;
import android.util.Log;

import androidx.core.app.NotificationCompat;
import androidx.lifecycle.Observer;

import lora.Protocol;

/**
 * Foreground service to maintain BLE connection and receive LoRa messages
 * even when app is in background or minimized.
 */
public class LoRaForegroundService extends Service {

    private static final String TAG = "LoRaForegroundService";
    private static final String CHANNEL_ID = "LoRaServiceChannel";
    private static final int NOTIFICATION_ID = 1;
    private static final int MESSAGE_NOTIFICATION_BASE_ID = 100;
    private final Observer<String> connectionStatusObserver = this::updateNotification;
    private BleManager bleManager;
    private int messageNotificationCounter = 0;
    private final Observer<Protocol.Message> messageReceivedObserver = this::handleReceivedMessage;

    @Override
    public void onCreate() {
        super.onCreate();
        Log.d(TAG, "Service created");

        createNotificationChannel();
        bleManager = BleManager.getInstance(this);

        // Observe BLE events
        bleManager.getMessageReceived().observeForever(messageReceivedObserver);
        bleManager.getConnectionStatus().observeForever(connectionStatusObserver);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        Log.d(TAG, "Service started");

        // Create notification and start foreground
        Notification notification = createNotification("LoRa service active", "Waiting for connection...");
        startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE);

        // Start BLE scan only if actively communicating (add logic as needed)
        if (!bleManager.isConnected()) {
            bleManager.startScan();
        }

        // If service is killed, do NOT restart it automatically
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        super.onDestroy();
        Log.d(TAG, "Service destroyed");

        // Clean up observers
        if (bleManager != null) {
            bleManager.getMessageReceived().removeObserver(messageReceivedObserver);
            bleManager.getConnectionStatus().removeObserver(connectionStatusObserver);
            bleManager.disconnect();
        }
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null; // We don't provide binding
    }

    private void createNotificationChannel() {
        NotificationChannel serviceChannel = new NotificationChannel(
                CHANNEL_ID,
                "LoRa Background Service",
                NotificationManager.IMPORTANCE_LOW);
        serviceChannel.setDescription("Maintains BLE connection to receive LoRa messages");

        NotificationManager manager = getSystemService(NotificationManager.class);
        if (manager != null) {
            manager.createNotificationChannel(serviceChannel);
        }
    }

    private Notification createNotification(String title, String text) {
        Intent notificationIntent = new Intent(this, MainActivity.class);
        PendingIntent pendingIntent = PendingIntent.getActivity(
                this,
                0,
                notificationIntent,
                PendingIntent.FLAG_IMMUTABLE);

        return new NotificationCompat.Builder(this, CHANNEL_ID)
                .setContentTitle(title)
                .setContentText(text)
                .setSmallIcon(android.R.drawable.ic_dialog_info)
                .setContentIntent(pendingIntent)
                .setOngoing(true)
                .build();
    }

    private void updateNotification(String status) {
        if (status == null)
            return;

        Log.d(TAG, "Updating notification with status: " + status);
        Notification notification = createNotification("LoRa Service", status);

        NotificationManager manager = getSystemService(NotificationManager.class);
        if (manager != null) {
            manager.notify(NOTIFICATION_ID, notification);
        }
    }

    private void handleReceivedMessage(Protocol.Message message) {
        if (message == null)
            return;

        Log.d(TAG, "Message received in background service");

        if (message instanceof Protocol.TextMessage textMsg) {
            // Show notification for new message
            showMessageNotification(textMsg);
        } else if (message instanceof Protocol.AckMessage ackMsg) {
            Log.d(TAG, "ACK received for seq: " + ackMsg.seq);
            // Could show subtle notification for ACK if desired
        }
    }

    private void showMessageNotification(Protocol.TextMessage textMsg) {
        Intent notificationIntent = new Intent(this, MainActivity.class);
        notificationIntent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);

        PendingIntent pendingIntent = PendingIntent.getActivity(
                this,
                0,
                notificationIntent,
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);

        String notificationText = textMsg.text;
        if (textMsg.hasGps) {
            notificationText += " 📍";
        }

        NotificationCompat.Builder builder = new NotificationCompat.Builder(this, CHANNEL_ID)
                .setSmallIcon(android.R.drawable.ic_dialog_email)
                .setContentTitle("New LoRa Message")
                .setContentText(notificationText)
                .setStyle(new NotificationCompat.BigTextStyle().bigText(notificationText))
                .setPriority(NotificationCompat.PRIORITY_HIGH)
                .setAutoCancel(true)
                .setContentIntent(pendingIntent);

        NotificationManager manager = getSystemService(NotificationManager.class);
        if (manager != null) {
            int notificationId = MESSAGE_NOTIFICATION_BASE_ID + (messageNotificationCounter++);
            manager.notify(notificationId, builder.build());
        }
    }
}
