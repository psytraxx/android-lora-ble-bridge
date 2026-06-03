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

## Seeed XIAO nRF52840 (`xiao_nrf52840`)

**Official**
- Wiki / getting started: https://wiki.seeedstudio.com/XIAO_BLE/

**Pinout / variants**
- Arduino variant (pin names, ADC, battery): https://github.com/maxgerhardt/framework-arduinoadafruitnrf52-seeed/blob/main/variants/Seeed_XIAO_nRF52840/variant.cpp
- Meshtastic variant (battery ADC reference — used for BATTERY_VOLTAGE_DIVIDER=3.0, ADC_CTRL=VBAT_ENABLE): https://github.com/meshtastic/firmware/blob/aa85fbbcc481516e2da2ff9744daff30b97a121f/variants/nrf52840/seeed_xiao_nrf52840_kit/variant.h#L117

---

## Generic / Framework

- Heltec Arduino framework repo (all ESP32-S3 variant files): https://github.com/Heltec-Aaron-Lee/WiFi_Kit_series
- PlatformIO pioarduino platform (used for all ESP32 envs): https://github.com/pioarduino/platform-espressif32

---

## Removal candidates

The following were working notes that are now fully implemented and no longer add value as references:

| Item | Reason safe to remove |
|---|---|
| Inline battery `#define` snippets (WSL V3 + XIAO) | Values already in `platformio.ini`; Meshtastic variant links above point to the same source |
| `"new model / Heltec WSL 3 with OLED display"` note | Board is now implemented and documented |
| Bare `https://github.com/Heltec-Aaron-Lee/WiFi_Kit_series` root link (duplicate of the one in Generic section) | Superseded by specific variant links above |
