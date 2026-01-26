/**
 * Protocol Serialization/Deserialization (Little-Endian)
 *
 * Binary message format matching ESP32 firmware
 */

import { pack6Bit, unpack6Bit } from './encoding';
import {
  type AckMessage,
  GPS_PRECISION_MULTIPLIER,
  MAX_TEXT_LENGTH,
  MESSAGE_TYPE,
  type Message,
  type TextMessage
} from './types';

/**
 * Serializes a Message to binary format
 *
 * TextMessage: [Type:1][Seq:1][CharCount:1][PackedLen:1][Packed:N][HasGPS:1][Lat?:4][Lon?:4]
 * AckMessage: [Type:1][Seq:1]
 *
 * @param message Message to serialize
 * @returns Binary representation
 */
export function serialize(message: Message): Uint8Array {
  switch (message.type) {
    case MESSAGE_TYPE.TEXT:
      return serializeTextMessage(message);
    case MESSAGE_TYPE.ACK:
      return serializeAckMessage(message);
    default:
      throw new Error(`Unknown message type: ${(message as Message).type}`);
  }
}

/**
 * Deserializes binary data to a Message
 *
 * @param data Binary data
 * @returns Parsed Message
 * @throws Error if data is invalid or corrupted
 */
export function deserialize(data: Uint8Array): Message {
  if (data.length === 0) {
    throw new Error('Cannot deserialize empty data');
  }

  const type = data[0];

  switch (type) {
    case MESSAGE_TYPE.TEXT:
      return deserializeTextMessage(data);
    case MESSAGE_TYPE.ACK:
      return deserializeAckMessage(data);
    default:
      throw new Error(`Unknown message type: 0x${type.toString(16)}`);
  }
}

/**
 * Serializes TextMessage
 */
function serializeTextMessage(msg: TextMessage): Uint8Array {
  if (msg.text.length > MAX_TEXT_LENGTH) {
    throw new Error(`Text length ${msg.text.length} exceeds maximum ${MAX_TEXT_LENGTH}`);
  }

  const packed = pack6Bit(msg.text);
  const charCount = msg.text.length;
  const packedLength = packed.length;

  const hasGps = msg.hasGps && msg.latitude != null && msg.longitude != null;
  const size = 5 + packedLength + (hasGps ? 8 : 0);
  const buffer = new Uint8Array(size);

  let offset = 0;

  // Header
  buffer[offset++] = MESSAGE_TYPE.TEXT;
  buffer[offset++] = msg.seq & 0xff;
  buffer[offset++] = charCount;
  buffer[offset++] = packedLength;

  // Packed text
  buffer.set(packed, offset);
  offset += packedLength;

  // GPS flag
  buffer[offset++] = hasGps ? 1 : 0;

  // GPS coordinates (little-endian int32)
  if (hasGps && msg.latitude != null && msg.longitude != null) {
    const latMicro = Math.round(msg.latitude * GPS_PRECISION_MULTIPLIER);
    const lonMicro = Math.round(msg.longitude * GPS_PRECISION_MULTIPLIER);

    writeInt32LE(buffer, offset, latMicro);
    offset += 4;
    writeInt32LE(buffer, offset, lonMicro);
    offset += 4;
  }

  return buffer;
}

/**
 * Deserializes TextMessage
 */
function deserializeTextMessage(data: Uint8Array): TextMessage {
  if (data.length < 5) {
    throw new Error(`TextMessage too short: ${data.length} bytes`);
  }

  let offset = 1; // Skip type byte

  const seq = data[offset++];
  const charCount = data[offset++];
  const packedLength = data[offset++];

  if (data.length < 5 + packedLength) {
    throw new Error(
      `Insufficient data for packed text: expected ${5 + packedLength}, got ${data.length}`
    );
  }

  const packed = data.slice(offset, offset + packedLength);
  offset += packedLength;

  const text = unpack6Bit(packed, charCount);

  if (offset >= data.length) {
    throw new Error('Missing GPS flag byte');
  }

  const hasGps = data[offset++] === 1;

  let latitude: number | undefined;
  let longitude: number | undefined;

  if (hasGps) {
    if (data.length < offset + 8) {
      throw new Error('Insufficient data for GPS coordinates');
    }

    const latMicro = readInt32LE(data, offset);
    offset += 4;
    const lonMicro = readInt32LE(data, offset);
    offset += 4;

    latitude = latMicro / GPS_PRECISION_MULTIPLIER;
    longitude = lonMicro / GPS_PRECISION_MULTIPLIER;
  }

  return {
    type: MESSAGE_TYPE.TEXT,
    seq,
    text,
    hasGps,
    latitude,
    longitude
  };
}

/**
 * Serializes AckMessage
 */
function serializeAckMessage(msg: AckMessage): Uint8Array {
  const buffer = new Uint8Array(2);
  buffer[0] = MESSAGE_TYPE.ACK;
  buffer[1] = msg.seq & 0xff;
  return buffer;
}

/**
 * Deserializes AckMessage
 */
function deserializeAckMessage(data: Uint8Array): AckMessage {
  if (data.length < 2) {
    throw new Error(`AckMessage too short: ${data.length} bytes`);
  }

  return {
    type: MESSAGE_TYPE.ACK,
    seq: data[1]
  };
}

/**
 * Writes int32 in little-endian format
 */
function writeInt32LE(buffer: Uint8Array, offset: number, value: number): void {
  buffer[offset] = value & 0xff;
  buffer[offset + 1] = (value >> 8) & 0xff;
  buffer[offset + 2] = (value >> 16) & 0xff;
  buffer[offset + 3] = (value >> 24) & 0xff;
}

/**
 * Reads int32 in little-endian format
 */
function readInt32LE(buffer: Uint8Array, offset: number): number {
  return (
    buffer[offset] |
    (buffer[offset + 1] << 8) |
    (buffer[offset + 2] << 16) |
    (buffer[offset + 3] << 24)
  );
}
