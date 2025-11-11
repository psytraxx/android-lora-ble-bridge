/**
 * 6-bit Character Encoding/Decoding
 *
 * Packs uppercase text into 6 bits per character for bandwidth efficiency
 * 50 chars × 6 bits = 300 bits = 38 bytes (vs 50 bytes UTF-8)
 * 24% bandwidth savings
 */

import { CHAR_SET, MAX_TEXT_LENGTH } from './types';

/**
 * Character lookup map for encoding
 */
const charToIndex = new Map<string, number>();
for (let i = 0; i < CHAR_SET.length; i++) {
  charToIndex.set(CHAR_SET[i], i);
}

/**
 * Validates if text contains only supported characters
 */
export function isValidText(text: string): boolean {
  const upper = text.toUpperCase();
  for (let i = 0; i < upper.length; i++) {
    if (!charToIndex.has(upper[i])) {
      return false;
    }
  }
  return true;
}

/**
 * Gets list of unsupported characters in text
 */
export function getUnsupportedChars(text: string): string[] {
  const upper = text.toUpperCase();
  const unsupported = new Set<string>();
  for (let i = 0; i < upper.length; i++) {
    if (!charToIndex.has(upper[i])) {
      unsupported.add(text[i]); // Keep original case for display
    }
  }
  return Array.from(unsupported);
}

/**
 * Packs text into 6-bit encoded bytes
 *
 * Algorithm:
 * - Each character → 6-bit index
 * - Pack bits into byte array (big-endian bit order within packing)
 * - Length = (charCount × 6 + 7) / 8
 *
 * @param text Input text (auto-converts to uppercase)
 * @returns Packed byte array
 * @throws Error if text contains unsupported characters or exceeds max length
 */
export function pack6Bit(text: string): Uint8Array {
  const upper = text.toUpperCase();

  if (upper.length > MAX_TEXT_LENGTH) {
    throw new Error(`Text exceeds maximum length of ${MAX_TEXT_LENGTH} characters`);
  }

  const unsupported = getUnsupportedChars(text);
  if (unsupported.length > 0) {
    throw new Error(`Unsupported characters: ${unsupported.join(', ')}`);
  }

  const charCount = upper.length;
  const packedLength = Math.floor((charCount * 6 + 7) / 8);
  const packed = new Uint8Array(packedLength);

  let bitOffset = 0;

  for (let i = 0; i < charCount; i++) {
    const char = upper[i];
    const index = charToIndex.get(char);

    if (index === undefined) {
      throw new Error(`Character "${char}" at position ${i} is not supported.`);
    }

    // Write 6 bits to output
    for (let bit = 5; bit >= 0; bit--) {
      const bitValue = (index >> bit) & 1;
      const byteIndex = Math.floor(bitOffset / 8);
      const bitPosition = 7 - (bitOffset % 8);

      if (bitValue) {
        packed[byteIndex] |= 1 << bitPosition;
      }

      bitOffset++;
    }
  }

  return packed;
}

/**
 * Unpacks 6-bit encoded bytes into text
 *
 * @param packed Packed byte array
 * @param charCount Number of characters to decode
 * @returns Decoded uppercase text
 * @throws Error if charCount exceeds max or packed data is insufficient
 */
export function unpack6Bit(packed: Uint8Array, charCount: number): string {
  if (charCount > MAX_TEXT_LENGTH) {
    throw new Error(`Character count ${charCount} exceeds maximum ${MAX_TEXT_LENGTH}`);
  }

  const expectedLength = Math.floor((charCount * 6 + 7) / 8);
  if (packed.length < expectedLength) {
    throw new Error(
      `Insufficient packed data: expected ${expectedLength} bytes, got ${packed.length}`
    );
  }

  let result = '';
  let bitOffset = 0;

  for (let i = 0; i < charCount; i++) {
    let index = 0;

    // Read 6 bits
    for (let bit = 5; bit >= 0; bit--) {
      const byteIndex = Math.floor(bitOffset / 8);
      const bitPosition = 7 - (bitOffset % 8);
      const bitValue = (packed[byteIndex] >> bitPosition) & 1;

      index |= bitValue << bit;
      bitOffset++;
    }

    if (index >= CHAR_SET.length) {
      throw new Error(`Invalid character index ${index} at position ${i}`);
    }

    result += CHAR_SET[index];
  }

  return result;
}

/**
 * Calculates packed byte length for given character count
 */
export function calculatePackedLength(charCount: number): number {
  return Math.floor((charCount * 6 + 7) / 8);
}

/**
 * Calculates actual byte size of a text message for display
 * Format: Type(1) + Seq(1) + CharCount(1) + PackedLen(1) + Packed(N) + HasGPS(1) + [GPS(8)]
 */
export function calculateMessageSize(text: string, hasGps: boolean): number {
  const charCount = text.length;
  const packedLength = calculatePackedLength(charCount);
  const headerSize = 4; // Type + Seq + CharCount + PackedLen
  const gpsSize = hasGps ? 9 : 1; // HasGPS byte + optional Lat(4) + Lon(4)

  return headerSize + packedLength + gpsSize;
}
