#include "PowerManager.h"
#include "FirmwareConfig.h"
#include "esp_pm.h"
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <driver/rtc_io.h>
#include <esp_log.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_wifi.h>
#include <esp_task_wdt.h>
#include <esp_bt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "PWR";

const float MIN_VOLTAGE = 3.04;
const float MAX_VOLTAGE = 4.26;
const uint8_t SCALED_VOLTAGE[100] = {
    254,
    242,
    230,
    227,
    223,
    219,
    215,
    213,
    210,
    207,
    206,
    202,
    202,
    200,
    200,
    199,
    198,
    198,
    196,
    196,
    195,
    195,
    194,
    192,
    191,
    188,
    187,
    185,
    185,
    185,
    183,
    182,
    180,
    179,
    178,
    175,
    175,
    174,
    172,
    171,
    170,
    169,
    168,
    166,
    166,
    165,
    165,
    164,
    161,
    161,
    159,
    158,
    158,
    157,
    156,
    155,
    151,
    148,
    147,
    145,
    143,
    142,
    140,
    140,
    136,
    132,
    130,
    130,
    129,
    126,
    125,
    124,
    121,
    120,
    118,
    116,
    115,
    114,
    112,
    112,
    110,
    110,
    108,
    106,
    106,
    104,
    102,
    101,
    99,
    97,
    94,
    90,
    81,
    80,
    76,
    73,
    66,
    52,
    32,
    7,
};
void PowerManager::configurePowerManagement()
{
    ESP_LOGI(TAG, "Configuring power management");

    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = PowerConstants::CPU_MIN_FREQ_MHZ,
        .light_sleep_enable = false}; // Disable light sleep to avoid issues with peripherals

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

    // Step 1: Disable external peripherals via VEXT
    disableExternalPeripherals();

    // Step 2: Set unused GPIOs to input mode to minimize leakage current
    setUnusedGPIOsToInput();

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
    adc_channel_t adc_channel;

#if BATTERY_ADC_PIN == 1
    adc_channel = ADC_CHANNEL_0; // GPIO1
#elif BATTERY_ADC_PIN == 4
    adc_channel = ADC_CHANNEL_3; // GPIO4
#else
    ESP_LOGE(TAG, "Unsupported battery ADC pin: %d", BATTERY_ADC_PIN);
    return 0.0f;
#endif

#ifdef BATTERY_ADC_CTRL
    // Enable ADC (Heltec boards) - set LOW per Heltec example
    gpio_set_direction((gpio_num_t)BATTERY_ADC_CTRL, GPIO_MODE_OUTPUT);
    gpio_set_level((gpio_num_t)BATTERY_ADC_CTRL, 0);
    vTaskDelay(pdMS_TO_TICKS(5));
#endif

    // Initialize ADC oneshot
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    // Configure channel: 12-bit width, 12dB attenuation (0-3.3V range)
    adc_oneshot_chan_cfg_t chan_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, adc_channel, &chan_config));

    // Read and convert directly (Heltec style: raw / 238.7)
    // Calibration factor accounts for 12-bit ADC range and voltage divider
    int raw_value = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, adc_channel, &raw_value));
    float battery_voltage = raw_value / 238.7f;

    // Cleanup
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc_handle));

#ifdef BATTERY_ADC_CTRL
    // Disable ADC (save power on Heltec boards) - set to INPUT, pulled up
    gpio_set_direction((gpio_num_t)BATTERY_ADC_CTRL, GPIO_MODE_INPUT);
#endif

    ESP_LOGI(TAG, "Battery: raw %d -> %.2f V", raw_value, battery_voltage);

    return battery_voltage;
#endif
}

uint8_t PowerManager::readBatteryLevel()
{
    float voltage = readBatteryVoltage();

    if (voltage <= 0.0f)
    {
        return 0; // Battery monitoring not available
    }

    // Use voltage curve for more accurate percentage calculation
    uint8_t level = voltageToPercentage(voltage);
    ESP_LOGI(TAG, "Battery level: %d%% (%.2fV)", level, voltage);

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
}

void PowerManager::disableBluetoothClassic()
{
    // Disable Bluetooth Classic (we only use BLE via NimBLE)
    esp_bt_mem_release(ESP_BT_MODE_CLASSIC_BT);
    ESP_LOGI(TAG, "Bluetooth Classic memory released");
}

// Private helper functions

void PowerManager::disableExternalPeripherals()
{
#ifdef VEXT_PIN
    // On Heltec boards, VEXT (GPIO 36) controls power to external peripherals
    // Setting it to INPUT mode cuts power (high-impedance state)
    gpio_set_direction((gpio_num_t)VEXT_PIN, GPIO_MODE_INPUT);
    ESP_LOGI(TAG, "VEXT (GPIO %d) set to input - external peripherals powered off", VEXT_PIN);
#else
    ESP_LOGD(TAG, "VEXT not defined for this board - skipping");
#endif
}

void PowerManager::setUnusedGPIOsToInput()
{
    // Set all unused GPIOs to input mode to minimize leakage current during deep sleep
    // This excludes pins actively used by the system

    ESP_LOGI(TAG, "Setting unused GPIOs to input mode");

    // List of GPIOs that are IN USE (should NOT be changed):
    // - LoRa SPI: LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS, LORA_RST, LORA_DIO0, LORA_BUSY
    // - Battery: BATTERY_ADC_PIN, BATTERY_ADC_CTRL (if defined)
    // - Wake: WAKE_BUTTON
    // - LED: LED_PIN (if defined)
    // - VEXT: VEXT_PIN (if defined)

    // ESP32-S3 valid GPIOs: 0-48 (excluding strapping pins and USB)
    // We'll set commonly unused GPIOs to input

    const gpio_num_t used_gpios[] = {
        (gpio_num_t)LORA_SCK,
        (gpio_num_t)LORA_MISO,
        (gpio_num_t)LORA_MOSI,
        (gpio_num_t)LORA_SS,
        (gpio_num_t)LORA_RST,
        (gpio_num_t)LORA_DIO0,
        (gpio_num_t)LORA_BUSY,
        (gpio_num_t)WAKE_BUTTON,
#ifdef BATTERY_ADC_PIN
        (gpio_num_t)BATTERY_ADC_PIN,
#endif
#ifdef BATTERY_ADC_CTRL
        (gpio_num_t)BATTERY_ADC_CTRL,
#endif
#ifdef LED_PIN
        (gpio_num_t)LED_PIN,
#endif
#ifdef VEXT_PIN
        (gpio_num_t)VEXT_PIN,
#endif
    };

    const int num_used_gpios = sizeof(used_gpios) / sizeof(used_gpios[0]);

    // Set unused GPIOs to input (iterate through common GPIO range)
    for (int gpio = 0; gpio <= 21; gpio++)
    {
        // Skip GPIOs that are in use
        bool is_used = false;
        for (int i = 0; i < num_used_gpios; i++)
        {
            if (gpio == used_gpios[i])
            {
                is_used = true;
                break;
            }
        }

        if (!is_used && GPIO_IS_VALID_GPIO(gpio))
        {
            gpio_set_direction((gpio_num_t)gpio, GPIO_MODE_INPUT);
        }
    }

    ESP_LOGI(TAG, "Unused GPIOs set to input mode");
}

uint8_t PowerManager::voltageToPercentage(float voltage)
{
    // Handle out-of-range voltages
    if (voltage <= MIN_VOLTAGE)
    {
        return 0;
    }
    if (voltage >= MAX_VOLTAGE)
    {
        return 100;
    }

    for (int n = 0; n < sizeof(SCALED_VOLTAGE); n++)
    {
        float step = (MAX_VOLTAGE - MIN_VOLTAGE) / 256;
        if (voltage > MIN_VOLTAGE + (step * SCALED_VOLTAGE[n]))
        {
            return 100 - n;
        }
    }
    return 0;
}