#ifndef LOGGING_H
#define LOGGING_H

/**
 * @file Logging.h
 * @brief Unified logging system for ESP32 and nRF52 platforms
 *
 * Provides consistent logging interface with automatic timestamps:
 * - ESP32: Uses ESP_LOG macros (timestamps from esp_log)
 * - nRF52: Uses Serial with millis() timestamp prefix
 *
 * Usage:
 *   LOG_I("BLE", "Client connected");
 *   LOG_E("LoRa", "Failed to send message: %d", errorCode);
 *
 * Log levels:
 *   LOG_E - Error
 *   LOG_W - Warning
 *   LOG_I - Info
 *   LOG_D - Debug
 *   LOG_V - Verbose
 */

#ifdef ESP32_PLATFORM

// ESP32: Use esp_log.h which provides automatic timestamps
#include <esp_log.h>

#define LOG_E(tag, format, ...) ESP_LOGE(tag, format, ##__VA_ARGS__)
#define LOG_W(tag, format, ...) ESP_LOGW(tag, format, ##__VA_ARGS__)
#define LOG_I(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#define LOG_D(tag, format, ...) ESP_LOGD(tag, format, ##__VA_ARGS__)
#define LOG_V(tag, format, ...) ESP_LOGV(tag, format, ##__VA_ARGS__)

#else // NRF52_PLATFORM or other platforms

// nRF52 and others: Use Serial with millis() timestamp
#include <Arduino.h>

// Helper function to print timestamp
inline void _log_print_timestamp() {
    unsigned long ms = millis();
    unsigned long s = ms / 1000;
    ms = ms % 1000;
    Serial.print("[");
    Serial.print(s);
    Serial.print(".");
    if (ms < 100) Serial.print("0");
    if (ms < 10) Serial.print("0");
    Serial.print(ms);
    Serial.print("] ");
}

// Error (red on terminals that support it)
#define LOG_E(tag, format, ...) do { \
    _log_print_timestamp(); \
    Serial.print("E ["); \
    Serial.print(tag); \
    Serial.print("] "); \
    Serial.printf(format, ##__VA_ARGS__); \
    Serial.println(); \
} while(0)

// Warning (yellow on terminals that support it)
#define LOG_W(tag, format, ...) do { \
    _log_print_timestamp(); \
    Serial.print("W ["); \
    Serial.print(tag); \
    Serial.print("] "); \
    Serial.printf(format, ##__VA_ARGS__); \
    Serial.println(); \
} while(0)

// Info (green on terminals that support it)
#define LOG_I(tag, format, ...) do { \
    _log_print_timestamp(); \
    Serial.print("I ["); \
    Serial.print(tag); \
    Serial.print("] "); \
    Serial.printf(format, ##__VA_ARGS__); \
    Serial.println(); \
} while(0)

// Debug
#define LOG_D(tag, format, ...) do { \
    _log_print_timestamp(); \
    Serial.print("D ["); \
    Serial.print(tag); \
    Serial.print("] "); \
    Serial.printf(format, ##__VA_ARGS__); \
    Serial.println(); \
} while(0)

// Verbose
#define LOG_V(tag, format, ...) do { \
    _log_print_timestamp(); \
    Serial.print("V ["); \
    Serial.print(tag); \
    Serial.print("] "); \
    Serial.printf(format, ##__VA_ARGS__); \
    Serial.println(); \
} while(0)

#endif // ESP32_PLATFORM

#endif // LOGGING_H
