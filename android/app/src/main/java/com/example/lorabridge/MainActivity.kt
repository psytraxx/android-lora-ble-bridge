package com.example.lorabridge

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
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

        // Draw behind the system bars; LorabridgeTheme picks the bar icon tint to
        // match the active color scheme, and ChatScreen consumes the insets.
        enableEdgeToEdge()

        setContent {
            LorabridgeTheme {
                ChatScreen()
            }
        }
    }
}
