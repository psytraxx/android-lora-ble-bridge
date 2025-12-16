/**
 * LoRa Protocol v3.0 - Type Definitions
 *
 * Binary message format for ESP32-S3 LoRa communication
 */

export const MESSAGE_TYPE = {
  TEXT: 0x01,
  ACK: 0x02,
  WAKE_UP: 0x03
} as const;

export type MessageType = (typeof MESSAGE_TYPE)[keyof typeof MESSAGE_TYPE];

/**
 * TextMessage: [Type][Seq][CharCount][PackedLen][PackedText][HasGPS][Lat?][Lon?]
 * Max size: 52 bytes (5 header + 38 text + 9 GPS)
 */
export interface TextMessage {
  type: typeof MESSAGE_TYPE.TEXT;
  seq: number; // 0-255
  text: string; // 1-50 characters
  hasGps: boolean;
  latitude?: number; // degrees
  longitude?: number; // degrees
}

/**
 * AckMessage: [Type][Seq]
 * Size: 2 bytes
 */
export interface AckMessage {
  type: typeof MESSAGE_TYPE.ACK;
  seq: number; // 0-255, matches TextMessage sequence
}

/**
 * WakeUpMessage: [Type]
 * Size: 1 byte (LoRa-only, never sent via BLE)
 */
export interface WakeUpMessage {
  type: typeof MESSAGE_TYPE.WAKE_UP;
}

export type Message = TextMessage | AckMessage | WakeUpMessage;

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
