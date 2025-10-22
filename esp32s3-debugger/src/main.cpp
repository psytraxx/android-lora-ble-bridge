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
#define LORA_DIO0 44 // LoRa DIO0 / IRQ pin

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
const int MAX_DISPLAY_LINES = 20; // Maximum lines to keep in history
String messageHistory[20];        // Store message lines
int messageCount = 0;
int lastRssi = 0;    // Last received RSSI
float lastSnr = 0.0; // Last received SNR

// Display dimming settings
const unsigned long DISPLAY_DIM_TIMEOUT = 30000; // 30 seconds
const uint8_t DISPLAY_BRIGHT = 255;              // Full brightness
const uint8_t DISPLAY_DIM = 10;                  // Dimmed brightness
unsigned long lastActivityTime = 0;              // Track last activity
bool displayDimmed = false;                      // Track dimming state

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
 * @brief Adds a new message to the display, pushing down existing messages
 */
void addMessageToDisplay(const String &message, int rssi, float snr)
{
    // Reset activity timer and restore brightness
    lastActivityTime = millis();
    if (displayDimmed)
    {
        display.setBrightness(DISPLAY_BRIGHT);
        displayDimmed = false;
    }

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

    int lineHeight = 18;   // Height per line for text size 2
    int statusHeight = 20; // Reserve space for status line at bottom
    int maxVisibleLines = (display.height() - statusHeight) / lineHeight;

    // Display messages (limit to what fits on screen)
    int linesToShow = min(messageCount, maxVisibleLines);
    for (int i = 0; i < linesToShow; i++)
    {
        display.setCursor(0, i * lineHeight);
        display.printLine(messageHistory[i]);
    }

    // Draw status line at bottom with RSSI/SNR
    int statusY = display.height() - 16;                      // Position at bottom
    display.fillRect(0, statusY, display.width(), 16, BLACK); // Clear status area
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

    pinMode(POWER_ON, OUTPUT);
    digitalWrite(POWER_ON, HIGH);

    // Initialize the display for visual feedback
    display.setup();
    display.printLine("TFT Initialized.");
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
    Serial.println("===================================\n");

    // Don't clear screen - keep init messages until first message arrives

    // Initialize activity timer
    lastActivityTime = millis();
}

/**
 * @brief Main loop - handles LoRa message reception and display
 */
void loop()
{
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
                Serial.println("\"");

                // Display text message on screen
                String displayText = "TXT #";
                displayText += String(msg.textData.seq);
                displayText += ": ";
                displayText += String(msg.textData.text);
                addMessageToDisplay(displayText, packet.rssi, packet.snr);

                // Wait for sender to return to RX mode (allow time for TX->RX transition)
                delay(500);

                // Send ACK
                Message ack = Message::createAck(msg.textData.seq);
                uint8_t ackBuf[64];
                int ackLen = ack.serialize(ackBuf, sizeof(ackBuf));

                if (ackLen > 0)
                {
                    Serial.print("Sending ACK for seq: ");
                    Serial.println(msg.textData.seq);
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

                break;
            }

            case MessageType::Gps:
            {
                Serial.print("GPS message - seq: ");
                Serial.print(msg.gpsData.seq);
                Serial.print(", lat: ");
                Serial.print(msg.gpsData.lat / 1000000.0, 6);
                Serial.print("°, lon: ");
                Serial.print(msg.gpsData.lon / 1000000.0, 6);
                Serial.println("°");

                // Display GPS coordinates on screen
                String gpsDisplay = "GPS #";
                gpsDisplay += String(msg.gpsData.seq);
                gpsDisplay += ": ";
                gpsDisplay += String(msg.gpsData.lat / 1000000.0, 5);
                gpsDisplay += "° ";
                gpsDisplay += String(msg.gpsData.lon / 1000000.0, 5);
                gpsDisplay += "°";
                addMessageToDisplay(gpsDisplay, packet.rssi, packet.snr);

                // Wait for sender to return to RX mode (allow time for TX->RX transition)
                delay(500);

                // Send ACK
                Message ack = Message::createAck(msg.gpsData.seq);
                uint8_t ackBuf[64];
                int ackLen = ack.serialize(ackBuf, sizeof(ackBuf));

                if (ackLen > 0)
                {
                    Serial.print("Sending ACK for GPS seq: ");
                    Serial.println(msg.gpsData.seq);
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

    // Check for display dimming timeout
    if (!displayDimmed && (millis() - lastActivityTime > DISPLAY_DIM_TIMEOUT))
    {
        display.setBrightness(DISPLAY_DIM);
        displayDimmed = true;
        Serial.println("Display dimmed due to inactivity");
    }

    // Small delay to prevent watchdog issues and allow task switching
    vTaskDelay(pdMS_TO_TICKS(10));

    // Reset watchdog to prevent timeout
    esp_task_wdt_reset();
}