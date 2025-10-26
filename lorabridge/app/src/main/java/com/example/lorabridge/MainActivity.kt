package com.example.lorabridge

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.example.lorabridge.presentation.chat.ChatScreen
import com.example.lorabridge.ui.theme.LorabridgeTheme
import dagger.hilt.android.AndroidEntryPoint

/**
 * Main activity with Hilt dependency injection
 * @see UC-8.2: Show Status Bar with Dark Icons
 */
@AndroidEntryPoint
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Configure window insets for edge-to-edge
        enableEdgeToEdge()
        WindowCompat.setDecorFitsSystemWindows(window, false)

        // Set light status bar icons
        WindowCompat.getInsetsController(window, window.decorView)?.apply {
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            isAppearanceLightStatusBars = true
        }

        setContent {
            LorabridgeTheme {
                ChatScreen()
            }
        }
    }
}
