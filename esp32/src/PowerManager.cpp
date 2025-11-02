#include "PowerManager.h"
#include "FirmwareConfig.h"
#include "esp_pm.h"
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <driver/rtc_io.h>
#include <esp_log.h>

static const char *TAG_POWER = "PWR";

void PowerManager::configurePowerManagement()
{
    ESP_LOGI(TAG_POWER, "Configuring power management");

// Check if power management is available (requires CONFIG_PM_ENABLE in ESP-IDF)
// Arduino framework for ESP32-S3 does not enable CONFIG_PM_ENABLE by default
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
}

// see https://github.com/geeksville/Meshtastic-esp32/blob/0f167faa63f53af19dee7959927966db69591436/src/sleep.cpp#L395
void PowerManager::configureWakeupSources(int wakeButton, int loraDio0)
{
    ESP_LOGI(TAG_POWER, "Configuring wakeup sources");

    // Configure button wake (LOW trigger - button pressed = LOW)
#if defined(CONFIG_IDF_TARGET_ESP32)
    // Enable internal pull-up for button
    gpio_pullup_en((gpio_num_t)wakeButton);
    gpio_wakeup_enable((gpio_num_t)wakeButton, GPIO_INTR_LOW_LEVEL);
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    // ESP32-S3: Use GPIO wakeup
    gpio_pullup_en((gpio_num_t)wakeButton);
    gpio_wakeup_enable((gpio_num_t)wakeButton, GPIO_INTR_LOW_LEVEL);
#endif

    // Configure LoRa DIO0 wake (HIGH trigger - LoRa interrupt = HIGH)
    // The key is to use the appropriate wakeup method based on chip capabilities
#if defined(CONFIG_IDF_TARGET_ESP32) && SOC_PM_SUPPORT_EXT_WAKEUP
    // ESP32 original: Can use EXT0 wakeup for RTC-capable GPIOs
    if (rtc_gpio_is_valid_gpio((gpio_num_t)loraDio0))
    {
        // This GPIO supports RTC, use EXT0 for lower power consumption
        gpio_pulldown_en((gpio_num_t)loraDio0); // Pull down so HIGH is detected

        // Enable EXT0 wakeup on HIGH level (LoRa interrupt active high)
        esp_sleep_enable_ext0_wakeup((gpio_num_t)loraDio0, 1); // 1 = HIGH level

        ESP_LOGI(TAG_POWER, "LoRa DIO0 (GPIO %d) configured with EXT0 wakeup (RTC)", loraDio0);
    }
    else
    {
        // Not an RTC GPIO, use standard GPIO wakeup
        gpio_pulldown_en((gpio_num_t)loraDio0);
        gpio_wakeup_enable((gpio_num_t)loraDio0, GPIO_INTR_HIGH_LEVEL);

        ESP_LOGI(TAG_POWER, "LoRa DIO0 (GPIO %d) configured with GPIO wakeup", loraDio0);
    }
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    // ESP32-S3: Use GPIO wakeup (no EXT0 support)
    gpio_pulldown_en((gpio_num_t)loraDio0);
    gpio_wakeup_enable((gpio_num_t)loraDio0, GPIO_INTR_HIGH_LEVEL);

    ESP_LOGI(TAG_POWER, "LoRa DIO0 (GPIO %d) configured with GPIO wakeup", loraDio0);
#else
    // Fallback for other variants
    gpio_pulldown_en((gpio_num_t)loraDio0);
    gpio_wakeup_enable((gpio_num_t)loraDio0, GPIO_INTR_HIGH_LEVEL);

    ESP_LOGI(TAG_POWER, "LoRa DIO0 (GPIO %d) configured with GPIO wakeup", loraDio0);
#endif

    // Enable GPIO wakeup system (must be called after gpio_wakeup_enable)
    esp_sleep_enable_gpio_wakeup();

    ESP_LOGI(TAG_POWER, "GPIO wakeup configured - Button (GPIO %d on LOW), LoRa DIO0 (GPIO %d on HIGH)",
             wakeButton, loraDio0);
}

void PowerManager::refreshWakeupSources()
{
    // Re-enable GPIO wakeup (required on some ESP32 variants before each sleep)
    esp_sleep_enable_gpio_wakeup();
}

int PowerManager::enterLightSleep()
{
    ESP_LOGI(TAG_POWER, "Entering light sleep...");

    // Ensure RTC peripherals stay on during light sleep (needed for GPIO wakeup)
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    // Flush all pending UART output before sleeping
    // This ensures log messages are actually transmitted before entering sleep
    uart_wait_tx_done((uart_port_t)CONFIG_ESP_CONSOLE_UART_NUM, portMAX_DELAY);

    // Enter light sleep (blocking call)
    esp_err_t res = esp_light_sleep_start();

    if (res != ESP_OK)
    {
        ESP_LOGE(TAG_POWER, "Light sleep failed with error: %d", res);
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
        ESP_LOGI(TAG_POWER, "Woke from light sleep - GPIO interrupt");
        break;
    case ESP_SLEEP_WAKEUP_EXT0:
        ESP_LOGI(TAG_POWER, "Woke from light sleep - EXT0 (LoRa RTC GPIO)");
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        ESP_LOGI(TAG_POWER, "Woke from light sleep - Timer");
        break;
    default:
        ESP_LOGI(TAG_POWER, "Woke from light sleep - Unknown reason (%d)", wakeup_reason);
        break;
    }

    return (int)wakeup_reason;
}
