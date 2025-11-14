#include "PowerManager.h"
#include "FirmwareConfig.h"
#include "esp_pm.h"
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <driver/rtc_io.h>
#include <esp_log.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <esp_wifi.h>
#include <esp_task_wdt.h>
#include <esp_bt.h>
#include <esp_private/periph_ctrl.h>

static const char *TAG = "PWR";

// Battery voltage constants (Li-ion)
static const float BATTERY_MIN_VOLTAGE = 3.0f; // 0%
static const float BATTERY_MAX_VOLTAGE = 4.2f; // 100%

void PowerManager::configurePowerManagement()
{
    ESP_LOGI(TAG, "Configuring power management");

    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = PowerConstants::CPU_MIN_FREQ_MHZ,
        .light_sleep_enable = false}; // Disabled: incompatible with BLE on ESP32-S3

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

float PowerManager::readBatteryVoltage()
{
#ifndef BATTERY_ADC_PIN
    ESP_LOGW(TAG, "Battery monitoring not configured for this board");
    return 0.0f;
#else

    // Map GPIO pin to ADC channel
    // LilyGo S3: GPIO4 = ADC1_CHANNEL_3
    // Heltec: GPIO1 = ADC1_CHANNEL_0
    auto adc_channel = ADC_CHANNEL_0;  // Default, overridden below

#if BATTERY_ADC_PIN == 1
    adc_channel = ADC_CHANNEL_0; // GPIO1
#elif BATTERY_ADC_PIN == 4
    adc_channel = ADC_CHANNEL_3; // GPIO4
#else
    ESP_LOGE(TAG, "Unsupported battery ADC pin: %d", BATTERY_ADC_PIN);
    return 0.0f;
#endif

    ESP_LOGI(TAG, "Reading battery from GPIO %d (ADC channel %d)", BATTERY_ADC_PIN, adc_channel);

    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {};
    init_config1.unit_id = ADC_UNIT_1;
    init_config1.ulp_mode = ADC_ULP_MODE_DISABLE;
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, adc_channel, &config));

#ifdef BATTERY_ADC_CTRL
    // Enable ADC (Heltec boards)
    gpio_set_direction((gpio_num_t)BATTERY_ADC_CTRL, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)BATTERY_ADC_CTRL, 1);
    vTaskDelay(pdMS_TO_TICKS(10)); // Wait for ADC to stabilize
#endif
    int raw_value = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, adc_channel, &raw_value));

    // Convert to voltage using calibration (use curve fitting for ESP32-S3)
    adc_cali_handle_t adc1_cali_handle = NULL;
    adc_cali_curve_fitting_config_t cali_config = {};
    cali_config.unit_id = ADC_UNIT_1;
    cali_config.chan = adc_channel;
    cali_config.atten = ADC_ATTEN_DB_12;
    cali_config.bitwidth = ADC_BITWIDTH_12;
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc1_cali_handle));

    int voltage_mv = 0;
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_handle, raw_value, &voltage_mv));

    // Cleanup
    ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(adc1_cali_handle));
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));

#ifdef BATTERY_ADC_CTRL
    // Disable ADC (save power on Heltec boards)
    gpio_set_level((gpio_num_t)BATTERY_ADC_CTRL, 0);
#endif

    // Apply voltage divider ratio
    float battery_voltage = (voltage_mv / 1000.0f) * BATTERY_VOLTAGE_DIVIDER;

    ESP_LOGI(TAG, "Battery: %d mV (raw: %d) -> %.2f V", voltage_mv, raw_value, battery_voltage);

    return battery_voltage;
#endif
}

uint8_t PowerManager::readBatteryLevel()
{
    auto voltage = readBatteryVoltage();

    if (voltage <= 0.0f)
    {
        return 0; // Battery monitoring not available
    }

    // Clamp voltage to valid range
    if (voltage < BATTERY_MIN_VOLTAGE)
        voltage = BATTERY_MIN_VOLTAGE;
    if (voltage > BATTERY_MAX_VOLTAGE)
        voltage = BATTERY_MAX_VOLTAGE;

    // Convert to percentage (0-100)
    auto percentage = ((voltage - BATTERY_MIN_VOLTAGE) / (BATTERY_MAX_VOLTAGE - BATTERY_MIN_VOLTAGE)) * 100.0f;

    auto level = static_cast<uint8_t>(percentage);
    ESP_LOGI(TAG, "Battery level: %d%%", level);

    return level;
}

void PowerManager::disableWiFi()
{
    // Disable WiFi completely (saves ~50-80 mA)
    esp_err_t err = esp_wifi_stop();
    if (err == ESP_OK || err == ESP_ERR_WIFI_NOT_INIT)
    {
        esp_wifi_deinit();
        ESP_LOGI(TAG, "WiFi disabled successfully");
    }
    else
    {
        ESP_LOGE(TAG, "WiFi stop failed: %d", err);
    }
    periph_module_disable(PERIPH_WIFI_MODULE);
}

void PowerManager::disableBluetoothClassic()
{
    // Disable Bluetooth Classic (we only use BLE via NimBLE)
    esp_bt_mem_release(ESP_BT_MODE_CLASSIC_BT);
    ESP_LOGI(TAG, "Bluetooth Classic memory released");
}