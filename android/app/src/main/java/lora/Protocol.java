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

    public static class DataMessage {
        public final byte seq;
        public final String text;
        public final int lat; // latitude * 1_000_000
        public final int lon; // longitude * 1_000_000

        public DataMessage(byte seq, String text, int lat, int lon) {
            if (text.length() > 255) {
                throw new IllegalArgumentException("Text too long");
            }
            this.seq = seq;
            this.text = text;
            this.lat = lat;
            this.lon = lon;
        }

        @Override
        public boolean equals(Object obj) {
            if (this == obj) return true;
            if (obj == null || getClass() != obj.getClass()) return false;
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

    public static class AckMessage {
        public final byte seq;

        public AckMessage(byte seq) {
            this.seq = seq;
        }

        @Override
        public boolean equals(Object obj) {
            if (this == obj) return true;
            if (obj == null || getClass() != obj.getClass()) return false;
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

        public static class Data extends Message {
            public final DataMessage dataMessage;

            public Data(DataMessage dataMessage) {
                super(MessageType.DATA);
                this.dataMessage = dataMessage;
            }

            @Override
            public byte[] serialize() {
                byte[] textBytes = dataMessage.text.getBytes(StandardCharsets.UTF_8);
                ByteBuffer buf = ByteBuffer.allocate(11 + textBytes.length).order(ByteOrder.LITTLE_ENDIAN);
                buf.put(type.getValue());
                buf.put(dataMessage.seq);
                buf.put((byte) textBytes.length);
                buf.put(textBytes);
                buf.putInt(dataMessage.lat);
                buf.putInt(dataMessage.lon);
                return buf.array();
            }

            @Override
            public boolean equals(Object obj) {
                if (this == obj) return true;
                if (obj == null || getClass() != obj.getClass()) return false;
                Data that = (Data) obj;
                return dataMessage.equals(that.dataMessage);
            }

            @Override
            public int hashCode() {
                return dataMessage.hashCode();
            }

            @Override
            public String toString() {
                return "Message.Data{" + dataMessage + "}";
            }
        }

        public static class Ack extends Message {
            public final AckMessage ackMessage;

            public Ack(AckMessage ackMessage) {
                super(MessageType.ACK);
                this.ackMessage = ackMessage;
            }

            @Override
            public byte[] serialize() {
                return new byte[]{type.getValue(), ackMessage.seq};
            }

            @Override
            public boolean equals(Object obj) {
                if (this == obj) return true;
                if (obj == null || getClass() != obj.getClass()) return false;
                Ack that = (Ack) obj;
                return ackMessage.equals(that.ackMessage);
            }

            @Override
            public int hashCode() {
                return ackMessage.hashCode();
            }

            @Override
            public String toString() {
                return "Message.Ack{" + ackMessage + "}";
            }
        }
    }
}