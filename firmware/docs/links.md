
https://wiki.seeedstudio.com/XIAO_BLE/
https://github.com/maxgerhardt/framework-arduinoadafruitnrf52-seeed/blob/main/variants/Seeed_XIAO_nRF52840/variant.cpp

https://github.com/Heltec-Aaron-Lee/WiFi_Kit_series/blob/master/variants/heltec_wireless_stick_lite_v3/pins_arduino.h


https://github.com/meshtastic/firmware/blob/aa85fbbcc481516e2da2ff9744daff30b97a121f/variants/esp32s3/heltec_wsl_v3/variant.h#L10

/*
 * Battery
 */
#define ADC_CTRL 37
#define ADC_CTRL_ENABLED LOW
#define BATTERY_PIN 1 // A battery voltage measurement pin, voltage divider connected here to measure battery voltage
#define ADC_CHANNEL ADC1_GPIO1_CHANNEL
#define ADC_ATTENUATION ADC_ATTEN_DB_2_5 // lower dB for high resistance voltage divider
#define ADC_MULTIPLIER 4.9 * 1.045




https://github.com/meshtastic/firmware/blob/aa85fbbcc481516e2da2ff9744daff30b97a121f/variants/nrf52840/seeed_xiao_nrf52840_kit/variant.h#L117

/*
 * Battery
 */
#define BATTERY_PIN PIN_VBAT      // P0.31: VBAT voltage divider
#define ADC_MULTIPLIER (3)        // ... R17=1M, R18=510k
#define ADC_CTRL VBAT_ENABLE      // P0.14: VBAT voltage divider
#define ADC_CTRL_ENABLED LOW      // ... sink
#define EXT_CHRG_DETECT (23)      // P0.17: Charge LED
#define EXT_CHRG_DETECT_VALUE LOW // ... BQ25101 ~CHG indicates charging
#define HICHG (22)                // P0.13: BQ25101 ISET 100mA instead of 50mA

#define BATTERY_SENSE_RESOLUTION_BITS (10)