/*
 * Antenna Comparison Test for SX1262/SX1268
 *
 * Usage:
 * 1. Flash one device as TX (MODE_TX), place at fixed location
 * 2. Flash second device as RX (MODE_RX)
 * 3. Run RX with antenna A, note the average RSSI/SNR
 * 4. Swap to antenna B, compare results
 *
 * Better antenna = higher RSSI (less negative) and higher SNR
 */

#include <Arduino.h>
#include <RadioLib.h>

// ===== CONFIGURATION =====
// Set ONE of these to true
#define MODE_TX false
#define MODE_RX true

// Heltec WiFi LoRa v3 pins
#define LORA_SCK   9
#define LORA_MISO  11
#define LORA_MOSI  10
#define LORA_SS    8
#define LORA_RST   12
#define LORA_DIO1  14
#define LORA_BUSY  13

// Match your project settings
#define FREQUENCY       433.92
#define BANDWIDTH       250.0
#define SPREADING_FACTOR 11
#define CODING_RATE     5
#define SYNC_WORD       0x12
#define TX_POWER        20
#define PREAMBLE_LENGTH 64

// Test settings
#define TX_INTERVAL_MS  1000
#define PACKET_PAYLOAD  "ANTENNA_TEST"

// ===== GLOBALS =====
SPIClass spi(HSPI);
SX1262 radio = new Module(LORA_SS, LORA_DIO1, LORA_RST, LORA_BUSY, spi);

uint32_t packetCount = 0;
uint32_t packetReceived = 0;
float rssiSum = 0;
float snrSum = 0;
float rssiMin = 0;
float rssiMax = -200;

// Interrupt flag
volatile bool receivedFlag = false;

// Forward declarations
void transmitLoop();
void receiveLoop();
void printSummary();

// ISR for packet received
#if defined(ESP8266) || defined(ESP32)
  ICACHE_RAM_ATTR
#endif
void setFlag(void) {
  receivedFlag = true;
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000);

  Serial.println("\n=== Antenna Comparison Test ===");
  Serial.println(MODE_TX ? "Mode: TRANSMITTER" : "Mode: RECEIVER");
  Serial.println();

  // Initialize SPI
  spi.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);

  // Initialize radio
  Serial.print("[SX1262] Initializing... ");
  int state = radio.begin(
    FREQUENCY,
    BANDWIDTH,
    SPREADING_FACTOR,
    CODING_RATE,
    SYNC_WORD,
    TX_POWER,
    PREAMBLE_LENGTH
  );

  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("FAILED! Code: ");
    Serial.println(state);
    while (true) delay(1000);
  }
  Serial.println("OK");

  // SX1262-specific setup
  radio.setTCXO(1.8);
  radio.setDio2AsRfSwitch(true);
  radio.setCurrentLimit(140.0);
  radio.setCRC(true);

  if (MODE_RX) {
    // Set up interrupt
    radio.setPacketReceivedAction(setFlag);

    // Start receiving
    state = radio.startReceive();
    if (state != RADIOLIB_ERR_NONE) {
      Serial.print("startReceive failed: ");
      Serial.println(state);
    }

    Serial.println("\nListening for packets...");
    Serial.println("RSSI: higher (less negative) = better");
    Serial.println("SNR:  higher = better\n");
    Serial.println("Pkt#\tRSSI\tSNR\tAvgRSSI\tAvgSNR");
    Serial.println("----\t----\t---\t-------\t------");
  } else {
    Serial.println("\nTransmitting test packets...");
  }
}

void loop() {
  if (MODE_TX) {
    transmitLoop();
  } else {
    receiveLoop();
  }
}

void transmitLoop() {
  packetCount++;

  // Build packet with counter
  String packet = String(PACKET_PAYLOAD) + ":" + String(packetCount);

  Serial.print("TX #");
  Serial.print(packetCount);
  Serial.print(" ... ");

  int state = radio.transmit(packet);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("OK");
  } else {
    Serial.print("FAILED! Code: ");
    Serial.println(state);
  }

  delay(TX_INTERVAL_MS);
}

void receiveLoop() {
  // Check if packet received via interrupt
  if (!receivedFlag) {
    return;
  }

  // Reset flag
  receivedFlag = false;

  // Read the packet
  String data;
  int state = radio.readData(data);

  if (state == RADIOLIB_ERR_NONE) {
    packetReceived++;

    float rssi = radio.getRSSI();
    float snr = radio.getSNR();

    // Update stats
    rssiSum += rssi;
    snrSum += snr;
    if (rssi > rssiMax) rssiMax = rssi;
    if (rssi < rssiMin) rssiMin = rssi;

    float avgRssi = rssiSum / packetReceived;
    float avgSnr = snrSum / packetReceived;

    // Print results
    Serial.print(packetReceived);
    Serial.print("\t");
    Serial.print(rssi, 1);
    Serial.print("\t");
    Serial.print(snr, 1);
    Serial.print("\t");
    Serial.print(avgRssi, 1);
    Serial.print("\t");
    Serial.println(avgSnr, 1);

    // Every 10 packets, print summary
    if (packetReceived % 10 == 0) {
      printSummary();
    }
  } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
    Serial.println("CRC error");
  } else {
    Serial.print("RX Error: ");
    Serial.println(state);
  }

  // Restart receive
  radio.startReceive();
}

void printSummary() {
  Serial.println("\n--- Summary after " + String(packetReceived) + " packets ---");
  Serial.print("Avg RSSI: ");
  Serial.print(rssiSum / packetReceived, 1);
  Serial.println(" dBm");
  Serial.print("Avg SNR:  ");
  Serial.print(snrSum / packetReceived, 1);
  Serial.println(" dB");
  Serial.print("RSSI Range: ");
  Serial.print(rssiMin, 1);
  Serial.print(" to ");
  Serial.print(rssiMax, 1);
  Serial.println(" dBm");
  Serial.println("----------------------------------\n");
  Serial.println("Pkt#\tRSSI\tSNR\tAvgRSSI\tAvgSNR");
  Serial.println("----\t----\t---\t-------\t------");
}
