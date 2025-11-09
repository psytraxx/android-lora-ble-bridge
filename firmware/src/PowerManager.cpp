#include "PowerManager.h"
#include "FirmwareConfig.h"
#include "esp_pm.h"
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <driver/rtc_io.h>
#include <driver/spi_master.h>
#include <Arduino.h>
// peripheral control / driver headers for power optimizations
#include "driver/periph_ctrl.h"
#include "driver/i2c.h"
#include <SPI.h>


void PowerManager::configurePowerManagement()
{
    Serial.println("Configuring power management");

    esp_pm_config_esp32s3_t pm_config = {};
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
    Serial.println("Configuring wakeup sources for deep sleep");

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
        Serial.printf("Error: GPIO %d is not RTC-capable! Cannot use for deep sleep wakeup.\n", loraDio0);
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

    Serial.printf("Wakeup configured - Button (GPIO %d LOW), LoRa DIO0 (GPIO %d HIGH)\n",
                  wakeButton, loraDio0);
}

void PowerManager::enterDeepSleep()
{

    Serial.printf("DIO0 state before sleep: %d\n", gpio_get_level((gpio_num_t)LORA_DIO0));

    Serial.printf("Entering deep sleep...\n");
    Serial.printf("Will wake on:\n");
    Serial.printf("  - LoRa DIO0 going HIGH\n");
    Serial.printf("  - Wake Button going LOW\n");

    Serial.flush();
    SPI.end(); // Just in case, end SPI before sleep (should not return)
    // Enter deep sleep - device will reset on wake
    esp_deep_sleep_start();

    
}

void PowerManager::printWakeupReason()
{
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

    switch (wakeup_reason)
    {
    case ESP_SLEEP_WAKEUP_EXT0:
        Serial.printf("Woke: LoRa DIO0 (Deep Sleep)\n");
        break;
    case ESP_SLEEP_WAKEUP_EXT1:
        Serial.printf("Woke: Button (Deep Sleep)\n");
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        Serial.printf("Woke: Timer\n");
        break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
        Serial.printf("Woke: Touchpad\n");
        break;
    case ESP_SLEEP_WAKEUP_ULP:
        Serial.printf("Woke: ULP Program\n");
        break;
    case ESP_SLEEP_WAKEUP_GPIO:
        Serial.printf("Woke: GPIO\n");
        break;
    case ESP_SLEEP_WAKEUP_UART:
        Serial.printf("Woke: UART\n");
        break;
    case ESP_SLEEP_WAKEUP_WIFI:
        Serial.printf("Woke: WIFI\n");
        break;
    case ESP_SLEEP_WAKEUP_COCPU:
        Serial.printf("Woke: COCPU\n");
        break;
    case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG:
        Serial.printf("Woke: COCPU Crash\n");
        break;
    case ESP_SLEEP_WAKEUP_BT:
        Serial.printf("Woke: Bluetooth\n");
        break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
        Serial.printf("Power On / Reset\n");
        break;
    default:
        Serial.printf("Woke: Unknown\n");
        break;
    }
}
