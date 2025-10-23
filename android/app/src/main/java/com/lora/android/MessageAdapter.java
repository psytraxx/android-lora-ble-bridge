package com.lora.android;

import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.LinearLayout;
import android.widget.RelativeLayout;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

public class MessageAdapter extends RecyclerView.Adapter<MessageAdapter.MessageViewHolder> {
    private final List<ChatMessage> messages = new ArrayList<>();
    private final SimpleDateFormat timeFormat = new SimpleDateFormat("HH:mm", Locale.getDefault());
    private final android.os.Handler mainHandler = new android.os.Handler(android.os.Looper.getMainLooper());
    private ScrollCallback scrollCallback;

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

        // Make message clickable if it has GPS coordinates
        if (message.hasGps) {
            holder.messageContainer.setOnClickListener(v -> openGoogleMaps(v.getContext(), message.latitude, message.longitude));
            holder.messageContainer.setClickable(true);
            holder.messageContainer.setFocusable(true);
        } else {
            holder.messageContainer.setOnClickListener(null);
            holder.messageContainer.setClickable(false);
            holder.messageContainer.setFocusable(false);
        }

        // Align message bubble based on sender
        RelativeLayout.LayoutParams params = (RelativeLayout.LayoutParams) holder.messageContainer.getLayoutParams();

        Context context = holder.itemView.getContext();

        if (message.isSent) {
            // Sent messages: align right, green background
            params.removeRule(RelativeLayout.ALIGN_PARENT_START);
            params.addRule(RelativeLayout.ALIGN_PARENT_END);
            holder.messageContainer.setBackgroundResource(R.drawable.message_bubble_sent);
            holder.messageText.setTextColor(androidx.core.content.ContextCompat.getColor(context, R.color.message_text));

            // Show ACK status indicator
            holder.ackStatusIcon.setVisibility(View.VISIBLE);
            switch (message.ackStatus) {
                case PENDING:
                    holder.ackStatusIcon.setText("⏱"); // Clock for pending
                    holder.ackStatusIcon.setTextColor(androidx.core.content.ContextCompat.getColor(context, R.color.ack_pending));
                    break;
                case DELIVERED:
                    holder.ackStatusIcon.setText("✓"); // Checkmark for delivered
                    holder.ackStatusIcon.setTextColor(androidx.core.content.ContextCompat.getColor(context, R.color.ack_delivered));
                    break;
                default:
                    holder.ackStatusIcon.setVisibility(View.GONE);
                    break;
            }
        } else {
            // Received messages: align left, white background with border
            params.removeRule(RelativeLayout.ALIGN_PARENT_END);
            params.addRule(RelativeLayout.ALIGN_PARENT_START);
            holder.messageContainer.setBackgroundResource(R.drawable.message_bubble_received);
            holder.messageText.setTextColor(androidx.core.content.ContextCompat.getColor(context, R.color.message_text));
            holder.ackStatusIcon.setVisibility(View.GONE); // No ACK indicator for received messages
        }

        holder.messageContainer.setLayoutParams(params);
    }

    @Override
    public int getItemCount() {
        return messages.size();
    }

    public void addMessage(String text, boolean isSent, byte seq) {
        addMessage(text, isSent, seq, false, 0.0, 0.0);
    }

    public void addMessage(String text, boolean isSent, byte seq, boolean hasGps, double latitude, double longitude) {
        messages.add(new ChatMessage(text, isSent, seq, hasGps, latitude, longitude));
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

    private void openGoogleMaps(Context context, double latitude, double longitude) {
        try {
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
        } catch (Exception e) {
            Toast.makeText(context, "Error opening location: " + e.getMessage(), Toast.LENGTH_SHORT).show();
        }
    }

    public enum AckStatus {
        NONE, // Not applicable (received messages)
        PENDING, // Sent, waiting for ACK
        DELIVERED // ACK received
    }

    public interface ScrollCallback {
        void onMessageAdded();
    }

    public static class ChatMessage {
        public final String text;
        public final boolean isSent; // true = sent by user, false = received
        public final long timestamp;
        public final byte seq; // Sequence number for matching ACKs
        public final boolean hasGps;
        public final double latitude;
        public final double longitude;
        public AckStatus ackStatus;

        public ChatMessage(String text, boolean isSent, byte seq) {
            this(text, isSent, seq, false, 0.0, 0.0);
        }

        public ChatMessage(String text, boolean isSent, byte seq, boolean hasGps, double latitude, double longitude) {
            this.text = text;
            this.isSent = isSent;
            this.timestamp = System.currentTimeMillis();
            this.seq = seq;
            this.ackStatus = isSent ? AckStatus.PENDING : AckStatus.NONE;
            this.hasGps = hasGps;
            this.latitude = latitude;
            this.longitude = longitude;
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
