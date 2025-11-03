#include "PowerManager.h"
#include "FirmwareConfig.h"
#include "esp_pm.h"
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <driver/rtc_io.h>
#include <Arduino.h>

void PowerManager::configurePowerManagement()
{
    Serial.println("Configuring power management");

#if defined(CONFIG_IDF_TARGET_ESP32)
    esp_pm_config_esp32_t pm_config = {};
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    esp_pm_config_esp32s3_t pm_config = {};
#else
#error "Unsupported ESP32 variant"
#endif

    pm_config.max_freq_mhz = CPU_FREQ_MHZ;
    pm_config.min_freq_mhz = PowerConstants::CPU_MIN_FREQ_MHZ;
    pm_config.light_sleep_enable = false;

    esp_err_t rv = esp_pm_configure(&pm_config);
    if (rv != ESP_OK)
    {
        Serial.printf("Failed to configure power management (err=%d)\n", rv);
        return;
    }

    Serial.printf("Power management configured (CPU: %d MHz max, %d MHz min)\n",
                  CPU_FREQ_MHZ, PowerConstants::CPU_MIN_FREQ_MHZ);
}

// see https://github.com/geeksville/Meshtastic-esp32/blob/0f167faa63f53af19dee7959927966db69591436/src/sleep.cpp#L395
void PowerManager::configureWakeupSources(int wakeButton, int loraDio0)
{
    Serial.println("Configuring wakeup sources");

    // Configure button wake (LOW trigger - button pressed = LOW)
    gpio_pullup_en((gpio_num_t)wakeButton);
    gpio_wakeup_enable((gpio_num_t)wakeButton, GPIO_INTR_LOW_LEVEL);

    // Configure LoRa DIO0 wake (HIGH trigger - LoRa interrupt = HIGH)
    gpio_pulldown_en((gpio_num_t)loraDio0);

#if defined(CONFIG_IDF_TARGET_ESP32) && SOC_PM_SUPPORT_EXT_WAKEUP
    // ESP32: Try EXT0 wakeup for RTC GPIOs (lower power), fallback to GPIO wakeup
    if (rtc_gpio_is_valid_gpio((gpio_num_t)loraDio0))
    {
        esp_sleep_enable_ext0_wakeup((gpio_num_t)loraDio0, 1); // 1 = HIGH level
        Serial.printf("LoRa DIO0 (GPIO %d) using EXT0 wakeup\n", loraDio0);
    }
    else
#endif
    {
        // ESP32-S3/C3 or non-RTC GPIO: Use standard GPIO wakeup
        gpio_wakeup_enable((gpio_num_t)loraDio0, GPIO_INTR_HIGH_LEVEL);
        Serial.printf("LoRa DIO0 (GPIO %d) using GPIO wakeup\n", loraDio0);
    }

    refreshWakeupSources();

    Serial.printf("Wakeup configured - Button (GPIO %d LOW), LoRa DIO0 (GPIO %d HIGH)\n",
                  wakeButton, loraDio0);
}

void PowerManager::refreshWakeupSources()
{
    // Re-enable GPIO wakeup (required on some ESP32 variants before each sleep)
    esp_sleep_enable_gpio_wakeup();
}

int PowerManager::enterLightSleep()
{
    Serial.println("Entering light sleep...");

    // Ensure RTC peripherals stay on during light sleep (needed for GPIO wakeup)
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    Serial.flush(); // Ensure all logs are sent before sleep

    // Enter light sleep (blocking call)
    esp_err_t res = esp_light_sleep_start();

    if (res != ESP_OK)
    {
        Serial.printf("Light sleep failed with error: %d\n", res);
        return ESP_SLEEP_WAKEUP_UNDEFINED;
    }

    // Get wakeup cause
    return logWakeupCause();
}

int PowerManager::logWakeupCause()
{

    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    // Log wakeup reason with detailed info
    switch (wakeup_reason)
    {
    case ESP_SLEEP_WAKEUP_GPIO:
        Serial.println("Woke from light sleep - GPIO interrupt");
        break;
    case ESP_SLEEP_WAKEUP_EXT0:
        Serial.println("Woke from light sleep - EXT0 (LoRa RTC GPIO)");
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        Serial.println("Woke from light sleep - Timer");
        break;
    default:
        Serial.printf("Woke from light sleep - Unknown reason (%d)\n", wakeup_reason);
        break;
    }

    return (int)wakeup_reason;
}
