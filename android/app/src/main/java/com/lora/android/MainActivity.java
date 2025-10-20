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
    private static final int MAX_TEXT_LENGTH = 50; // Maximum text length for optimal LoRa range

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
        messageViewModel = new ViewModelProvider(this).get(MessageViewModel.class);
        bleManager = new BleManager(this, new BleManager.BleCallback() {
            @Override
            public void onConnectionStatusChanged(String status) {
                messageViewModel.updateConnectionStatus(status);
            }

            @Override
            public void onConnected() {
                runOnUiThread(() -> sendButton.setEnabled(true));
            }

            @Override
            public void onDisconnected() {
                runOnUiThread(() -> sendButton.setEnabled(false));
            }

            @Override
            public void onMessageReceived(Protocol.Message message) {
                runOnUiThread(() -> {
                    if (message instanceof Protocol.TextMessage textMsg) {
                        messageAdapter.addMessage(textMsg.text, false, textMsg.seq);
                        messagesRecyclerView.smoothScrollToPosition(messageAdapter.getItemCount() - 1);
                    } else if (message instanceof Protocol.GpsMessage gpsMsg) {
                        double lat = gpsMsg.lat / 1_000_000.0;
                        double lon = gpsMsg.lon / 1_000_000.0;
                        messageAdapter.addMessage(String.format(Locale.US, "📍 GPS: %.6f, %.6f", lat, lon), false,
                                gpsMsg.seq);
                        messagesRecyclerView.smoothScrollToPosition(messageAdapter.getItemCount() - 1);
                    } else if (message instanceof Protocol.AckMessage ackMsg) {
                        messageAdapter.updateAckStatus(ackMsg.seq, MessageAdapter.AckStatus.DELIVERED);
                        Toast.makeText(MainActivity.this, "✓ Message delivered", Toast.LENGTH_SHORT).show();
                    }
                });
            }
        });
        gpsManager = new GpsManager(this);

        messageViewModel.setManagers(bleManager, gpsManager, messageAdapter);

        // Set up RecyclerView
        messagesRecyclerView.setLayoutManager(new LinearLayoutManager(this));
        messagesRecyclerView.setAdapter(messageAdapter);

        // Observe ViewModel
        messageViewModel.getConnectionStatus().observe(this, status -> connectionStatusTextView.setText(status));
        messageViewModel.getGpsDisplay().observe(this, gps -> gpsTextView.setText(gps));

        checkPermissions();

        sendButton.setOnClickListener(v -> {
            messageViewModel.sendMessage(messageEditText.getText().toString());
            messageEditText.setText("");
            dismissKeyboard();
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
        messageViewModel.updateConnectionStatus("Initializing...");

        // Update GPS periodically every 5 seconds
        final android.os.Handler gpsHandler = new android.os.Handler(android.os.Looper.getMainLooper());
        gpsHandler.postDelayed(new Runnable() {
            @Override
            public void run() {
                messageViewModel.updateGps();
                gpsHandler.postDelayed(this, 5000); // Update every 5 seconds
            }
        }, 5000);
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
                startBleScan();
            } else {
                Toast.makeText(this, "Permissions required", Toast.LENGTH_SHORT).show();
            }
        }
    }

    private void startBleScan() {
        bleManager.startScan();
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
        int charCount = text.length();
        int packedBytes = Protocol.calculatePackedSize(text);
        int totalMessageSize = 12 + packedBytes; // 12 byte header + packed text

        String countText = charCount + "/" + MAX_TEXT_LENGTH + " chars (" + totalMessageSize + " bytes)";
        charCountTextView.setText(countText);

        // Change color if approaching limit
        if (charCount >= MAX_TEXT_LENGTH) {
            charCountTextView.setTextColor(0xFFFF0000); // Red
        } else if (charCount >= MAX_TEXT_LENGTH * 0.9) {
            charCountTextView.setTextColor(0xFFFF6600); // Orange
        } else {
            charCountTextView.setTextColor(0xFF666666); // Gray
        }
    }
}