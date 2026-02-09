import { describe, expect, it } from 'vitest';
import { deserialize, serialize } from './serialization';
import { type AckMessage, MESSAGE_TYPE, type TextMessage } from './types';

describe('LoRa Protocol Serialization/Deserialization', () => {
  it('should serialize and deserialize a TextMessage without GPS', () => {
    const originalMessage: TextMessage = {
      type: MESSAGE_TYPE.TEXT,
      seq: 42,
      text: 'HELLO',
      hasGps: false,
      senderTime: 1234567890
    };

    const serialized = serialize(originalMessage);
    const deserialized = deserialize(serialized) as TextMessage;

    expect(deserialized.type).toBe(MESSAGE_TYPE.TEXT);
    expect(deserialized.seq).toBe(originalMessage.seq);
    expect(deserialized.text).toBe(originalMessage.text);
    expect(deserialized.hasGps).toBe(false);
    expect(deserialized.latitude).toBeUndefined();
    expect(deserialized.longitude).toBeUndefined();
    expect(deserialized.senderTime).toBe(originalMessage.senderTime);
  });

  it('should serialize and deserialize a TextMessage with GPS', () => {
    const originalMessage: TextMessage = {
      type: MESSAGE_TYPE.TEXT,
      seq: 99,
      text: 'LOCATION',
      hasGps: true,
      latitude: 47.123456,
      longitude: 8.987654,
      senderTime: 987654321
    };

    const serialized = serialize(originalMessage);
    const deserialized = deserialize(serialized) as TextMessage;

    expect(deserialized.type).toBe(MESSAGE_TYPE.TEXT);
    expect(deserialized.seq).toBe(originalMessage.seq);
    expect(deserialized.text).toBe(originalMessage.text);
    expect(deserialized.hasGps).toBe(true);
    expect(deserialized.latitude).toBeCloseTo(originalMessage.latitude ?? 0, 6);
    expect(deserialized.longitude).toBeCloseTo(originalMessage.longitude ?? 0, 6);
    expect(deserialized.senderTime).toBe(originalMessage.senderTime);
  });

  it('should serialize and deserialize an AckMessage', () => {
    const originalMessage: AckMessage = {
      type: MESSAGE_TYPE.ACK,
      seq: 123
    };

    const serialized = serialize(originalMessage);
    const deserialized = deserialize(serialized) as AckMessage;

    expect(deserialized.type).toBe(MESSAGE_TYPE.ACK);
    expect(deserialized.seq).toBe(originalMessage.seq);
    expect(serialized.length).toBe(2);
  });

  it('should throw an error for invalid message type during serialization', () => {
    const invalidMessage = { type: 99 } as unknown as TextMessage;
    expect(() => serialize(invalidMessage)).toThrow('Unknown message type: 99');
  });

  it('should throw an error for invalid message type during deserialization', () => {
    const invalidData = new Uint8Array([99, 0]);
    expect(() => deserialize(invalidData)).toThrow('Unknown message type: 0x63');
  });

  it('should handle max length text message', () => {
    const text = 'A'.repeat(50);
    const originalMessage: TextMessage = {
      type: MESSAGE_TYPE.TEXT,
      seq: 1,
      text: text,
      hasGps: false
    };
    const serialized = serialize(originalMessage);
    const deserialized = deserialize(serialized) as TextMessage;
    expect(deserialized.text).toBe(text);
  });

  it('should throw an error if text exceeds max length', () => {
    const text = 'A'.repeat(51);
    const originalMessage: TextMessage = {
      type: MESSAGE_TYPE.TEXT,
      seq: 1,
      text: text,
      hasGps: false
    };
    expect(() => serialize(originalMessage)).toThrow('Text length 51 exceeds maximum 50');
  });
});
