package com.example.lorabridge.presentation.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.lorabridge.domain.model.AckStatus
import com.example.lorabridge.domain.model.ChatMessage
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Message bubble component for chat display
 * @see UC-6.1: Display Chat Message
 * @see UC-6.2: Update ACK Status Indicator
 * @see UC-4.2: Open Location in Maps
 */
@Composable
fun MessageBubble(
    message: ChatMessage,
    onMapClick: (Double, Double) -> Unit
) {
    val timeFormat = SimpleDateFormat("HH:mm", Locale.getDefault())

    Box(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 4.dp),
        contentAlignment = if (message.isSent) Alignment.CenterEnd else Alignment.CenterStart
    ) {
        Column(
            modifier = Modifier
                .background(
                    color = if (message.isSent) {
                        MaterialTheme.colorScheme.primaryContainer
                    } else {
                        MaterialTheme.colorScheme.secondaryContainer
                    },
                    shape = RoundedCornerShape(12.dp)
                )
                .clickable(enabled = message.canOpenMaps()) {
                    message.latitude?.let { lat ->
                        message.longitude?.let { lon ->
                            onMapClick(lat, lon)
                        }
                    }
                }
                .padding(12.dp)
        ) {
            Text(
                text = message.text,
                style = MaterialTheme.typography.bodyLarge,
                color = MaterialTheme.colorScheme.onPrimaryContainer
            )

            Spacer(modifier = Modifier.padding(top = 4.dp))

            Row(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = timeFormat.format(Date(message.timestamp)),
                    style = MaterialTheme.typography.bodySmall,
                    fontSize = 10.sp,
                    color = MaterialTheme.colorScheme.onPrimaryContainer.copy(alpha = 0.7f)
                )

                // ACK status for sent messages
                if (message.isSent) {
                    Text(
                        text = when (message.ackStatus) {
                            AckStatus.PENDING -> "⏱"
                            AckStatus.DELIVERED -> "✓"
                            AckStatus.FAILED -> "✗"
                            AckStatus.NONE -> ""
                        },
                        fontSize = 12.sp,
                        fontWeight = FontWeight.Bold,
                        color = when (message.ackStatus) {
                            AckStatus.PENDING -> Color(0xFFFF9800)  // Orange
                            AckStatus.DELIVERED -> Color(0xFF4CAF50)  // Green
                            AckStatus.FAILED -> Color(0xFFF44336)  // Red
                            AckStatus.NONE -> Color.Transparent
                        }
                    )
                }

                // GPS indicator
                if (message.hasGps) {
                    Text(
                        text = "📍",
                        fontSize = 12.sp
                    )
                }
            }
        }
    }
}
