package com.lora.android;

import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class MessageAdapter extends RecyclerView.Adapter<MessageAdapter.MessageViewHolder> {

    private static final Pattern GPS_PATTERN = Pattern.compile("📍 GPS: (-?\\d+\\.\\d+), (-?\\d+\\.\\d+)");
    
    private final List<ChatMessage> messages = new ArrayList<>();
    private final SimpleDateFormat timeFormat = new SimpleDateFormat("HH:mm", Locale.getDefault());
    private final android.os.Handler mainHandler = new android.os.Handler(android.os.Looper.getMainLooper());
    private ScrollCallback scrollCallback;

    public interface ScrollCallback {
        void onMessageAdded();
    }

    public void setScrollCallback(ScrollCallback callback) {
        this.scrollCallback = callback;
    }

    @NonNull
    @Override
    public MessageViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        View view = LayoutInflater.from(parent.getContext())
                .inflate(R.layout.message_item, parent, false);
        return new MessageViewHolder(view);
    }

    @Override
    public void onBindViewHolder(@NonNull MessageViewHolder holder, int position) {
        ChatMessage message = messages.get(position);

        holder.messageText.setText(message.text);
        holder.messageTime.setText(timeFormat.format(new Date(message.timestamp)));

        // Check if this is a GPS message and make it clickable
        Matcher gpsMatcher = GPS_PATTERN.matcher(message.text);
        if (gpsMatcher.find()) {
            String messageText = message.text; // Capture for lambda
            holder.messageContainer.setOnClickListener(v -> openGoogleMaps(v.getContext(), messageText));
            holder.messageContainer.setClickable(true);
            holder.messageContainer.setFocusable(true);
        } else {
            holder.messageContainer.setOnClickListener(null);
            holder.messageContainer.setClickable(false);
            holder.messageContainer.setFocusable(false);
        }

        // Align message bubble based on sender
        FrameLayout.LayoutParams params = (FrameLayout.LayoutParams) holder.messageContainer.getLayoutParams();

        if (message.isSent) {
            // Sent messages: align right, green background
            params.gravity = Gravity.END;
            holder.messageContainer.setBackgroundResource(R.drawable.message_bubble_sent);
            holder.messageText.setTextColor(0xFF000000); // Black text

            // Show ACK status indicator
            holder.ackStatusIcon.setVisibility(View.VISIBLE);
            switch (message.ackStatus) {
                case PENDING:
                    holder.ackStatusIcon.setText("⏱"); // Clock for pending
                    holder.ackStatusIcon.setTextColor(0xFF999999); // Gray
                    break;
                case DELIVERED:
                    holder.ackStatusIcon.setText("✓"); // Checkmark for delivered
                    holder.ackStatusIcon.setTextColor(0xFF4CAF50); // Green
                    break;
                default:
                    holder.ackStatusIcon.setVisibility(View.GONE);
                    break;
            }
        } else {
            // Received messages: align left, white background with border
            params.gravity = Gravity.START;
            holder.messageContainer.setBackgroundResource(R.drawable.message_bubble_received);
            holder.messageText.setTextColor(0xFF000000); // Black text
            holder.ackStatusIcon.setVisibility(View.GONE); // No ACK indicator for received messages
        }

        holder.messageContainer.setLayoutParams(params);
    }

    @Override
    public int getItemCount() {
        return messages.size();
    }

    public void addMessage(String text, boolean isSent, byte seq) {
        messages.add(new ChatMessage(text, isSent, seq));
        notifyItemInserted(messages.size() - 1);
        if (scrollCallback != null) {
            scrollCallback.onMessageAdded();
        }
    }

    public void updateAckStatus(byte seq, AckStatus status) {
        // Ensure we're on main thread
        if (android.os.Looper.myLooper() != android.os.Looper.getMainLooper()) {
            mainHandler.post(() -> updateAckStatus(seq, status));
            return;
        }

        for (int i = 0; i < messages.size(); i++) {
            ChatMessage msg = messages.get(i);
            if (msg.isSent && msg.seq == seq) {
                msg.ackStatus = status;
                notifyItemChanged(i);
                break; // Exit once we find the message
            }
        }
    }

    public void clear() {
        messages.clear();
        notifyDataSetChanged();
    }

    private void openGoogleMaps(Context context, String messageText) {
        try {
            Matcher matcher = GPS_PATTERN.matcher(messageText);
            if (matcher.find()) {
                double latitude = Double.parseDouble(matcher.group(1));
                double longitude = Double.parseDouble(matcher.group(2));
                
                // Create Google Maps intent with marker
                Uri gmmIntentUri = Uri.parse("geo:" + latitude + "," + longitude + "?q=" + latitude + "," + longitude);
                Intent mapIntent = new Intent(Intent.ACTION_VIEW, gmmIntentUri);
                mapIntent.setPackage("com.google.android.apps.maps");
                
                // Check if Google Maps is installed
                if (mapIntent.resolveActivity(context.getPackageManager()) != null) {
                    context.startActivity(mapIntent);
                } else {
                    // Fallback to browser if Google Maps not installed
                    Uri browserUri = Uri.parse("https://www.google.com/maps/search/?api=1&query=" + latitude + "," + longitude);
                    Intent browserIntent = new Intent(Intent.ACTION_VIEW, browserUri);
                    context.startActivity(browserIntent);
                }
            }
        } catch (Exception e) {
            Toast.makeText(context, "Error opening location: " + e.getMessage(), Toast.LENGTH_SHORT).show();
        }
    }

    public enum AckStatus {
        NONE, // Not applicable (received messages)
        PENDING, // Sent, waiting for ACK
        DELIVERED // ACK received
    }

    public static class ChatMessage {
        public final String text;
        public final boolean isSent; // true = sent by user, false = received
        public final long timestamp;
        public final byte seq; // Sequence number for matching ACKs
        public AckStatus ackStatus;

        public ChatMessage(String text, boolean isSent, byte seq) {
            this.text = text;
            this.isSent = isSent;
            this.timestamp = System.currentTimeMillis();
            this.seq = seq;
            this.ackStatus = isSent ? AckStatus.PENDING : AckStatus.NONE;
        }
    }

    static class MessageViewHolder extends RecyclerView.ViewHolder {
        final LinearLayout messageContainer;
        final TextView messageText;
        final TextView messageTime;
        final TextView ackStatusIcon;

        MessageViewHolder(@NonNull View itemView) {
            super(itemView);
            messageContainer = itemView.findViewById(R.id.messageContainer);
            messageText = itemView.findViewById(R.id.messageText);
            messageTime = itemView.findViewById(R.id.messageTime);
            ackStatusIcon = itemView.findViewById(R.id.ackStatusIcon);
        }
    }
}
