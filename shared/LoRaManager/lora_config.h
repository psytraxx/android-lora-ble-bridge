#ifndef LORA_CONFIG_H
#define LORA_CONFIG_H

#include <stdint.h> // For fixed-width integer types

// LoRa configuration parameters optimized for long-range communication
// SF10 + BW125 provides excellent range (5-10 km) with reasonable data rate
// SF10 at 433.92MHz: ~550ms ToA for 42 bytes (max message size with 50 char text)

/**
 * @brief LoRa frequency.
 * 433.92 MHz - standard frequency for 433 MHz ISM band
 */
#define LORA_FREQUENCY 433920000UL // 433.92 MHz

/**
 * @brief LoRa bandwidth.
 * Narrower BW = better sensitivity, longer range
 */
#define LORA_BANDWIDTH 31E3 // 31 kHz

/**
 * @brief LoRa spreading factor.
 * Higher SF = longer range, slower speed
 */
#define LORA_SPREADING_FACTOR 11 // SF11 for long range

/**
 * @brief LoRa coding rate.
 * Good error correction
 */
#define LORA_CODING_RATE 5 // 4/5 coding rate

/**
 * @brief LoRa TX power.
 * 14 dBm (~25 mW) - check local regulations for 433 MHz ISM band
 * SX1278 supports 2-20 dBm
 */
#define LORA_TX_POWER 20 // dBm

#endif // LORA_CONFIG_H
