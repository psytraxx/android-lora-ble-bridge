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

// ---- PeripheralPowerMgmt implementations migrated here ----
void PowerManager::disableADC()
{
    Serial.println("Disabling ADC peripheral");
    periph_module_disable(PERIPH_SARADC_MODULE);
}

void PowerManager::disableI2C()
{
    Serial.println("Disabling I2C peripherals");

    // Delete I2C drivers if initialized
    i2c_driver_delete(I2C_NUM_0);
    i2c_driver_delete(I2C_NUM_1);

    // Disable peripheral modules
    periph_module_disable(PERIPH_I2C0_MODULE);
    periph_module_disable(PERIPH_I2C1_MODULE);
}

void PowerManager::disableExtraUARTs()
{
    Serial.println("Disabling UART1/UART2 (keeping UART0 for debug)");

    // Delete UART drivers
    uart_driver_delete(UART_NUM_1);
    uart_driver_delete(UART_NUM_2);

    // Disable peripheral modules
    periph_module_disable(PERIPH_UART1_MODULE);
    periph_module_disable(PERIPH_UART2_MODULE);
}

void PowerManager::disableUnusedSPI()
{
    Serial.println("Disabling SPI3 (keeping SPI2 for LoRa)");

    // ONLY disable SPI3 - SPI2 is used by LoRa radio
    periph_module_disable(PERIPH_SPI3_MODULE);
}

void PowerManager::configureUnusedGPIOs(uint64_t usedPins)
{
    Serial.println("Configuring unused GPIOs with pull-ups");

    const int safeGPIOs[] = {
        1, 2, 4, 9, 14, 15, 16, 17, 18, 21,
        35, 36, 37, 38, 39, 40, 41, 42};

    int configured = 0;

    for (int gpio : safeGPIOs)
    {
        if (usedPins & (1ULL << gpio))
        {
            continue;
        }

        gpio_set_direction((gpio_num_t)gpio, GPIO_MODE_INPUT);
        gpio_set_pull_mode((gpio_num_t)gpio, GPIO_PULLUP_ONLY);
        gpio_pullup_en((gpio_num_t)gpio);
        gpio_pulldown_dis((gpio_num_t)gpio);

        configured++;
    }

    Serial.printf("Configured %d unused GPIOs for low power\n", configured);
}

void PowerManager::optimizeUnusedPeripherals(uint64_t usedGPIOs, bool disableGPIOConfig)
{
    Serial.println("Optimizing unused peripherals for power savings");

    disableADC();
    disableI2C();
    disableExtraUARTs();
    disableUnusedSPI();

    if (!disableGPIOConfig)
    {
        configureUnusedGPIOs(usedGPIOs);
    }

    Serial.println("Peripheral power optimization complete");
}

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

    // Flush UART0 to ensure all logs are sent before sleeping
    uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(100));
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
