/**
 * LoRa Protocol v3.5 - Type Definitions
 *
 * Binary message format for ESP32-S3 LoRa communication
 */

export const MESSAGE_TYPE = {
  TEXT: 0x01,
  ACK: 0x02
} as const;

export type MessageType = (typeof MESSAGE_TYPE)[keyof typeof MESSAGE_TYPE];

/**
 * TextMessage: [Type][Seq][CharCount][PackedLen][PackedText][HasGPS][Lat?][Lon?][SenderTime]
 * Max size: 55 bytes (5 header + 38 text + 8 GPS + 4 sender time)
 */
export interface TextMessage {
  type: typeof MESSAGE_TYPE.TEXT;
  seq: number; // 0-255
  text: string; // 1-50 characters
  hasGps: boolean;
  latitude?: number; // degrees
  longitude?: number; // degrees
  senderTime?: number; // Unix time (seconds)
}

/**
 * AckMessage: [Type][Seq]
 * Size: 2 bytes
 */
export interface AckMessage {
  type: typeof MESSAGE_TYPE.ACK;
  seq: number; // 0-255, matches TextMessage sequence
}

export type Message = TextMessage | AckMessage;

/**
 * Device info from BLE read characteristic (16 bytes)
 * [Battery:1][RSSI:2 LE][SNR:2 LE][TxPower:1][Freq:4 LE][BW:4 LE][SF:1][CR:1]
 */
export interface DeviceInfo {
  batteryLevel: number; // 0-100%
  rssi: number; // dBm (int16)
  snr: number; // dB (float, decoded from int16 × 100)
  txPower: number; // dBm (int8)
  frequencyHz: number; // Hz
  bandwidthHz: number; // Hz
  spreadingFactor: number; // 7-12
  codingRate: number; // 5-8
}

/**
 * 6-bit character set (64 characters total)
 * Space + A-Z + 0-9 + punctuation
 */
export const CHAR_SET = ' ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?-:;\'"@#$%&*()[]{}=+/<>_';

export const MAX_TEXT_LENGTH = 50;
export const MAX_PACKED_LENGTH = 38; // 50 chars × 6 bits = 300 bits = 37.5 → 38 bytes

/**
 * GPS encoding: degrees × 1,000,000 → int32 (microdegrees)
 * Precision: ~1 meter
 */
export const GPS_PRECISION_MULTIPLIER = 1_000_000;

/**
 * Protocol constants
 */
export const PROTOCOL = {
  MAX_TEXT_LENGTH,
  MAX_PACKED_LENGTH,
  CHAR_SET,
  MESSAGE_TYPE,
  GPS_PRECISION_MULTIPLIER,
  ACK_TIMEOUT_MS: 10_000
} as const;
