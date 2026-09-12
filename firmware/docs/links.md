# Reference Links

---

## Heltec Wireless Stick Lite V3 (`heltec-wireless-stick-lite-v3`)

**Official**
- Product page: https://heltec.org/project/wireless-stick-lite-v3/

**Pinout / variants**
- Heltec repo pins: https://github.com/Heltec-Aaron-Lee/WiFi_Kit_series/blob/master/variants/heltec_wireless_stick_lite_v3/pins_arduino.h
- Meshtastic variant (battery ADC reference — used for BATTERY_VOLTAGE_DIVIDER=5.1205, ADC_CTRL=37): https://github.com/meshtastic/firmware/blob/aa85fbbcc481516e2da2ff9744daff30b97a121f/variants/esp32s3/heltec_wsl_v3/variant.h#L10

> **Note**: GPIO35=MOSI=LED, GPIO36=SCK=VEXT, GPIO37=MISO=ADC_CTRL — conflicting pins.
> LED_PIN, VEXT_PIN, and BATTERY_ADC_CTRL are intentionally omitted from platformio.ini for this env.

---

## Heltec Wireless Stick V3 (`heltec-wireless-stick-v3`)

**Official**
- Product page: https://heltec.org/project/wireless-stick-v3/
- I²C / OLED pin discussion: http://community.heltec.cn/t/heltec-v3-and-wsl-v3-i2c-pins/12382

**Pinout / variants**
- Heltec repo pins: https://github.com/Heltec-Aaron-Lee/WiFi_Kit_series/blob/master/variants/heltec_wireless_stick_v3/pins_arduino.h

> **Note**: 64×32 SSD1306 OLED on SDA=17/SCL=18/RST=21, powered via VEXT (GPIO36, active LOW).
> Same LoRa SPI pins as Lite V3. Board JSON lives in `firmware/boards/heltec_wireless_stick_v3.json`.

---

## Seeed XIAO nRF52840 + Wio-SX1262 (`xiao_nrf52840`)

**Official**
- Seeed XIAO nRF52840 wiki: https://wiki.seeedstudio.com/XIAO_BLE/
- Wio-SX1262 LoRa module (radio + `LORA_RF_SWITCH` pin reference): [Wio-SX1262 for XIAO V1.0.pdf](Wio-SX1262%20for%20XIAO%20V1.0.pdf)

**Pinout / variants**
- XIAO nRF52840 pinout reference: [XIAO-nRF52840-pinout_sheet.xlsx](XIAO-nRF52840-pinout_sheet.xlsx)
- Board photo: [imageXIAO_nRF52840-2.webp](imageXIAO_nRF52840-2.webp)
- Module photo: [image_Wio-SX1262_-1.webp](image_Wio-SX1262_-1.webp)

> **Note**: This env runs the SX1262 radio at 869.525 MHz (EU868), not 433.92 MHz like the Heltec boards, and defines `LORA_RF_SWITCH=D5` for the Wio-SX1262's RF switch control pin.

---

## Generic / Framework

- Heltec Arduino framework repo (all ESP32-S3 variant files): https://github.com/Heltec-Aaron-Lee/WiFi_Kit_series
- PlatformIO pioarduino platform (used for all ESP32 envs): https://github.com/pioarduino/platform-espressif32
