package com.example.lorabridge.presentation.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.wrapContentWidth
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Place
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalLocale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.lorabridge.domain.model.AckStatus
import com.example.lorabridge.domain.model.ChatMessage
import com.example.lorabridge.ui.theme.LocalStatusColors
import java.text.SimpleDateFormat
import java.util.Date

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
    val timeFormat = SimpleDateFormat("HH:mm", LocalLocale.current.platformLocale)
    val statusColors = LocalStatusColors.current

    // Sent and received bubbles use different containers, so the text and the
    // metadata have to read their "on" colors from the matching role.
    val containerColor = if (message.isSent) {
        MaterialTheme.colorScheme.primaryContainer
    } else {
        MaterialTheme.colorScheme.secondaryContainer
    }
    val contentColor = if (message.isSent) {
        MaterialTheme.colorScheme.onPrimaryContainer
    } else {
        MaterialTheme.colorScheme.onSecondaryContainer
    }

    // A tail-less corner on the owning side reads as a chat bubble without
    // needing a drawn tail.
    val bubbleShape = if (message.isSent) {
        RoundedCornerShape(topStart = 18.dp, topEnd = 18.dp, bottomStart = 18.dp, bottomEnd = 4.dp)
    } else {
        RoundedCornerShape(topStart = 18.dp, topEnd = 18.dp, bottomStart = 4.dp, bottomEnd = 18.dp)
    }

    Box(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 3.dp),
        contentAlignment = if (message.isSent) Alignment.CenterEnd else Alignment.CenterStart
    ) {
        Column(
            modifier = Modifier
                // Keep a gutter on the opposite edge so the direction of each
                // message stays obvious even for long text.
                .fillMaxWidth(0.85f)
                .wrapContentWidth(if (message.isSent) Alignment.End else Alignment.Start)
                .clip(bubbleShape)
                .background(color = containerColor)
                .clickable(enabled = message.canOpenMaps()) {
                    message.latitude?.let { lat ->
                        message.longitude?.let { lon ->
                            onMapClick(lat, lon)
                        }
                    }
                }
                .padding(horizontal = 14.dp, vertical = 10.dp)
        ) {
            Text(
                text = message.text,
                style = MaterialTheme.typography.bodyLarge,
                color = contentColor
            )

            Row(
                modifier = Modifier.padding(top = 4.dp),
                horizontalArrangement = Arrangement.spacedBy(6.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    text = timeFormat.format(Date(message.timestamp)),
                    style = MaterialTheme.typography.labelSmall,
                    color = contentColor.copy(alpha = 0.7f)
                )

                // ACK status for sent messages
                if (message.isSent && message.ackStatus != AckStatus.NONE) {
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
                            AckStatus.PENDING -> statusColors.pending
                            AckStatus.DELIVERED -> statusColors.delivered
                            AckStatus.FAILED -> statusColors.failed
                            AckStatus.NONE -> contentColor
                        }
                    )
                }

                // GPS indicator — a vector icon tints with the bubble, unlike the
                // fixed-color emoji it replaces.
                if (message.hasGps) {
                    Icon(
                        imageVector = Icons.Default.Place,
                        contentDescription = "Has location",
                        tint = contentColor.copy(alpha = 0.7f),
                        modifier = Modifier.size(14.dp)
                    )
                }
            }
        }
    }
}
