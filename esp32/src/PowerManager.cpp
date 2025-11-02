#include "PowerManager.h"
#include "FirmwareConfig.h"
#include "esp_pm.h"
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_log.h>

static const char *TAG_POWER = "PowerManager";

void PowerManager::configurePowerManagement()
{
    ESP_LOGI(TAG_POWER, "Configuring power management");

// Check if power management is available (requires CONFIG_PM_ENABLE in ESP-IDF)
// Arduino framework for ESP32-S3 does not enable CONFIG_PM_ENABLE by default
#ifdef POWER_MANAGEMENT_ENABLED
    // Select appropriate power management config type based on chip
#if defined(CONFIG_IDF_TARGET_ESP32)
    static esp_pm_config_t pm_config = {};
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
    static esp_pm_config_t pm_config = {};
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    static esp_pm_config_t pm_config = {};
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    static esp_pm_config_t pm_config = {};
#else
#error "Unsupported ESP32 variant"
#endif

    pm_config.max_freq_mhz = CPU_FREQ_MHZ;
    pm_config.min_freq_mhz = PowerConstants::CPU_MIN_FREQ_MHZ;
    pm_config.light_sleep_enable = false; // Manual sleep only

    int rv = esp_pm_configure(&pm_config);
    if (rv != ESP_OK)
    {
        ESP_LOGE(TAG_POWER, "Failed to configure power management (err=%d)", rv);
        return;
    }

    ESP_LOGI(TAG_POWER, "Power management configured (CPU: %d MHz max, %d MHz min)", CPU_FREQ_MHZ, PowerConstants::CPU_MIN_FREQ_MHZ);
#else
    setCpuFrequencyMhz(CPU_FREQ_MHZ);
    ESP_LOGW(TAG_POWER, "Dynamic frequency scaling not available (CONFIG_PM_ENABLE not set)");
    ESP_LOGI(TAG_POWER, "CPU running at fixed %d MHz", CPU_FREQ_MHZ);
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

    ESP_LOGI(TAG_POWER, "GPIO wakeup configured - Button (GPIO %d on LOW), LoRa DIO0 (GPIO %d on HIGH)", wakeButton, loraDio0);
}

void PowerManager::refreshWakeupSources()
{
    // Re-enable GPIO wakeup (required on some ESP32 variants before each sleep)
    esp_sleep_enable_gpio_wakeup();
}

int PowerManager::enterLightSleep()
{
    ESP_LOGI(TAG_POWER, "Entering light sleep...");

    // Flush all pending UART output before sleeping
    // This ensures log messages are actually transmitted before entering sleep
    uart_wait_tx_done((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, portMAX_DELAY);

    // Enter light sleep (blocking call)
    esp_light_sleep_start();

    // Get wakeup cause
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    // Log wakeup reason
    ESP_LOGI(TAG_POWER, "Woke from light sleep - reason: ");
    switch (wakeup_reason)
    {
    case ESP_SLEEP_WAKEUP_GPIO:
        ESP_LOGI(TAG_POWER, "GPIO");
        break;
    case ESP_SLEEP_WAKEUP_EXT0:
        ESP_LOGI(TAG_POWER, "EXT0 (wake button)");
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        ESP_LOGI(TAG_POWER, "Timer");
        break;
    default:
        ESP_LOGI(TAG_POWER, "Unknown (%d)", wakeup_reason);
        break;
    }

    return (int)wakeup_reason;
}
