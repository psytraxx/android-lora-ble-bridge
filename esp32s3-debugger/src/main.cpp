//! ESP32 Firmware for LoRa Receiver with Display
//!
//! This firmware implements a LoRa receiver that:
//! - Receives LoRa messages (GPS and Text)
//! - Sends acknowledgments for received messages
//! - Displays messages on TFT screen
//!
//! Features:
//! - LoRa radio for long-range communication (5-10 km typical)
//! - TFT display for visual feedback
//! - LED indicator for received messages

#include <Arduino.h>
#include "lora_config.h"
#include "LoRaManager.h"
#include "Protocol.h"
#include <freertos/queue.h>
#include <esp_task_wdt.h>
#include <freertos/task.h>
#include <LoRa.h>
#include <DisplayManager.h>

// --- Pin Definitions ---
/**
 * @brief Define the pins used for SPI communication and peripherals.
 * These pins are specific to the receiver device and should match the hardware setup.
 */
#define LORA_SCK 12  // SPI clock pin
#define LORA_MISO 13 // SPI MISO pin
#define LORA_MOSI 11 // SPI MOSI pin
#define LORA_SS 10   // LoRa chip select pin
#define LORA_RST 43  // LoRa reset pin
#define LORA_DIO0 3  // LoRa DIO0 / IRQ pin (RTC GPIO for wake-up)

// --- Display Pin Definitions ---
/**
 * @brief Define the pins used for the display.
 */
#define PIN_LCD_BL 38 // BackLight enable pin (see Dimming.txt)
#define LCD_D0 39     // Data pin 0
#define LCD_D1 40     // Data pin 1
#define LCD_D2 41     // Data pin 2
#define LCD_D3 42     // Data pin 3
#define LCD_D4 45     // Data pin 4
#define LCD_D5 46     // Data pin 5
#define LCD_D6 47     // Data pin 6
#define LCD_D7 48     // Data pin 7
#define LCD_WR 8      // Write pin
#define LCD_RD 9      // Read pin
#define LCD_DC 7      // Data/Command pin
#define LCD_CS 6      // Chip Select pin
#define LCD_RES 5     // Reset pin

#define POWER_ON 15 // Power on pin

// --- Button Pin Definitions (LilyGo T-Display-S3) ---
#define WAKE_BUTTON 14 // User button (right) - used for wake-up from deep sleep

#define SERIAL_BAUD_RATE 115200

// Manager objects
LoRaManager loraManager(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS, LORA_RST, LORA_DIO0, LORA_FREQUENCY);

// Struct for LoRa packets with metadata
struct LoRaPacket
{
    uint8_t buffer[256];
    int len;
    int rssi;
    float snr;
};

QueueHandle_t loRaQueue;

DisplayManager display(LCD_D0, LCD_D1, LCD_D2, LCD_D3, LCD_D4, LCD_D5, LCD_D6, LCD_D7,
                       LCD_WR, LCD_RD, LCD_DC, LCD_CS, LCD_RES, PIN_LCD_BL);

// State tracking
bool firstMessageReceived = false;
const int MAX_DISPLAY_LINES = 20;         // Maximum lines to keep in history
String messageHistory[MAX_DISPLAY_LINES]; // Store message lines
int messageCount = 0;
int lastRssi = 0;    // Last received RSSI
float lastSnr = 0.0; // Last received SNR

// ACK timing (non-blocking)
unsigned long ackSendTime = 0;
bool ackPending = false;
Message pendingAckMsg;
int pendingAckSeq = 0;

// Button debouncing and long press detection
unsigned long lastButtonPressTime = 0;
const unsigned long BUTTON_DEBOUNCE = 50;       // 50ms debounce
const unsigned long LONG_PRESS_DURATION = 2000; // 2 seconds for deep sleep
bool buttonPressed = false;
unsigned long buttonPressStartTime = 0;

// Display brightness setting
const uint8_t DISPLAY_BRIGHT = 255; // Full brightness

// Sleep mode settings
unsigned long lastActivityTime = 0;        // Track last activity for sleep
const unsigned long SLEEP_TIMEOUT = 30000; // 30 seconds before light sleep
RTC_DATA_ATTR int bootCount = 0;           // Persistent across deep sleep

// Display layout constants
const int LINE_HEIGHT = 18;               // Height per line for text size 2
const int STATUS_HEIGHT = 20;             // Reserve space for status line at bottom
const int STATUS_LINE_Y_OFFSET = 16;      // Status line position from bottom
const int BUTTON_INDICATOR_Y_OFFSET = 32; // Button indicator position from bottom

// ACK delay constant (time to wait for TX->RX mode switch)
const unsigned long ACK_DELAY_MS = 500; // 500ms delay before sending ACK

/**
 * @brief LoRa receive callback - handles incoming LoRa packets event-driven
 */
void onLoRaReceive(int packetSize)
{
    if (packetSize == 0)
        return;

    LoRaPacket packet;
    packet.len = LoRa.readBytes(packet.buffer, sizeof(packet.buffer));
    packet.rssi = LoRa.packetRssi();
    packet.snr = LoRa.packetSnr();

    if (packet.len > 0)
    {
        xQueueSend(loRaQueue, &packet, 0);
    }
}

/**
 * @brief Configure wake-up sources for deep sleep
 */
void configureDeepSleepWakeup()
{
    // Configure button wake-up (active LOW - pressed when connected to GND)
    esp_sleep_enable_ext0_wakeup((gpio_num_t)WAKE_BUTTON, 0); // Wake on button press

    Serial.println("Configured deep sleep wake-up sources:");
    Serial.println("  - Wake Button (GPIO 14) - active LOW");
}

/**
 * @brief Enter deep sleep mode (triggered by long button press)
 */
void enterDeepSleep()
{
    Serial.println("\n===================================");
    Serial.println("Entering DEEP SLEEP mode...");
    Serial.println("Wake-up source:");
    Serial.println("  - Button press (GPIO 14)");
    Serial.println("===================================\n");

    // Send LoRa message to notify about deep sleep
    Serial.println("Sending deep sleep notification via LoRa...");
    Message deepSleepMsg = Message::createText(0, "Going to deep sleep");
    uint8_t buf[64];
    int len = deepSleepMsg.serialize(buf, sizeof(buf));

    if (len > 0)
    {
        Serial.print("Transmitting ");
        Serial.print(len);
        Serial.println(" bytes via LoRa");

        if (loraManager.sendPacket(buf, len))
        {
            Serial.println("Deep sleep notification sent successfully");
        }
        else
        {
            Serial.println("Failed to send deep sleep notification");
        }

        // Return to RX mode briefly then proceed to sleep
        loraManager.startReceiveMode();
        delay(100); // Brief delay to allow transmission to complete
    }
    else
    {
        Serial.println("Failed to serialize deep sleep message");
    }

    // Show sleep message on display
    display.clearScreen();
    display.setTextSize(2);
    display.setCursor(10, 40);
    display.printLine("DEEP SLEEP");
    display.setCursor(10, 70);
    display.setTextSize(1);
    display.printLine("Manual sleep activated");
    display.printLine("Press button to wake");

    delay(2000); // Show message for 2 seconds

    // Turn off display backlight
    display.setBrightness(0);

    // Configure wake-up sources (button only for deep sleep)
    configureDeepSleepWakeup();

    // Flush serial
    Serial.flush();

    // Enter deep sleep
    esp_deep_sleep_start();
}

/**
 * @brief Configure wake-up sources for light sleep
 */
void configureLightSleepWakeup()
{
    // Configure LoRa DIO0 wake-up only (active HIGH when packet received)
    esp_sleep_enable_ext1_wakeup(1ULL << LORA_DIO0, ESP_EXT1_WAKEUP_ANY_HIGH);

    Serial.println("Configured light sleep wake-up sources:");
    Serial.println("  - LoRa DIO0 (GPIO 3) - active HIGH only");
}

/**
 * @brief Enter light sleep mode
 * Light sleep preserves RAM and peripheral states (including SPI and LoRa module)
 * CRITICAL: Must reinitialize LoRa module after wake to ensure proper RX mode
 */
void enterLightSleep()
{
    Serial.println("\n===================================");
    Serial.println("Entering LIGHT SLEEP mode...");
    Serial.println("Wake-up source:");
    Serial.println("  - LoRa message only (GPIO 3)");
    Serial.println("===================================\n");

    // Show sleep message on display
    display.clearScreen();
    display.setTextSize(2);
    display.setCursor(10, 60);
    display.printLine("Light Sleep Mode");
    display.setCursor(10, 90);
    display.setTextSize(1);
    display.printLine("Send LoRa message");
    display.printLine("to wake up");

    delay(2000); // Show message for 2 seconds

    // Turn off display backlight
    display.setBrightness(0);

    // Configure wake-up sources (LoRa only)
    configureLightSleepWakeup();

    // Flush serial
    Serial.flush();

    // Enter light sleep (preserves RAM and peripherals)
    esp_light_sleep_start();

    // ===== CRITICAL: Wake-up handling =====
    // Execution continues here after wake-up
    Serial.println("\n===================================");
    Serial.println("Woke up from light sleep!");
    Serial.println("===================================\n");

    // CRITICAL: Reinitialize LoRa module after sleep
    // The SX1278 may have lost sync or entered idle mode during sleep
    Serial.println("Reinitializing LoRa module after sleep...");
    loraManager.startReceiveMode();
    delay(50); // Allow LoRa module to stabilize
    Serial.println("LoRa module back in RX mode");

    // Restore display brightness
    display.setBrightness(DISPLAY_BRIGHT);
    lastActivityTime = millis();

    // Clear screen and show wake message
    display.clearScreen();
    display.printLine("Woke: LoRa Message");

    // LoRa callback will process the queued packet
}

/**
 * @brief Check and handle wake-up reason on initial boot
 */
void printWakeupReason()
{
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    Serial.print("Boot count: ");
    Serial.println(bootCount);

    switch (wakeup_reason)
    {
    case ESP_SLEEP_WAKEUP_EXT0:
        Serial.println("Woke up from deep sleep via button press (EXT0)");
        display.printLine("Woke: Button (Deep Sleep)");
        break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
        Serial.println("Power-on or reset");
        display.printLine("Power On / Reset");
        break;
    default:
        Serial.println("Woke from other source");
        break;
    }
}

/**
 * @brief Adds a new message to the display, pushing down existing messages
 */
void addMessageToDisplay(const String &message, int rssi, float snr)
{
    // Reset activity timer and restore brightness
    lastActivityTime = millis();
    // Clear screen on first message
    if (!firstMessageReceived)
    {
        firstMessageReceived = true;
        display.clearScreen();
        messageCount = 0;
    }

    // Update RSSI/SNR values
    lastRssi = rssi;
    lastSnr = snr;

    // Shift existing messages down
    for (int i = MAX_DISPLAY_LINES - 1; i > 0; i--)
    {
        messageHistory[i] = messageHistory[i - 1];
    }

    // Add new message at top (without RSSI/SNR, will be shown at bottom)
    messageHistory[0] = message;

    if (messageCount < MAX_DISPLAY_LINES)
    {
        messageCount++;
    }

    // Redraw all messages
    display.clearScreen();
    display.setTextSize(2); // Bigger font
    display.setCursor(0, 0);

    int maxVisibleLines = (display.height() - STATUS_HEIGHT) / LINE_HEIGHT;

    // Display messages (limit to what fits on screen)
    int linesToShow = min(messageCount, maxVisibleLines);
    for (int i = 0; i < linesToShow; i++)
    {
        display.setCursor(0, i * LINE_HEIGHT);
        display.printLine(messageHistory[i]);
    }

    // Draw status line at bottom with RSSI/SNR
    int statusY = display.height() - STATUS_LINE_Y_OFFSET;
    display.fillRect(0, statusY, display.width(), STATUS_LINE_Y_OFFSET, BLACK); // Clear status area
    display.setCursor(0, statusY);
    display.setTextSize(1); // Smaller font for status
    display.setTextColor(GREEN, BLACK);

    // Build status string to avoid overload ambiguity
    char statusBuf[50];
    sprintf(statusBuf, "RSSI: %d dBm | SNR: %.1f dB", lastRssi, (double)lastSnr);
    display.print(statusBuf);

    display.setTextColor(WHITE, BLACK); // Reset to default
}

/**
 * @brief Setup routine for ESP32 LoRa Receiver
 */
void setup()
{
    Serial.begin(SERIAL_BAUD_RATE);
    delay(2000);

    bootCount++; // Increment boot counter (persists in RTC memory)

    pinMode(POWER_ON, OUTPUT);
    digitalWrite(POWER_ON, HIGH);

    // Configure buttons as input with pull-up
    pinMode(WAKE_BUTTON, INPUT_PULLUP);

    // Initialize the display for visual feedback
    display.setup();
    display.printLine("TFT Initialized.");

    // Print wake-up reason (only relevant on first boot, not after light sleep)
    printWakeupReason();

    display.printLine("LoRa Receiver Starting...");
    Serial.println("TFT Initialized.");

// Set CPU frequency for power savings (configurable via build flag)
#ifndef CPU_FREQ_MHZ
#define CPU_FREQ_MHZ 160
#endif
    setCpuFrequencyMhz(CPU_FREQ_MHZ);
    Serial.print("CPU Frequency set to: ");
    Serial.print(getCpuFrequencyMhz());
    Serial.println(" MHz");

    // Initialize watchdog for robustness (30 second timeout)
    esp_task_wdt_config_t wdt_config = {
        .timeout_ms = 30000, // 30 seconds
        .trigger_panic = true,
    };
    esp_task_wdt_init(&wdt_config);
    esp_task_wdt_add(xTaskGetCurrentTaskHandle());

    Serial.println("===================================");
    Serial.println("ESP32 LoRa Receiver starting...");
    Serial.println("===================================");

    // Create message queue for LoRa packets
    loRaQueue = xQueueCreate(15, sizeof(LoRaPacket));

    if (loRaQueue == nullptr)
    {
        Serial.println("Failed to create message queue. Halting execution.");
        display.printLine("Queue creation failed!");
        while (1)
        {
            delay(1000);
        }
    }

    // Initialize LoRa
    Serial.println("\nInitializing LoRa radio...");
    Serial.println(loraManager.getConfigurationString());
    display.printLine("Initializing LoRa...");

    const int LORA_RETRY_COUNT = 3;
    int loraRetries = LORA_RETRY_COUNT;
    bool loraSuccess = false;

    while (loraRetries > 0 && !loraSuccess)
    {
        Serial.print("LoRa setup attempt ");
        Serial.print(LORA_RETRY_COUNT - loraRetries + 1);
        Serial.print("/");
        Serial.println(LORA_RETRY_COUNT);

        if (loraManager.setup())
        {
            loraSuccess = true;
            Serial.println("LoRa setup successful");
            display.printLine("LoRa initialized!");
        }
        else
        {
            Serial.println("LoRa setup failed");
            display.printLine("LoRa setup failed!");
            if (loraRetries > 1)
            {
                Serial.println("Retrying in 1 second...");
                delay(1000);
            }
            loraRetries--;
        }
    }

    if (!loraSuccess)
    {
        Serial.println("LoRa setup failed permanently. Halting execution.");
        display.printLine("LoRa Init Failed!");
        while (1)
        {
            delay(1000);
        }
    }

    // Set up event-driven LoRa reception
    LoRa.onReceive(onLoRaReceive);

    // Start continuous receive mode
    loraManager.startReceiveMode();
    display.printLine("LoRa Receiver ready.");
    Serial.println("LoRa Receiver ready.");

    Serial.println("\n===================================");
    Serial.println("All systems initialized successfully");
    Serial.println("Waiting for LoRa messages...");
    Serial.println("Using light sleep (preserves LoRa state)");
    Serial.println("Long press button (2s) for deep sleep");
    Serial.println("===================================\n");

    // Don't clear screen - keep init messages until first message arrives

    // Initialize activity timer
    lastActivityTime = millis();

    // Initialize message history
    for (int i = 0; i < MAX_DISPLAY_LINES; i++)
    {
        messageHistory[i] = "";
    }
    messageCount = 0;

    Serial.println("Message history initialized");
}

/**
 * @brief Main loop - handles LoRa message reception and display
 */
void loop()
{
    // Check for button press (long press = deep sleep, short press = activity reset)
    bool currentButtonState = digitalRead(WAKE_BUTTON) == LOW;

    if (currentButtonState && !buttonPressed && (millis() - lastButtonPressTime > BUTTON_DEBOUNCE))
    {
        // Button just pressed (after debounce)
        buttonPressed = true;
        buttonPressStartTime = millis();
        lastButtonPressTime = millis(); // Update for release debounce
        Serial.println("Button pressed - hold for 2s for deep sleep");

        // Show indicator on display (above status line)
        int indicatorY = display.height() - BUTTON_INDICATOR_Y_OFFSET;
        display.fillRect(0, indicatorY, display.width(), 16, BLACK); // Clear area
        display.setCursor(0, indicatorY);
        display.setTextSize(1);
        display.setTextColor(YELLOW, BLACK);
        display.print("Hold for deep sleep...");
        display.setTextColor(WHITE, BLACK);
    }
    else if (buttonPressed)
    {
        unsigned long pressDuration = millis() - buttonPressStartTime;

        if (!currentButtonState && (millis() - lastButtonPressTime > BUTTON_DEBOUNCE))
        {
            // Button released (with debounce)
            buttonPressed = false;
            lastButtonPressTime = millis();

            if (pressDuration >= LONG_PRESS_DURATION)
            {
                Serial.println("Long press detected - entering deep sleep");
                enterDeepSleep();
                // Never returns (device resets on wake)
            }
            else
            {
                // Short press - reset activity timer
                Serial.println("Button short press - activity reset");
                lastActivityTime = millis();
            }
        }
        else if (pressDuration >= LONG_PRESS_DURATION)
        {
            // Long press threshold reached while still held - enter deep sleep immediately
            Serial.println("Long press threshold reached - entering deep sleep");
            enterDeepSleep();
            // Never returns (device resets on wake)
        }
    }

    // Check for messages from LoRa (event-driven via callback)
    LoRaPacket packet;
    if (xQueueReceive(loRaQueue, &packet, 0) == pdTRUE)
    {
        Serial.print("LoRa RX: received ");
        Serial.print(packet.len);
        Serial.print(" bytes, RSSI: ");
        Serial.print(packet.rssi);
        Serial.print(" dBm, SNR: ");
        Serial.print(packet.snr);
        Serial.println(" dB");

        // Deserialize message
        Message msg;
        if (msg.deserialize(packet.buffer, packet.len))
        {
            Serial.print("LoRa message deserialized: type=");
            Serial.println((int)msg.type);

            // Handle different message types
            switch (msg.type)
            {
            case MessageType::Text:
            {
                Serial.print("Text message - seq: ");
                Serial.print(msg.textData.seq);
                Serial.print(", text: \"");
                Serial.print(msg.textData.text);
                Serial.print("\"");

                if (msg.textData.hasGps)
                {
                    Serial.print(", GPS: ");
                    Serial.print(msg.textData.lat / 1000000.0, 6);
                    Serial.print("°, ");
                    Serial.print(msg.textData.lon / 1000000.0, 6);
                    Serial.print("°");
                }
                Serial.println();

                // Display text message on screen
                String displayText = "TXT #";
                displayText += String(msg.textData.seq);
                displayText += ": ";
                displayText += String(msg.textData.text);

                // Add GPS info if available
                if (msg.textData.hasGps)
                {
                    displayText += " [";
                    displayText += String(msg.textData.lat / 1000000.0, 5);
                    displayText += "°,";
                    displayText += String(msg.textData.lon / 1000000.0, 5);
                    displayText += "°]";
                }

                addMessageToDisplay(displayText, packet.rssi, packet.snr);

                // Schedule ACK to send after delay (non-blocking)
                // This allows sender time to switch from TX to RX mode
                ackPending = true;
                pendingAckSeq = msg.textData.seq;
                ackSendTime = millis() + ACK_DELAY_MS;
                pendingAckMsg = Message::createAck(msg.textData.seq);

                Serial.print("ACK scheduled for seq ");
                Serial.print(msg.textData.seq);
                Serial.print(" in ");
                Serial.print(ACK_DELAY_MS);
                Serial.println("ms");

                break;
            }

            case MessageType::Ack:
            {
                Serial.print("Received ACK for seq: ");
                Serial.println(msg.ackData.seq);

                // Display ACK on screen (brief info)
                String ackDisplay = "ACK #";
                ackDisplay += String(msg.ackData.seq);
                addMessageToDisplay(ackDisplay, packet.rssi, packet.snr);
                break;
            }
            }
        }
        else
        {
            Serial.println("Failed to deserialize LoRa message");
            addMessageToDisplay("ERROR: Decode failed", packet.rssi, packet.snr);
        }
    }

    // Check for pending ACK to send (non-blocking)
    if (ackPending && millis() >= ackSendTime)
    {
        ackPending = false;

        uint8_t ackBuf[64];
        int ackLen = pendingAckMsg.serialize(ackBuf, sizeof(ackBuf));

        if (ackLen > 0)
        {
            Serial.print("Sending ACK for seq: ");
            Serial.println(pendingAckSeq);
            bool ackSent = loraManager.sendPacket(ackBuf, ackLen);
            if (ackSent)
            {
                Serial.println("ACK sent successfully");
            }
            else
            {
                Serial.println("ACK send failed");
            }
            loraManager.startReceiveMode();
        }
    }

    // Check for sleep timeout (prevents immediate re-sleep after wake)
    unsigned long timeSinceActivity = millis() - lastActivityTime;
    if (timeSinceActivity > SLEEP_TIMEOUT)
    {
        Serial.println("Inactivity timeout - entering light sleep mode");
        enterLightSleep();
        // After wake-up, execution continues here
        // Activity time already reset in enterLightSleep()
    }

    // Small delay to prevent watchdog issues and allow task switching
    vTaskDelay(pdMS_TO_TICKS(10));

    // Reset watchdog to prevent timeout
    esp_task_wdt_reset();
}