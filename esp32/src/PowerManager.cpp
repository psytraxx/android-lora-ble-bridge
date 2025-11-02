#include "PowerManager.h"
#include "FirmwareConfig.h"
#include "esp_pm.h"
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <Arduino.h>

void PowerManager::configurePowerManagement()
{
    Serial.println("PowerManager: Configuring power management");

// Check if power management is available (requires CONFIG_PM_ENABLE in ESP-IDF)
// Arduino framework for ESP32-S3 does not enable CONFIG_PM_ENABLE by default
#ifdef POWER_MANAGEMENT_ENABLED
    // Select appropriate power management config type based on chip
#if defined(CONFIG_IDF_TARGET_ESP32)
    static esp_pm_config_esp32_t pm_config = {};
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
    static esp_pm_config_esp32s2_t pm_config = {};
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    static esp_pm_config_esp32s3_t pm_config = {};
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    static esp_pm_config_esp32c3_t pm_config = {};
#else
#error "Unsupported ESP32 variant"
#endif

    pm_config.max_freq_mhz = CPU_FREQ_MHZ;
    pm_config.min_freq_mhz = PowerConstants::CPU_MIN_FREQ_MHZ;
    pm_config.light_sleep_enable = false; // Manual sleep only

    int rv = esp_pm_configure(&pm_config);
    if (rv != ESP_OK)
    {
        Serial.print("PowerManager: Failed to configure power management (err=");
        Serial.print(rv);
        Serial.println(")");
        return;
    }

    Serial.print("PowerManager: Power management configured (CPU: ");
    Serial.print(CPU_FREQ_MHZ);
    Serial.print(" MHz max, ");
    Serial.print(PowerConstants::CPU_MIN_FREQ_MHZ);
    Serial.println(" MHz min)");
#else
    setCpuFrequencyMhz(CPU_FREQ_MHZ);
    Serial.println("PowerManager: Dynamic frequency scaling not available (CONFIG_PM_ENABLE not set)");
    Serial.print("PowerManager: CPU running at fixed ");
    Serial.print(CPU_FREQ_MHZ);
    Serial.println(" MHz");
#endif
}

void PowerManager::configureWakeupSources(int wakeButton, int loraDio0)
{
    // Configure boot button wake (GPIO wakeup for LOW trigger)
    // TODO: not working as expected on ESP32-S3 - needs further investigation
    gpio_wakeup_enable((gpio_num_t)wakeButton, GPIO_INTR_LOW_LEVEL);

    // Configure LoRa DIO0 wake (GPIO wakeup for HIGH trigger)
    // Add pulldown to ensure clean LOW state when idle
    gpio_pulldown_en((gpio_num_t)loraDio0);
    gpio_wakeup_enable((gpio_num_t)loraDio0, GPIO_INTR_HIGH_LEVEL);

    // Enable GPIO wakeup - must be called after gpio_wakeup_enable
    esp_sleep_enable_gpio_wakeup();

    Serial.print("PowerManager: GPIO wakeup configured - Button (GPIO");
    Serial.print(wakeButton);
    Serial.print(" on LOW), LoRa DIO0 (GPIO");
    Serial.print(loraDio0);
    Serial.println(" on HIGH)");
}

void PowerManager::refreshWakeupSources()
{
    // Re-enable GPIO wakeup (required on some ESP32 variants before each sleep)
    esp_sleep_enable_gpio_wakeup();
}

int PowerManager::enterLightSleep()
{
    Serial.println("PowerManager: Entering light sleep...");
    Serial.flush();

    // Enter light sleep (blocking call)
    esp_light_sleep_start();

    // Get wakeup cause
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    // Log wakeup reason
    Serial.print("PowerManager: Woke from light sleep - reason: ");
    switch (wakeup_reason)
    {
    case ESP_SLEEP_WAKEUP_GPIO:
        Serial.println("GPIO");
        break;
    case ESP_SLEEP_WAKEUP_EXT0:
        Serial.println("EXT0 (wake button)");
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        Serial.println("Timer");
        break;
    default:
        Serial.print("Unknown (");
        Serial.print(wakeup_reason);
        Serial.println(")");
        break;
    }

    return (int)wakeup_reason;
}
