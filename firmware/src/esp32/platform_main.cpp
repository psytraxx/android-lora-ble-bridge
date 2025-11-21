//! ESP32 Platform Entry Point (Hexagonal Architecture)
//!
//! This file provides the platform-specific implementation for ESP32.
//! It creates concrete adapters and injects them into the unified main application.

#include "ports/PlatformPorts.h"
#include "esp32/FirmwareConfig.h"
#include "esp32/adapters/ESP32BLEAdapter.h"
#include "esp32/adapters/ESP32LoRaAdapter.h"
#include "esp32/adapters/ESP32StorageAdapter.h"
#include "esp32/adapters/ESP32PowerAdapter.h"
#include "esp32/adapters/ESP32SystemAdapter.h"
#include "esp32/adapters/ESP32ActivityAdapter.h"
#include <Adafruit_SleepyDog.h>

// Platform-specific configuration functions for main.cpp
LoRaConfig getPlatformLoRaConfig()
{
    return {
        .frequency = LORA_FREQUENCY,
        .bandwidth = LORA_BANDWIDTH,
        .spreadingFactor = LORA_SPREADING_FACTOR,
        .codingRate = LORA_CODING_RATE,
        .txPower = LORA_TX_POWER};
}

unsigned long getPowerInactivityTimeout()
{
    return PowerConstants::INACTIVITY_TIMEOUT_MS;
}

int getLedRxBlinks()
{
    return LEDConstants::RX_BLINKS;
}

int getLedTxBlinks()
{
    return LEDConstants::TX_BLINKS;
}

/**
 * @brief Create ESP32-specific platform adapters
 *
 * This factory function instantiates all ESP32 adapters:
 * - ESP32BLEAdapter (NimBLE)
 * - ESP32LoRaAdapter (RadioLib with SX1262/SX1278)
 * - ESP32StorageAdapter (NVS)
 * - ESP32PowerAdapter (ADC-based battery monitoring)
 * - ESP32SystemAdapter (Watchdog, LEDManager)
 * - ESP32ActivityAdapter (ApplicationController with mutex)
 *
 * Note: Logging is done directly via Serial (no adapter needed)
 */
PlatformPorts createPlatformPorts()
{
    // Initialize watchdog first
    int watchdogMS = Watchdog.enable(WatchdogConstants::TIMEOUT_SECONDS * 1000);
    Serial.print("Watchdog enabled: ");
    Serial.print(watchdogMS);
    Serial.println(" ms");

    PlatformPorts ports;

    ports.system = new ESP32SystemAdapter();
    ports.power = new ESP32PowerAdapter();
    ports.storage = new ESP32StorageAdapter();
    ports.activity = new ESP32ActivityAdapter();
    ports.ble = new ESP32BLEAdapter();

    ports.lora = new ESP32LoRaAdapter(
        LORA_SCK,
        LORA_MISO,
        LORA_MOSI,
        LORA_SS,
        LORA_RST,
        LORA_DIO0,
        LORA_BUSY);

    return ports;
}

/**
 * @brief Get ESP32 device name from configuration
 */
const char *getDeviceName()
{
    return DEVICE_NAME;
}
