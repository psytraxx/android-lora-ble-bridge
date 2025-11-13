#include "PowerManager.h"
#include "FirmwareConfig.h"
#include "esp_pm.h"
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <driver/rtc_io.h>
#include <esp_log.h>

static const char *TAG = "PWR";

void PowerManager::configurePowerManagement()
{
    ESP_LOGI(TAG, "Configuring power management");

    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = PowerConstants::CPU_MIN_FREQ_MHZ,
        .light_sleep_enable = true};

    esp_err_t rv = esp_pm_configure(&pm_config);
    if (rv != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to configure power management (err=%d)", rv);
        return;
    }

    ESP_LOGI(TAG, "Power management configured (CPU: %d MHz max, %d MHz min)",
             CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ, PowerConstants::CPU_MIN_FREQ_MHZ);
}

// see https://github.com/geeksville/Meshtastic-esp32/blob/0f167faa63f53af19dee7959927966db69591436/src/sleep.cpp#L395
void PowerManager::configureWakeupSources(int wakeButton, int loraDio0)
{
    ESP_LOGI(TAG, "Configuring wakeup sources for deep sleep");

    // Initialize DIO0 as an RTC pin FIRST (before enabling wakeup)
    if (rtc_gpio_is_valid_gpio((gpio_num_t)loraDio0))
    {
        rtc_gpio_init((gpio_num_t)loraDio0);
        rtc_gpio_set_direction((gpio_num_t)loraDio0, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pulldown_en((gpio_num_t)loraDio0);
        rtc_gpio_pullup_dis((gpio_num_t)loraDio0);
    }
    else
    {
        ESP_LOGE(TAG, "GPIO %d is not RTC-capable! Cannot use for deep sleep wakeup.", loraDio0);
    }

    // Configure wake-up source: DIO0 going HIGH (use ext0)
    esp_sleep_enable_ext0_wakeup((gpio_num_t)loraDio0, 1);

    // Initialize button pin as RTC GPIO
    if (rtc_gpio_is_valid_gpio((gpio_num_t)wakeButton))
    {
        rtc_gpio_init((gpio_num_t)wakeButton);
        rtc_gpio_set_direction((gpio_num_t)wakeButton, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pullup_en((gpio_num_t)wakeButton); // Button needs pull-up for LOW trigger
        rtc_gpio_pulldown_dis((gpio_num_t)wakeButton);
    }

    esp_sleep_enable_ext1_wakeup((1ULL << wakeButton), ESP_EXT1_WAKEUP_ANY_LOW);

    ESP_LOGI(TAG, "Wakeup configured - Button (GPIO %d LOW), LoRa DIO0 (GPIO %d HIGH)",
             wakeButton, loraDio0);
}

void PowerManager::enterDeepSleep()
{

    ESP_LOGI(TAG, "DIO0 state before sleep: %d", gpio_get_level((gpio_num_t)LORA_DIO0));

    ESP_LOGI(TAG, "Entering deep sleep...");
    ESP_LOGI(TAG, "Will wake on:");
    ESP_LOGI(TAG, "  - LoRa DIO0 going HIGH");
    ESP_LOGI(TAG, "  - Wake Button going LOW");

    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    // Enter deep sleep - device will reset on wake
    esp_deep_sleep_start();
}

void PowerManager::printWakeupReason()
{
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    switch (wakeup_reason)
    {
    case ESP_SLEEP_WAKEUP_EXT0:
        ESP_LOGI(TAG, "Woke: LoRa DIO0 (Deep Sleep)");
        break;
    case ESP_SLEEP_WAKEUP_EXT1:
        ESP_LOGI(TAG, "Woke: Button (Deep Sleep)");
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        ESP_LOGI(TAG, "Woke: Timer");
        break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
        ESP_LOGI(TAG, "Woke: Touchpad");
        break;
    case ESP_SLEEP_WAKEUP_ULP:
        ESP_LOGI(TAG, "Woke: ULP Program");
        break;
    case ESP_SLEEP_WAKEUP_GPIO:
        ESP_LOGI(TAG, "Woke: GPIO");
        break;
    case ESP_SLEEP_WAKEUP_UART:
        ESP_LOGI(TAG, "Woke: UART");
        break;
    case ESP_SLEEP_WAKEUP_WIFI:
        ESP_LOGI(TAG, "Woke: WIFI");
        break;
    case ESP_SLEEP_WAKEUP_COCPU:
        ESP_LOGI(TAG, "Woke: COCPU");
        break;
    case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG:
        ESP_LOGI(TAG, "Woke: COCPU Crash");
        break;
    case ESP_SLEEP_WAKEUP_BT:
        ESP_LOGI(TAG, "Woke: Bluetooth");
        break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
        ESP_LOGI(TAG, "Power On / Reset");
        break;
    default:
        ESP_LOGI(TAG, "Woke: Unknown");
        break;
    }
}
