package com.example.lorabridge.ui.theme

import android.app.Activity
import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.core.view.WindowCompat

private val DarkColorScheme = darkColorScheme(
    primary = SignalTeal80,
    onPrimary = SignalTeal10,
    primaryContainer = SignalTeal30,
    onPrimaryContainer = SignalTeal90,
    secondary = Slate80,
    onSecondary = Slate10,
    secondaryContainer = Slate30,
    onSecondaryContainer = Slate90,
    tertiary = Amber80,
    onTertiary = Amber10,
    tertiaryContainer = Amber30,
    onTertiaryContainer = Amber90,
    error = Error80,
    onError = Error10,
    errorContainer = Error30,
    onErrorContainer = Error90,
    background = Neutral10,
    onBackground = Neutral90,
    surface = Neutral10,
    onSurface = Neutral90,
    surfaceVariant = NeutralVariant30,
    onSurfaceVariant = NeutralVariant80,
    outline = NeutralVariant50
)

private val LightColorScheme = lightColorScheme(
    primary = SignalTeal40,
    onPrimary = Neutral99,
    primaryContainer = SignalTeal90,
    onPrimaryContainer = SignalTeal10,
    secondary = Slate40,
    onSecondary = Neutral99,
    secondaryContainer = Slate90,
    onSecondaryContainer = Slate10,
    tertiary = Amber40,
    onTertiary = Neutral99,
    tertiaryContainer = Amber90,
    onTertiaryContainer = Amber10,
    error = Error40,
    onError = Neutral99,
    errorContainer = Error90,
    onErrorContainer = Error10,
    background = Neutral99,
    onBackground = Neutral10,
    surface = Neutral99,
    onSurface = Neutral10,
    surfaceVariant = Neutral90,
    onSurfaceVariant = NeutralVariant30,
    outline = NeutralVariant50
)

/**
 * Status accents that are not part of the Material scheme but still need to flip
 * with it, so ACK ticks stay legible on both backgrounds.
 */
data class StatusColors(
    val pending: Color,
    val delivered: Color,
    val failed: Color
)

val LocalStatusColors = staticCompositionLocalOf {
    StatusColors(StatusPendingLight, StatusDeliveredLight, StatusFailedLight)
}

@Composable
fun LorabridgeTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    // Wallpaper-based color would override the signal palette, so it is opt-in.
    dynamicColor: Boolean = false,
    content: @Composable () -> Unit
) {
    val colorScheme = when {
        dynamicColor && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S -> {
            val context = LocalContext.current
            if (darkTheme) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
        }

        darkTheme -> DarkColorScheme
        else -> LightColorScheme
    }

    // System bar icons have to contrast with the app background, so they follow
    // the resolved scheme rather than being pinned to the light variant.
    val view = LocalView.current
    if (!view.isInEditMode) {
        SideEffect {
            val window = (view.context as Activity).window
            WindowCompat.getInsetsController(window, view).apply {
                isAppearanceLightStatusBars = !darkTheme
                isAppearanceLightNavigationBars = !darkTheme
            }
        }
    }

    val statusColors = if (darkTheme) {
        StatusColors(StatusPendingDark, StatusDeliveredDark, StatusFailedDark)
    } else {
        StatusColors(StatusPendingLight, StatusDeliveredLight, StatusFailedLight)
    }

    CompositionLocalProvider(LocalStatusColors provides statusColors) {
        MaterialTheme(
            colorScheme = colorScheme,
            typography = Typography,
            content = content
        )
    }
}
