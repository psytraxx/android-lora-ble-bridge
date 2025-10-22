package lora;

import androidx.annotation.NonNull;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * LoRa Message Protocol for Android
 * Binary format for efficient BLE and LoRa communication
 * Uses 6-bit character packing for bandwidth optimization
 */
public class Protocol {

    /**
     * Maximum text length in characters for optimal long-range LoRa transmission.
     * With 6-bit packing: 50 chars = 38 bytes (was 50 bytes)
     * With SF10, BW125, 433MHz: 50 bytes (12 header + 38 text) = ~600ms Time on Air
     * This allows ~60 messages per hour within 1% duty cycle limits.
     */
    public static final int MAX_TEXT_LENGTH = 50;

    /**
     * Character set for 6-bit encoding (64 characters)
     * UPPERCASE ONLY: Space + A-Z + 0-9 + punctuation
     */
    private static final String CHARSET = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.,!?-:;'\"@#$%&*()[]{}=+/<>_";

    /**
     * Convert a character to its 6-bit encoded value
     * Automatically converts lowercase to uppercase
     */
    private static byte charTo6Bit(char ch) throws IllegalArgumentException {
        char upperCh = Character.toUpperCase(ch);
        int index = CHARSET.indexOf(upperCh);
        if (index < 0) {
            throw new IllegalArgumentException("Character not supported: '" + ch + "'");
        }
        return (byte) index;
    }

    /**
     * Convert a 6-bit value back to a character
     */
    private static char sixBitToChar(byte val) throws IllegalArgumentException {
        if (val < 0 || val >= CHARSET.length()) {
            throw new IllegalArgumentException("Invalid 6-bit value: " + val);
        }
        return CHARSET.charAt(val);
    }

    /**
     * Pack text into 6-bit encoded bytes
     * Each character is encoded as 6 bits instead of 8 bits (UTF-8)
     * Lowercase letters are automatically converted to uppercase
     * 50 chars × 6 bits = 300 bits = 37.5 bytes → 38 bytes
     */
    private static byte[] packText(String text) throws IllegalArgumentException {
        int charCount = text.length();
        int byteCount = (charCount * 6 + 7) / 8; // Round up
        byte[] result = new byte[byteCount];

        int bitOffset = 0;

        for (int i = 0; i < charCount; i++) {
            byte value = charTo6Bit(text.charAt(i));

            int byteIdx = bitOffset / 8;
            int bitInByte = bitOffset % 8;

            if (bitInByte <= 2) {
                // The 6 bits fit within the current byte
                result[byteIdx] |= (byte) (value << (2 - bitInByte));
            } else {
                // The 6 bits span two bytes
                int bitsInFirst = 8 - bitInByte;
                int bitsInSecond = 6 - bitsInFirst;

                result[byteIdx] |= (byte) (value >> bitsInSecond);
                if (byteIdx + 1 < result.length) {
                    result[byteIdx + 1] |= (byte) (value << (8 - bitsInSecond));
                }
            }

            bitOffset += 6;
        }

        return result;
    }

    /**
     * Unpack 6-bit encoded bytes back to text
     * Reads 6 bits at a time and converts to characters (uppercase)
     */
    private static String unpackText(byte[] packed, int charCount) throws IllegalArgumentException {
        StringBuilder result = new StringBuilder(charCount);
        int bitOffset = 0;

        for (int i = 0; i < charCount; i++) {
            int byteIdx = bitOffset / 8;
            int bitInByte = bitOffset % 8;

            if (byteIdx >= packed.length) {
                throw new IllegalArgumentException("Insufficient packed data");
            }

            byte value;
            if (bitInByte <= 2) {
                // The 6 bits are within the current byte
                value = (byte) (((packed[byteIdx] & 0xFF) >>> (2 - bitInByte)) & 0x3F);
            } else {
                // The 6 bits span two bytes
                int bitsInFirst = 8 - bitInByte;
                int bitsInSecond = 6 - bitsInFirst;

                byte firstPart = (byte) (packed[byteIdx] & ((1 << bitsInFirst) - 1));
                byte secondPart;
                if (byteIdx + 1 < packed.length) {
                    secondPart = (byte) ((packed[byteIdx + 1] & 0xFF) >>> (8 - bitsInSecond));
                } else {
                    throw new IllegalArgumentException("Insufficient packed data");
                }

                value = (byte) ((firstPart << bitsInSecond) | secondPart);
            }

            result.append(sixBitToChar(value));
            bitOffset += 6;
        }

        return result.toString();
    }

    /**
     * Calculate the packed size for a given text
     */
    public static int calculatePackedSize(String text) {
        return (text.length() * 6 + 7) / 8;
    }

    /**
     * Validate if a character is supported
     */
    public static boolean isCharacterSupported(char ch) {
        char upperCh = Character.toUpperCase(ch);
        return CHARSET.indexOf(upperCh) >= 0;
    }

    /**
     * Validate if all characters in text are supported
     */
    public static boolean isTextSupported(String text) {
        for (int i = 0; i < text.length(); i++) {
            if (!isCharacterSupported(text.charAt(i))) {
                return false;
            }
        }
        return true;
    }

    public enum MessageType {
        TEXT((byte) 0x01),
        ACK((byte) 0x02);

        private final byte value;

        MessageType(byte value) {
            this.value = value;
        }

        public static MessageType fromByte(byte value) {
            for (MessageType type : values()) {
                if (type.value == value) {
                    return type;
                }
            }
            throw new IllegalArgumentException("Unknown message type: " + value);
        }

        public byte getValue() {
            return value;
        }
    }

    public static class TextMessage extends Message {
        public final byte seq;
        public final String text;
        public final boolean hasGps;
        public final int lat; // latitude * 1_000_000 (only valid if hasGps=true)
        public final int lon; // longitude * 1_000_000 (only valid if hasGps=true)

        public TextMessage(byte seq, String text) {
            super(MessageType.TEXT);
            if (text.length() > MAX_TEXT_LENGTH) {
                throw new IllegalArgumentException("Text too long (max " + MAX_TEXT_LENGTH + " chars)");
            }
            this.seq = seq;
            this.text = text;
            this.hasGps = false;
            this.lat = 0;
            this.lon = 0;
        }

        public TextMessage(byte seq, String text, int lat, int lon) {
            super(MessageType.TEXT);
            if (text.length() > MAX_TEXT_LENGTH) {
                throw new IllegalArgumentException("Text too long (max " + MAX_TEXT_LENGTH + " chars)");
            }
            this.seq = seq;
            this.text = text;
            this.hasGps = true;
            this.lat = lat;
            this.lon = lon;
        }

        @Override
        public byte[] serialize() {
            byte[] packedText = packText(text);
            int totalSize = 1 + 1 + 1 + 1 + 1 + packedText.length; // type + seq + charCount + packedLen + hasGps + packed
            if (hasGps) {
                totalSize += 8; // lat + lon
            }
            byte[] data = new byte[totalSize];
            data[0] = MessageType.TEXT.getValue();
            data[1] = seq;
            data[2] = (byte) text.length(); // Original character count
            data[3] = (byte) packedText.length; // Packed byte count
            System.arraycopy(packedText, 0, data, 4, packedText.length);
            data[4 + packedText.length] = (byte) (hasGps ? 1 : 0);
            if (hasGps) {
                ByteBuffer buf = ByteBuffer.wrap(data, 5 + packedText.length, 8).order(ByteOrder.LITTLE_ENDIAN);
                buf.putInt(lat);
                buf.putInt(lon);
            }
            return data;
        }

        @Override
        public boolean equals(Object obj) {
            if (this == obj)
                return true;
            if (obj == null || getClass() != obj.getClass())
                return false;
            TextMessage that = (TextMessage) obj;
            return seq == that.seq && text.equals(that.text) && hasGps == that.hasGps && lat == that.lat
                    && lon == that.lon;
        }

        @Override
        public int hashCode() {
            return java.util.Objects.hash(seq, text, hasGps, lat, lon);
        }

        @NonNull
        @Override
        public String toString() {
            if (hasGps) {
                return "TextMessage{seq=" + seq + ", text='" + text + "', lat=" + lat + ", lon=" + lon + "}";
            }
            return "TextMessage{seq=" + seq + ", text='" + text + "'}";
        }
    }

    public static class AckMessage extends Message {
        public final byte seq;

        public AckMessage(byte seq) {
            super(MessageType.ACK);
            this.seq = seq;
        }

        @Override
        public byte[] serialize() {
            byte[] data = new byte[2];
            data[0] = MessageType.ACK.getValue();
            data[1] = seq;
            return data;
        }

        @Override
        public boolean equals(Object obj) {
            if (this == obj)
                return true;
            if (obj == null || getClass() != obj.getClass())
                return false;
            AckMessage that = (AckMessage) obj;
            return seq == that.seq;
        }

        @Override
        public int hashCode() {
            return Byte.hashCode(seq);
        }

        @NonNull
        @Override
        public String toString() {
            return "AckMessage{seq=" + seq + "}";
        }
    }

    public static abstract class Message {
        public final MessageType type;

        protected Message(MessageType type) {
            this.type = type;
        }

        public static Message deserialize(byte[] data) throws IllegalArgumentException {
            if (data.length < 1) {
                throw new IllegalArgumentException("Data too short");
            }
            MessageType type = MessageType.fromByte(data[0]);
            return switch (type) {
                case TEXT -> deserializeText(data);
                case ACK -> deserializeAck(data);
            };
        }

        private static TextMessage deserializeText(byte[] data) {
            if (data.length < 5) {
                throw new IllegalArgumentException("Data too short for TextMessage header");
            }
            byte seq = data[1];
            int charCount = data[2] & 0xFF; // Original character count
            int packedLen = data[3] & 0xFF; // Packed byte count
            if (data.length < 5 + packedLen) {
                throw new IllegalArgumentException("Data too short for packed text + hasGps flag");
            }
            byte[] packedBytes = new byte[packedLen];
            System.arraycopy(data, 4, packedBytes, 0, packedLen);
            String text = unpackText(packedBytes, charCount);
            boolean hasGps = data[4 + packedLen] != 0;

            if (hasGps) {
                if (data.length < 5 + packedLen + 8) {
                    throw new IllegalArgumentException("Data too short for GPS data");
                }
                ByteBuffer buf = ByteBuffer.wrap(data, 5 + packedLen, 8).order(ByteOrder.LITTLE_ENDIAN);
                int lat = buf.getInt();
                int lon = buf.getInt();
                return new TextMessage(seq, text, lat, lon);
            } else {
                return new TextMessage(seq, text);
            }
        }

        private static AckMessage deserializeAck(byte[] data) {
            if (data.length < 2) {
                throw new IllegalArgumentException("Data too short for AckMessage");
            }
            byte seq = data[1];
            return new AckMessage(seq);
        }

        public abstract byte[] serialize();
    }
}