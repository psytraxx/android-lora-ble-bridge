package com.lora.android;

import android.content.Context;
import android.os.Bundle;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.lifecycle.ViewModelProvider;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import java.util.Locale;

import lora.Protocol;

public class MainActivity extends AppCompatActivity {

    private static final int REQUEST_PERMISSIONS = 1;
    private static final long GPS_UPDATE_INTERVAL_MS = 5000; // 5 seconds
    private static final double CHAR_COUNT_WARNING_THRESHOLD = 0.9; // 90% of max

    private EditText messageEditText;
    private Button sendButton;
    private RecyclerView messagesRecyclerView;
    private MessageAdapter messageAdapter;
    private TextView gpsTextView;
    private TextView charCountTextView;
    private TextView connectionStatusTextView;

    private MessageViewModel messageViewModel;
    private BleManager bleManager;
    private GpsManager gpsManager;
    
    private android.os.Handler gpsHandler;
    private Runnable gpsUpdateRunnable;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        // Set up Action Bar title
        if (getSupportActionBar() != null) {
            getSupportActionBar().setTitle("LoRa Chat");
        }

        messageEditText = findViewById(R.id.messageEditText);
        sendButton = findViewById(R.id.sendButton);
        messagesRecyclerView = findViewById(R.id.messagesRecyclerView);
        gpsTextView = findViewById(R.id.gpsTextView);
        charCountTextView = findViewById(R.id.charCountTextView);
        connectionStatusTextView = findViewById(R.id.connectionStatusTextView);

        // Initialize managers
        messageAdapter = new MessageAdapter();

        // Set scroll callback to auto-scroll when messages are added
        messageAdapter.setScrollCallback(() -> messagesRecyclerView.post(() -> {
            if (messageAdapter.getItemCount() > 0) {
                messagesRecyclerView.smoothScrollToPosition(messageAdapter.getItemCount() - 1);
            }
        }));

        messageViewModel = new ViewModelProvider(this).get(MessageViewModel.class);
        bleManager = new BleManager(this);
        gpsManager = new GpsManager(this);

        messageViewModel.setManagers(bleManager, gpsManager, messageAdapter);

        // Set up RecyclerView
        messagesRecyclerView.setLayoutManager(new LinearLayoutManager(this));
        messagesRecyclerView.setAdapter(messageAdapter);

        // Observe ViewModel
        messageViewModel.getGpsDisplay().observe(this, gps -> gpsTextView.setText(gps));
        messageViewModel.getShowToast().observe(this, message -> {
            if (message != null) {
                Toast.makeText(MainActivity.this, message, Toast.LENGTH_LONG).show();
            }
        });

        // Observe BLE connection state for send button and status
        bleManager.getConnectionStatus().observe(this, status -> connectionStatusTextView.setText(status));
        bleManager.getConnected().observe(this,
                connected -> sendButton.setEnabled(connected != null ? connected : false));

        checkPermissions();

        sendButton.setOnClickListener(v -> {
            String messageText = messageEditText.getText().toString();
            if (!messageText.isEmpty()) {
                messageViewModel.sendMessage(messageText);
                messageEditText.setText("");
            }
        });
        sendButton.setEnabled(false); // Disabled until connected

        // Add text watcher to update character count
        messageEditText.addTextChangedListener(new android.text.TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {
            }

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
                updateCharCount(s.toString());
            }

            @Override
            public void afterTextChanged(android.text.Editable s) {
            }
        });

        messageEditText.setOnEditorActionListener((v, actionId, event) -> {
            android.util.Log.d("LoRaApp", "EditorAction - actionId: " + actionId + ", event: " + event);

            // Handle any action - Done, Send, Unspecified, etc.
            if (actionId == EditorInfo.IME_ACTION_DONE ||
                    actionId == EditorInfo.IME_ACTION_SEND ||
                    actionId == EditorInfo.IME_ACTION_GO ||
                    actionId == EditorInfo.IME_NULL) {
                dismissKeyboard();
                return true;
            }
            return false;
        });

        messageViewModel.updateGps();
        updateCharCount(""); // Initialize counter

        startGpsPeriodicUpdates();
    }

    private void checkPermissions() {
        if (!PermissionHelper.hasAllPermissions(this)) {
            PermissionHelper.requestPermissions(this, REQUEST_PERMISSIONS);
        } else {
            startBleScan();
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions,
            @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_PERMISSIONS) {
            if (PermissionHelper.areAllPermissionsGranted(grantResults)) {
                gpsManager.startLocationUpdates(); // Start GPS updates when permissions granted
                startBleScan();
            } else {
                Toast.makeText(this, "Permissions required", Toast.LENGTH_SHORT).show();
            }
        }
    }

    private void startBleScan() {
        bleManager.startScan();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (gpsHandler != null && gpsUpdateRunnable != null) {
            gpsHandler.removeCallbacks(gpsUpdateRunnable);
        }
        if (gpsManager != null) {
            gpsManager.stopLocationUpdates();
        }
        if (bleManager != null) {
            bleManager.disconnect();
        }
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (gpsHandler != null && gpsUpdateRunnable != null) {
            gpsHandler.removeCallbacks(gpsUpdateRunnable);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (gpsHandler != null && gpsUpdateRunnable != null) {
            gpsHandler.postDelayed(gpsUpdateRunnable, GPS_UPDATE_INTERVAL_MS);
        }
    }

    private void dismissKeyboard() {
        InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
        if (imm != null && getCurrentFocus() != null) {
            imm.hideSoftInputFromWindow(getCurrentFocus().getWindowToken(), 0);
        }
        if (messageEditText != null) {
            messageEditText.clearFocus();
        }
    }

    private void updateCharCount(String text) {
        if (text == null) text = "";
        int charCount = text.length();
        int packedBytes = Protocol.calculatePackedSize(text);
        int totalMessageSize = 12 + packedBytes; // 12 byte header + packed text

        String countText = charCount + "/" + Protocol.MAX_TEXT_LENGTH + " chars (" + totalMessageSize + " bytes)";
        charCountTextView.setText(countText);

        // Change color if approaching limit
        if (charCount >= Protocol.MAX_TEXT_LENGTH) {
            charCountTextView.setTextColor(0xFFFF0000); // Red
        } else if (charCount >= Protocol.MAX_TEXT_LENGTH * CHAR_COUNT_WARNING_THRESHOLD) {
            charCountTextView.setTextColor(0xFFFF6600); // Orange
        } else {
            charCountTextView.setTextColor(0xFF666666); // Gray
        }
    }

    private void startGpsPeriodicUpdates() {
        gpsHandler = new android.os.Handler(android.os.Looper.getMainLooper());
        gpsUpdateRunnable = new Runnable() {
            @Override
            public void run() {
                messageViewModel.updateGps();
                gpsHandler.postDelayed(this, GPS_UPDATE_INTERVAL_MS);
            }
        };
        gpsHandler.postDelayed(gpsUpdateRunnable, GPS_UPDATE_INTERVAL_MS);
    }
}