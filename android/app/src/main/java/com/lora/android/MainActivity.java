package com.lora.android;

import android.content.Context;
import android.os.Bundle;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.lifecycle.ViewModelProvider;
import androidx.recyclerview.widget.LinearLayoutManager;

import com.lora.android.databinding.ActivityMainBinding;

import lora.Protocol;

public class MainActivity extends AppCompatActivity {

    private static final int REQUEST_PERMISSIONS = 1;
    private static final double CHAR_COUNT_WARNING_THRESHOLD = 0.9; // 90% of max

    private ActivityMainBinding binding;

    private MessageAdapter messageAdapter;

    private MessageViewModel messageViewModel;
    private BleManager bleManager;
    private GpsManager gpsManager;

    private String pendingMessage = null; // Message to send after reconnection

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());

        // Ensure status bar is visible and icons are dark
        WindowInsetsController controller = getWindow().getInsetsController();
        if (controller != null) {
            controller.show(WindowInsets.Type.statusBars());
            controller.setSystemBarsAppearance(WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS,
                    WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS);
        }

        // Set up Action Bar title
        if (getSupportActionBar() != null) {
            getSupportActionBar().setTitle("LoRa Chat");
        }

        // Initialize managers
        messageAdapter = new MessageAdapter();

        // Set scroll callback to auto-scroll when messages are added
        messageAdapter.setScrollCallback(() -> binding.messagesRecyclerView.post(() -> {
            if (messageAdapter.getItemCount() > 0) {
                binding.messagesRecyclerView.smoothScrollToPosition(messageAdapter.getItemCount() - 1);
            }
        }));

        messageViewModel = new ViewModelProvider(this).get(MessageViewModel.class);
        bleManager = BleManager.getInstance(this);
        gpsManager = new GpsManager(this);

        messageViewModel.setManagers(bleManager, gpsManager, messageAdapter);

        // Set up RecyclerView
        binding.messagesRecyclerView.setLayoutManager(new LinearLayoutManager(this));
        binding.messagesRecyclerView.setAdapter(messageAdapter);

        // Observe ViewModel
        messageViewModel.getGpsDisplay().observe(this, gps -> binding.gpsTextView.setText(gps));
        messageViewModel.getShowToast().observe(this, message -> {
            if (message != null) {
                Toast.makeText(MainActivity.this, message, Toast.LENGTH_LONG).show();
            }
        });

        // Observe BLE connection state for send button and status
        bleManager.getConnectionStatus().observe(this, status -> binding.connectionStatusTextView.setText(status));
        bleManager.getConnected().observe(this, connected -> {
            // Update button enabled state based on connection and text content
            updateSendButtonState();

            boolean isConnected = connected != null ? connected : false;

            // Auto-send pending message after reconnection
            if (isConnected && pendingMessage != null) {
                String messageToSend = pendingMessage;
                pendingMessage = null; // Clear pending message

                // Request fresh GPS and send
                gpsManager.requestSingleLocationUpdate();
                messageViewModel.sendMessage(messageToSend);
                binding.messageEditText.setText("");
                dismissKeyboard();
                Toast.makeText(this, "Message sent!", Toast.LENGTH_SHORT).show();
            }
        });

        // Observe canSendNewMessage state to enable/disable button
        messageViewModel.getCanSendNewMessage().observe(this, canSend -> {
            if (canSend != null && !canSend) {
                // Disable button while waiting for ACK
                binding.sendButton.setEnabled(false);
            } else {
                // Re-enable based on text content
                updateSendButtonState();
            }
        });

        checkPermissions();

        binding.sendButton.setOnClickListener(v -> {
            String messageText = binding.messageEditText.getText().toString().trim();

            // Don't do anything if message is empty
            if (messageText.isEmpty()) {
                return;
            }

            // Check if BLE is connected
            Boolean isConnected = bleManager.getConnected().getValue();
            if (isConnected == null || !isConnected) {
                // Not connected - queue message and initiate reconnection
                pendingMessage = messageText;
                Toast.makeText(this, "Reconnecting...", Toast.LENGTH_SHORT).show();
                bleManager.connect();
                return;
            }

            // Connected - send message normally
            // Request fresh GPS location when user sends message (event-driven)
            gpsManager.requestSingleLocationUpdate();
            messageViewModel.sendMessage(messageText);
            binding.messageEditText.setText("");
            dismissKeyboard();
        });

        // Add text watcher to update character count and button state
        binding.messageEditText.addTextChangedListener(new android.text.TextWatcher() {
            @Override
            public void beforeTextChanged(CharSequence s, int start, int count, int after) {
            }

            @Override
            public void onTextChanged(CharSequence s, int start, int before, int count) {
                updateCharCount(s.toString());
                updateSendButtonState();
            }

            @Override
            public void afterTextChanged(android.text.Editable s) {
            }
        });

        binding.messageEditText.setOnEditorActionListener((v, actionId, event) -> {
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

        // Get initial GPS location (event-driven, not periodic)
        messageViewModel.updateGps();
        updateCharCount(""); // Initialize counter
        updateSendButtonState(); // Initialize send button state (disabled when empty)
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
                // Note: GPS updates are now event-driven, not started automatically
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
        if (gpsManager != null) {
            gpsManager.stopLocationUpdates();
        }
        if (bleManager != null) {
            bleManager.disconnect();
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        // Validate BLE connection state when app comes to foreground
        // This ensures UI accurately reflects actual connection state
        if (bleManager != null) {
            bleManager.validateConnectionState();
        }

        // Refresh GPS display when app comes to foreground
        if (gpsManager != null && messageViewModel != null) {
            messageViewModel.updateGps();
        }
    }

    private void dismissKeyboard() {
        InputMethodManager imm = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);
        if (imm != null && getCurrentFocus() != null) {
            imm.hideSoftInputFromWindow(getCurrentFocus().getWindowToken(), 0);
        }
        binding.messageEditText.clearFocus();
    }

    private void updateCharCount(String text) {
        if (text == null)
            text = "";
        int charCount = text.length();
        int packedBytes = Protocol.calculatePackedSize(text);
        int totalMessageSize = 12 + packedBytes; // 12 byte header + packed text

        String countText = charCount + "/" + Protocol.MAX_TEXT_LENGTH + " chars (" + totalMessageSize + " bytes)";
        binding.charCountTextView.setText(countText);

        // Change color if approaching limit
        if (charCount >= Protocol.MAX_TEXT_LENGTH) {
            binding.charCountTextView.setTextColor(
                    androidx.core.content.ContextCompat.getColor(this, R.color.char_count_exceeded));
        } else if (charCount >= Protocol.MAX_TEXT_LENGTH * CHAR_COUNT_WARNING_THRESHOLD) {
            binding.charCountTextView.setTextColor(
                    androidx.core.content.ContextCompat.getColor(this, R.color.char_count_warning));
        } else {
            binding.charCountTextView.setTextColor(
                    androidx.core.content.ContextCompat.getColor(this, R.color.char_count_normal));
        }
    }

    private void updateSendButtonState() {
        // Enable send button only if there's text to send and can send new message
        String text = binding.messageEditText.getText().toString().trim();
        Boolean canSend = messageViewModel.getCanSendNewMessage().getValue();
        boolean canSendValue = canSend != null && canSend;
        binding.sendButton.setEnabled(!text.isEmpty() && canSendValue);
    }
}
