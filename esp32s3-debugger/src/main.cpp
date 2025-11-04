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
#include <RadioLib.h>
#include "Protocol.h"
#include <freertos/queue.h>
#include <esp_task_wdt.h>
#include <freertos/task.h>
#include <DisplayManager.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

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
#define WAKE_BUTTON 14 // User button (right) - used for wake-up from light sleep

#define SERIAL_BAUD_RATE 115200

// RadioLib SX1278 radio instance
SX1278 radio = new Module(LORA_SS, LORA_DIO0, LORA_RST, RADIOLIB_NC);

// Struct for LoRa packets with metadata
struct LoRaPacket
{
    uint8_t buffer[256];
    int len;
    int rssi;
    float snr;
};

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

// Button state tracking
unsigned long lastButtonPressTime = 0;
bool buttonPressed = false;

// Display brightness setting
const uint8_t DISPLAY_BRIGHT = 255; // Full brightness

// Sleep mode settings
unsigned long lastActivityTime = 0;        // Track last activity for sleep
const unsigned long SLEEP_TIMEOUT = 60000; // 60 seconds awake timeout (reset on message)
RTC_DATA_ATTR int bootCount = 0;           // Persistent across light sleep

// Persistent message storage (RTC memory) - keep up to 10 latest human-readable messages
#define PERSISTENT_SLOTS 10
#define PERSISTENT_MSG_BUF 256

// Store null-terminated C-strings in RTC memory so they survive light sleep.
// Using fixed-size arrays avoids dynamic allocation in RTC memory.
RTC_DATA_ATTR char rtcMessageStorage[PERSISTENT_SLOTS][PERSISTENT_MSG_BUF];
RTC_DATA_ATTR int rtc_msg_count = 0; // number of valid messages stored (<= PERSISTENT_SLOTS)
RTC_DATA_ATTR int rtc_head = 0;      // next write index (0..PERSISTENT_SLOTS-1)

// Display layout constants
const int LINE_HEIGHT = 18;               // Height per line for text size 2
const int STATUS_HEIGHT = 20;             // Reserve space for status line at bottom
const int STATUS_LINE_Y_OFFSET = 16;      // Status line position from bottom
const int BUTTON_INDICATOR_Y_OFFSET = 32; // Button indicator position from bottom

// ACK delay constant (time to wait for TX->RX mode switch)
const unsigned long ACK_DELAY_MS = 50; // 50ms delay before sending ACK

// Flags for LoRa activity (set in ISR, checked in loop)
// IMPORTANT: ISR should ONLY set flags - all data reading happens in main loop
volatile bool loraPacketReceived = false;

/**
 * @brief LoRa receive callback - handles incoming LoRa packets event-driven (ISR)
 * Following RadioLib best practices - ISR only sets flag
 */
#if defined(ESP8266) || defined(ESP32)
ICACHE_RAM_ATTR
#endif
void onLoRaReceive(void)
{
    // Set flag only - do NOT read data in ISR!
    // IMPORTANT: No Serial.print allowed in ISR - causes re-entry issues
    loraPacketReceived = true;
}

/**
 * @brief Enter deep sleep mode with DIO0 and button wake-up
 * Device will wake up when:
 * - LoRa DIO0 pin goes HIGH (incoming message), or
 * - Wake button is pressed (goes LOW)
 */
void goToDeepSleep()
{
    Serial.println("Entering deep sleep...");
    Serial.println("Will wake on:");
    Serial.println("  - LoRa DIO0 (GPIO 3) going HIGH");
    Serial.println("  - Wake Button (GPIO 14) going LOW");

    // Power off peripherals during sleep
    digitalWrite(POWER_ON, LOW);

    // Configure wake-up source: DIO0 going HIGH (use ext0)
    esp_sleep_enable_ext0_wakeup((gpio_num_t)LORA_DIO0, 1);

    // Configure wake-up source: Button going LOW (use ext1)
    // ext1 allows multiple pins with logic level (ANY_LOW or ALL_HIGH)
    esp_sleep_enable_ext1_wakeup((1ULL << WAKE_BUTTON), ESP_EXT1_WAKEUP_ANY_LOW);

    // Initialize DIO0 as an RTC pin before going to sleep
    rtc_gpio_init((gpio_num_t)LORA_DIO0);
    rtc_gpio_set_direction((gpio_num_t)LORA_DIO0, RTC_GPIO_MODE_INPUT_ONLY);

    // Initialize WAKE_BUTTON as an RTC pin with pullup
    rtc_gpio_init((gpio_num_t)WAKE_BUTTON);
    rtc_gpio_set_direction((gpio_num_t)WAKE_BUTTON, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en((gpio_num_t)WAKE_BUTTON);
    rtc_gpio_pulldown_dis((gpio_num_t)WAKE_BUTTON);

    // Flush serial before sleeping
    Serial.flush();

    // Enter deep sleep - device will reset on wake
    esp_deep_sleep_start();
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
        display.printLine("Woke: LoRa DIO0 (Deep Sleep)");
        break;
    case ESP_SLEEP_WAKEUP_EXT1:
        display.printLine("Woke: Button (Deep Sleep)");
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        display.printLine("Woke: Timer");
        break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
        display.printLine("Woke: Touchpad");
        break;
    case ESP_SLEEP_WAKEUP_ULP:
        display.printLine("Woke: ULP Program");
        break;
    case ESP_SLEEP_WAKEUP_GPIO:
        display.printLine("Woke: GPIO");
        break;
    case ESP_SLEEP_WAKEUP_UART:
        display.printLine("Woke: UART");
        break;
    case ESP_SLEEP_WAKEUP_WIFI:
        display.printLine("Woke: WIFI");
        break;
    case ESP_SLEEP_WAKEUP_COCPU:
        display.printLine("Woke: COCPU");
        break;
    case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG:
        display.printLine("Woke: COCPU Crash");
        break;
    case ESP_SLEEP_WAKEUP_BT:
        display.printLine("Woke: Bluetooth");
        break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
        display.printLine("Power On / Reset");
        break;
    default:
        display.printLine("Woke: Unknown");
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
    Serial.printf("State: lastActivityTime reset to %lu\n", lastActivityTime);
    // Clear screen on first message
    if (!firstMessageReceived)
    {
        firstMessageReceived = true;
        Serial.println("State: firstMessageReceived set to true");
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
        Serial.printf("State: messageCount incremented to %d\n", messageCount);
    }

    // Redraw all messages
    display.clearScreen();
    display.setFontGeneral(); // Bigger font
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
    display.setFontTiny(); // Smaller font for status
    display.setTextColor(GREEN, BLACK);

    // Build status string to avoid overload ambiguity
    char statusBuf[50];
    sprintf(statusBuf, "RSSI: %d dBm | SNR: %.1f dB", lastRssi, (double)lastSnr);
    display.print(statusBuf);

    display.setTextColor(WHITE, BLACK); // Reset to default
}

/**
 * @brief Restore messages saved in RTC persistent storage and add them to display history
 */
void restorePersistentMessages()
{
    if (rtc_msg_count <= 0)
    {
        Serial.println("No persistent messages to restore");
        return;
    }

    // Initialize message history (in-memory) first so restoring persistent
    // messages from RTC memory appends into this buffer instead of being
    // cleared afterwards.
    for (int i = 0; i < MAX_DISPLAY_LINES; i++)
    {
        messageHistory[i] = "";
    }
    messageCount = 0;
    Serial.println("Message history initialized");

    Serial.print("Restoring ");
    Serial.print(rtc_msg_count);
    Serial.println(" persistent messages from RTC memory");

    int start = (rtc_head - rtc_msg_count + PERSISTENT_SLOTS) % PERSISTENT_SLOTS;
    for (int i = 0; i < rtc_msg_count; i++)
    {
        int idx = (start + i) % PERSISTENT_SLOTS;

        // rtcMessageStorage contains a null-terminated C-string summary of the message
        const char *saved = rtcMessageStorage[idx];
        // RSSI/SNR not persisted; pass 0 values when restoring
        if (saved[0] != '\0')
        {
            addMessageToDisplay(String(saved), 0, 0.0f);
        }
        else
        {
            addMessageToDisplay("(Saved) <empty>", 0, 0.0f);
        }
    }
}

/**
 * @brief Setup routine for ESP32 LoRa Receiver
 */
void setup()
{
    Serial.begin(SERIAL_BAUD_RATE);

    bootCount++; // Increment boot counter (persists in RTC memory)

    // Power on peripherals first thing after wake-up
    pinMode(POWER_ON, OUTPUT);
    digitalWrite(POWER_ON, HIGH);

    // Wait for power to stabilize before initializing peripherals
    // Critical for battery operation where voltage may take time to settle
    delay(200);

    // Configure buttons as input with pull-up
    pinMode(WAKE_BUTTON, INPUT_PULLUP);

    // Initialize the display for visual feedback
    display.setup();
    display.printLine("TFT Initialized.");

    printWakeupReason();

// Set CPU frequency for power savings (configurable via build flag)
#ifndef CPU_FREQ_MHZ
#define CPU_FREQ_MHZ 160
#endif
    setCpuFrequencyMhz(CPU_FREQ_MHZ);
    Serial.print("CPU Frequency set to: ");
    Serial.print(getCpuFrequencyMhz());
    Serial.println(" MHz");

    esp_task_wdt_add(xTaskGetCurrentTaskHandle());

    Serial.println("===================================");
    Serial.println("ESP32 LoRa Receiver starting...");
    Serial.println("===================================");

    // Initialize LoRa with RadioLib
    display.printLine("Initializing LoRa...");

    SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);

    const int LORA_RETRY_COUNT = 3;
    int loraRetries = LORA_RETRY_COUNT;
    bool loraSuccess = false;

    while (loraRetries > 0 && !loraSuccess)
    {
        Serial.print("LoRa setup attempt ");
        Serial.print(LORA_RETRY_COUNT - loraRetries + 1);
        Serial.print("/");
        Serial.println(LORA_RETRY_COUNT);

        int state = radio.begin(LORA_FREQUENCY, LORA_BANDWIDTH, LORA_SPREADING_FACTOR, LORA_CODING_RATE, 0x12, LORA_TX_POWER);

        if (state == RADIOLIB_ERR_NONE)
        {
            loraSuccess = true;
            display.printLine("LoRa initialized!");
            Serial.println("LoRa setup successful");
            Serial.print("  Frequency: ");
            Serial.print(LORA_FREQUENCY);
            Serial.println(" MHz");
            Serial.print("  Bandwidth: ");
            Serial.print(LORA_BANDWIDTH);
            Serial.println(" kHz");
            Serial.print("  Spreading Factor: ");
            Serial.println(LORA_SPREADING_FACTOR);
            Serial.print("  Coding Rate: 4/");
            Serial.println(LORA_CODING_RATE);
            Serial.print("  TX Power: ");
            Serial.print(LORA_TX_POWER);
            Serial.println(" dBm");
        }
        else
        {
            display.printLine("LoRa setup failed!");
            Serial.print("LoRa setup failed, code ");
            Serial.println(state);
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
        display.printLine("LoRa Init Failed!");
        while (1)
        {
            delay(1000);
        }
    }

    // Set up event-driven LoRa reception
    radio.setPacketReceivedAction(onLoRaReceive);

    // Start continuous receive mode
    int state = radio.startReceive();
    if (state != RADIOLIB_ERR_NONE)
    {
        Serial.print("Failed to start receive mode, code ");
        Serial.println(state);
    }
    display.printLine("LoRa Receiver ready.");

    Serial.println("\n===================================");
    Serial.println("All systems initialized successfully");
    Serial.println("Waiting for LoRa messages...");
    Serial.println("Short press wakes or sends test message when awake");
    Serial.println("===================================\n");

    delay(2000);

    // Restore any messages persisted across light sleep
    restorePersistentMessages();

    // Initialize activity timer
    lastActivityTime = millis();
    Serial.printf("State: lastActivityTime initialized to %lu\n", lastActivityTime);
}

/**
 * @brief Main loop - handles LoRa message reception and display
 */
void loop()
{
    // Check for button press (long press = light sleep, short press = activity reset)
    bool currentButtonState = digitalRead(WAKE_BUTTON) == LOW;

    if (currentButtonState && !buttonPressed)
    {
        // Button just pressed
        buttonPressed = true;
        lastButtonPressTime = millis();
        Serial.println("Button pressed");

        // Show indicator on display (above status line)
        int indicatorY = display.height() - BUTTON_INDICATOR_Y_OFFSET;
        display.fillRect(0, indicatorY, display.width(), 16, BLACK); // Clear area
        display.setCursor(0, indicatorY);
        display.setFontGeneral();
        display.setTextColor(YELLOW, BLACK);
        display.print("Button pressed...");
        display.setTextColor(WHITE, BLACK);
    }
    else if (buttonPressed)
    {
        if (!currentButtonState)
        {
            // Button released - treat as short press
            buttonPressed = false;
            lastButtonPressTime = millis();

            // Short press - send test message when awake
            Serial.println("Button short press - sending test message");
            Message testMsg = Message::createText(0, "Test from device");
            uint8_t tbuf[128];
            int tlen = testMsg.serialize(tbuf, sizeof(tbuf));
            if (tlen > 0)
            {
                // Clear RX interrupt handler to allow DIO0 to signal TX completion
                radio.clearPacketReceivedAction();

                // Reconfigure watchdog for 10 seconds
                esp_task_wdt_init(10, true);
                esp_task_wdt_add(xTaskGetCurrentTaskHandle());

                int state = radio.transmit(tbuf, tlen);

                // Restore normal watchdog timeout
                esp_task_wdt_init(5, true);
                esp_task_wdt_add(xTaskGetCurrentTaskHandle());

                if (state == RADIOLIB_ERR_NONE)
                {
                    Serial.println("Test message sent");
                }
                else
                {
                    Serial.print("Failed to send test message, code ");
                    Serial.println(state);
                }

                // Restore RX interrupt handler and return to RX mode
                radio.setPacketReceivedAction(onLoRaReceive);
                radio.startReceive();
            }

            // Reset awake timer
            lastActivityTime = millis();
            Serial.printf("State: lastActivityTime reset to %lu\n", lastActivityTime);
        }
    }

    // Check for messages from LoRa (flag set by ISR, read data here in main loop)
    if (loraPacketReceived)
    {
        loraPacketReceived = false;

        // Read packet data in main loop (NOT in ISR)
        LoRaPacket packet;
        int state = radio.readData(packet.buffer, sizeof(packet.buffer));

        if (state == RADIOLIB_ERR_NONE)
        {
            packet.len = radio.getPacketLength();
            packet.rssi = radio.getRSSI();
            packet.snr = radio.getSNR();

            Serial.print("LoRa RX: received ");
            Serial.print(packet.len);
            Serial.print(" bytes, RSSI: ");
            Serial.print(packet.rssi);
            Serial.print(" dBm, SNR: ");
            Serial.print(packet.snr);
            Serial.println(" dB");

            // Deserialize message
            Message msg;
            String summary;
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

                    summary = displayText;

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
                    summary = ackDisplay;
                    addMessageToDisplay(ackDisplay, packet.rssi, packet.snr);
                    break;
                }

                case MessageType::WakeUp:
                {
                    Serial.println("Received WakeUp message");
                    // WakeUp messages are used to wake the device from sleep
                    // The device is already awake if we received this, so just log it
                    summary = "WakeUp signal";
                    addMessageToDisplay("WakeUp signal received", packet.rssi, packet.snr);
                    break;
                }
                }
            }
            else
            {
                Serial.println("Failed to deserialize LoRa message");
                summary = "ERROR: Decode failed";
                addMessageToDisplay("ERROR: Decode failed", packet.rssi, packet.snr);
            }

            // Persist to RTC circular buffer
            strncpy(rtcMessageStorage[rtc_head], summary.c_str(), PERSISTENT_MSG_BUF - 1);
            rtcMessageStorage[rtc_head][PERSISTENT_MSG_BUF - 1] = '\0';
            rtc_head = (rtc_head + 1) % PERSISTENT_SLOTS;
            if (rtc_msg_count < PERSISTENT_SLOTS)
                rtc_msg_count++;
        }
        else if (state == RADIOLIB_ERR_CRC_MISMATCH)
        {
            Serial.println("LoRa RX: CRC error");
        }
        else
        {
            Serial.print("LoRa RX failed, code ");
            Serial.println(state);
        }

        // Restart receive mode
        radio.startReceive();
    }

    // Check for pending ACK to send (non-blocking)
    if (ackPending && millis() >= ackSendTime)
    {
        ackPending = false;
        Serial.println("State: ackPending cleared");

        uint8_t ackBuf[64];
        int ackLen = pendingAckMsg.serialize(ackBuf, sizeof(ackBuf));

        if (ackLen > 0)
        {
            Serial.print("Sending ACK for seq: ");
            Serial.println(pendingAckSeq);

            // Clear RX interrupt handler to allow DIO0 to signal TX completion
            radio.clearPacketReceivedAction();

            // Reconfigure watchdog for 10 seconds
            esp_task_wdt_init(10, true);
            esp_task_wdt_add(xTaskGetCurrentTaskHandle());

            int state = radio.transmit(ackBuf, ackLen);

            // Restore normal watchdog timeout
            esp_task_wdt_init(5, true);
            esp_task_wdt_add(xTaskGetCurrentTaskHandle());

            if (state == RADIOLIB_ERR_NONE)
            {
                Serial.println("ACK sent successfully");
            }
            else
            {
                Serial.print("ACK send failed, code ");
                Serial.println(state);
            }

            // Restore RX interrupt handler and return to RX mode
            radio.setPacketReceivedAction(onLoRaReceive);
            radio.startReceive();
        }
    }

    // Check for sleep timeout (prevents immediate re-sleep after wake)
    unsigned long timeSinceActivity = millis() - lastActivityTime;
    if (timeSinceActivity > SLEEP_TIMEOUT)
    {
        Serial.println("Inactivity timeout - entering deep sleep mode");
        goToDeepSleep();
        // Device will reset on wake
    }

    // Small delay to prevent watchdog issues and allow task switching
    vTaskDelay(pdMS_TO_TICKS(10));

    // Reset watchdog to prevent timeout
    esp_task_wdt_reset();
}