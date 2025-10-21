package com.lora.android;

import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;

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
        android.util.Log.d("MessageAdapter", "updateAckStatus called: seq=" + seq + ", status=" + status);
        android.util.Log.d("MessageAdapter", "Total messages in list: " + messages.size());

        boolean found = false;
        for (int i = 0; i < messages.size(); i++) {
            ChatMessage msg = messages.get(i);
            android.util.Log.d("MessageAdapter",
                    "  Message[" + i + "]: seq=" + msg.seq + ", isSent=" + msg.isSent + ", text=" + msg.text);

            if (msg.isSent && msg.seq == seq) {
                android.util.Log.d("MessageAdapter", "  -> MATCH FOUND! Updating message " + i);
                msg.ackStatus = status;
                notifyItemChanged(i);
                found = true;
                break;
            }
        }

        if (!found) {
            android.util.Log.w("MessageAdapter", "No matching sent message found for seq=" + seq);
        }
    }

    public void clear() {
        messages.clear();
        notifyDataSetChanged();
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
