package com.example.lorabridge.ui.theme

import androidx.compose.ui.graphics.Color

/**
 * Signal-themed palette: a teal/cyan primary (radio, connectivity) with a warm
 * amber secondary for in-flight state. Tones follow the Material 3 scale, so the
 * light and dark schemes below stay contrast-correct without hand-tuning.
 */

// Primary — signal teal
val SignalTeal80 = Color(0xFF4FD8E4)
val SignalTeal40 = Color(0xFF006874)
val SignalTeal90 = Color(0xFF9EF0FA)
val SignalTeal10 = Color(0xFF001F24)
val SignalTeal30 = Color(0xFF004F58)

// Secondary — muted slate
val Slate80 = Color(0xFFB0CBD1)
val Slate40 = Color(0xFF4A6268)
val Slate90 = Color(0xFFCCE7ED)
val Slate10 = Color(0xFF051F23)
val Slate30 = Color(0xFF324A50)

// Tertiary — amber, used for "approaching limit" / pending states
val Amber80 = Color(0xFFF7BD6B)
val Amber40 = Color(0xFF7C5800)
val Amber90 = Color(0xFFFFDEA6)
val Amber10 = Color(0xFF271900)
val Amber30 = Color(0xFF5A3F00)

// Neutrals
val Neutral99 = Color(0xFFFAFDFD)
val Neutral10 = Color(0xFF191C1D)
val Neutral95 = Color(0xFFEFF1F1)
val Neutral20 = Color(0xFF2D3132)
val Neutral90 = Color(0xFFDBE4E6)
val Neutral30 = Color(0xFF414749)
val NeutralVariant30 = Color(0xFF3F484A)
val NeutralVariant80 = Color(0xFFBFC8CA)
val NeutralVariant50 = Color(0xFF6F797A)

// Error
val Error80 = Color(0xFFFFB4AB)
val Error40 = Color(0xFFBA1A1A)
val Error90 = Color(0xFFFFDAD6)
val Error10 = Color(0xFF410002)
val Error30 = Color(0xFF93000A)

/** Status accents used for ACK indicators, readable on both schemes. */
val StatusPendingLight = Color(0xFF8A5300)
val StatusPendingDark = Color(0xFFFFB95C)
val StatusDeliveredLight = Color(0xFF1B6B33)
val StatusDeliveredDark = Color(0xFF6FDC8C)
val StatusFailedLight = Color(0xFFBA1A1A)
val StatusFailedDark = Color(0xFFFFB4AB)
