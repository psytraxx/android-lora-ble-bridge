# **ESP32 LoRa-BLE Gateway: Use Cases**

This document outlines the primary user scenarios and system behaviors based on the power-saving architecture.

---
### Testing

## **1\. Device Startup**

* **Action:** The device is powered on or reset.  
* **Response:** The system initializes and immediately enters "Active Advertising" mode for 30 seconds to allow a user to connect.

## **2\. Disconnected & Idle**

* **Scenario:** The device is on but not connected to the Android app.
* **Behavior:** The device is in a power-saving loop:
  1. **Advertise:** Actively advertises via BLE for 30 seconds.
  2. **Sleep:** Enters Light Sleep indefinitely until woken by boot button press or LoRa activity.
  3. Press the boot button (GPIO0) to wake the device and restart advertising.
  4. This cycle ensures maximum power savings while allowing manual wake via button.

## **3\. Receiving LoRa Data (While Disconnected)**

* **Scenario:** A LoRa message arrives while the device is in Light Sleep state.
* **Behavior:**
  1. The LoRa module triggers a GPIO pin (DIO0), instantly waking the ESP32.
  2. The device receives the LoRa message.
  3. The message is stored in an internal buffer (to be delivered later).
  4. **The device starts advertising for 30 seconds** so the user can connect and retrieve the buffered message.
  5. If no connection is made during the 30-second advertising window, the device returns to Light Sleep.

## **4\. Connecting the Android App**

* **Scenario:** The user presses the boot button (or a LoRa message arrives) to wake the device, which starts advertising. The user then opens the Android app and connects during the 30-second advertising window.
* **Behavior:**
  1. A BLE connection is established.
  2. The device waits 1000ms for Android BLE stack setup (MTU negotiation, service discovery, notification enablement).
  3. After the stabilization period, the device uploads the entire buffer of stored LoRa messages to the app.
  4. After the sync is complete, the device enters the "Always Active" connected mode.
  5. **A 60-second inactivity timer starts.** The device will remain active and connected for at least 60 seconds.
  6. Any BLE or LoRa activity resets the timer back to 60 seconds.

## **5\. Relaying App Message to LoRa (While Connected)**

* **Scenario:** The user is connected and sends a message from the app.
* **Behavior:**
  1. The device is in "Always Active" mode (no power saving while connected).
  2. It receives the message via BLE.
  3. It immediately transmits that message over the LoRa radio.
  4. **Any BLE or LoRa activity resets the 60-second inactivity timer.**
  5. It stays active, awaiting the next command.

## **6\. Disconnecting the App (Manual)**

* **Scenario:** The user manually disconnects from the device within the app.  
* **Behavior:**  
  1. The BLE connection is terminated.  
  2. The device immediately reverts to the "Disconnected & Idle" power-saving loop (starting with 30 seconds of advertising).

## **7\. Disconnecting (Automatic Inactivity)**

* **Scenario:** The device is connected, but there is no communication (no BLE or LoRa activity) for 60 seconds.
* **Behavior:**
  1. When BLE connects, the device stays fully active (no light sleep) for at least 60 seconds.
  2. **Any BLE or LoRa activity automatically renews the 60-second timer.**
  3. After 60 seconds of complete inactivity (no BLE messages, no LoRa packets), the inactivity timer expires.
  4. The device *automatically* terminates the BLE connection to save power.
  5. The device reverts to the "Disconnected & Idle" power-saving loop (starting with 30 seconds of advertising).
