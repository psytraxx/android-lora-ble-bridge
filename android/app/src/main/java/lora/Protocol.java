package lora;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

/**
 * LoRa Message Protocol for Android
 * Binary format for efficient BLE and LoRa communication
 */
public class Protocol {

    public enum MessageType {
        DATA((byte) 0x01),
        ACK((byte) 0x02);

        private final byte value;

        MessageType(byte value) {
            this.value = value;
        }

        public byte getValue() {
            return value;
        }

        public static MessageType fromByte(byte value) {
            for (MessageType type : values()) {
                if (type.value == value) {
                    return type;
                }
            }
            throw new IllegalArgumentException("Unknown message type: " + value);
        }
    }

    public static class DataMessage extends Message {
        public final byte seq;
        public final String text;
        public final int lat; // latitude * 1_000_000
        public final int lon; // longitude * 1_000_000

        public DataMessage(byte seq, String text, int lat, int lon) {
            super(MessageType.DATA);
            if (text.length() > 255) {
                throw new IllegalArgumentException("Text too long");
            }
            this.seq = seq;
            this.text = text;
            this.lat = lat;
            this.lon = lon;
        }

        @Override
        public byte[] serialize() {
            byte textBytes[] = text.getBytes(StandardCharsets.UTF_8);
            byte data[] = new byte[1 + 1 + 1 + textBytes.length + 8];
            data[0] = MessageType.DATA.getValue();
            data[1] = seq;
            data[2] = (byte) textBytes.length;
            System.arraycopy(textBytes, 0, data, 3, textBytes.length);
            ByteBuffer buf = ByteBuffer.wrap(data, 3 + textBytes.length, 8).order(ByteOrder.LITTLE_ENDIAN);
            buf.putInt(lat);
            buf.putInt(lon);
            return data;
        }

        @Override
        public boolean equals(Object obj) {
            if (this == obj)
                return true;
            if (obj == null || getClass() != obj.getClass())
                return false;
            DataMessage that = (DataMessage) obj;
            return seq == that.seq && lat == that.lat && lon == that.lon && text.equals(that.text);
        }

        @Override
        public int hashCode() {
            return java.util.Objects.hash(seq, text, lat, lon);
        }

        @Override
        public String toString() {
            return "DataMessage{seq=" + seq + ", text='" + text + "', lat=" + lat + ", lon=" + lon + "}";
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
            byte data[] = new byte[2];
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

        public abstract byte[] serialize();

        public static Message deserialize(byte[] data) throws IllegalArgumentException {
            if (data.length < 1) {
                throw new IllegalArgumentException("Data too short");
            }
            MessageType type = MessageType.fromByte(data[0]);
            switch (type) {
                case DATA:
                    return deserializeData(data);
                case ACK:
                    return deserializeAck(data);
                default:
                    throw new IllegalArgumentException("Unknown message type");
            }
        }

        private static DataMessage deserializeData(byte[] data) {
            if (data.length < 11) {
                throw new IllegalArgumentException("Data too short for DataMessage");
            }
            byte seq = data[1];
            byte textLen = data[2];
            if (data.length < 11 + textLen) {
                throw new IllegalArgumentException("Data too short for text");
            }
            String text = new String(data, 3, textLen, StandardCharsets.UTF_8);
            ByteBuffer buf = ByteBuffer.wrap(data, 3 + textLen, 8).order(ByteOrder.LITTLE_ENDIAN);
            int lat = buf.getInt();
            int lon = buf.getInt();
            return new DataMessage(seq, text, lat, lon);
        }

        private static AckMessage deserializeAck(byte[] data) {
            if (data.length < 2) {
                throw new IllegalArgumentException("Data too short for AckMessage");
            }
            byte seq = data[1];
            return new AckMessage(seq);
        }
    }
}