package lora;

import org.junit.Test;
import org.junit.Before;
import static org.junit.Assert.*;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/**
 * Unit tests for the LoRa Protocol implementation
 * Tests serialization and deserialization of Data and ACK messages
 */
public class ProtocolTest {

    private static final double EPSILON = 0.000001; // For GPS coordinate comparison

    @Before
    public void setUp() {
        // Setup any common test data if needed
    }

    // ============================================
    // ACK Message Tests
    // ============================================

    @Test
    public void testAckMessageSerialization_Zero() {
        Protocol.AckMessage ack = new Protocol.AckMessage((byte) 0);
        byte[] data = ack.serialize();

        assertEquals(2, data.length);
        assertEquals(0x02, data[0]); // ACK type
        assertEquals(0, data[1]); // seq number
    }

    @Test
    public void testAckMessageSerialization_MaxSeq() {
        Protocol.AckMessage ack = new Protocol.AckMessage((byte) 255);
        byte[] data = ack.serialize();

        assertEquals(2, data.length);
        assertEquals(0x02, data[0]);
        assertEquals((byte) 255, data[1]);
    }

    @Test
    public void testAckMessageDeserialization() {
        byte[] data = new byte[] { 0x02, 42 };
        Protocol.Message msg = Protocol.Message.deserialize(data);

        assertTrue(msg instanceof Protocol.AckMessage);
        Protocol.AckMessage ack = (Protocol.AckMessage) msg;
        assertEquals(42, ack.seq);
    }

    @Test
    public void testAckMessageRoundTrip() {
        Protocol.AckMessage original = new Protocol.AckMessage((byte) 123);
        byte[] serialized = original.serialize();
        Protocol.Message deserialized = Protocol.Message.deserialize(serialized);

        assertTrue(deserialized instanceof Protocol.AckMessage);
        Protocol.AckMessage result = (Protocol.AckMessage) deserialized;
        assertEquals(original.seq, result.seq);
        assertEquals(original, result);
    }

    // ============================================
    // Data Message Tests - Basic
    // ============================================

    @Test
    public void testDataMessageSerialization_EmptyText() {
        Protocol.DataMessage msg = new Protocol.DataMessage(
                (byte) 1,
                "",
                37774200, // 37.7742 degrees lat
                -122419200 // -122.4192 degrees lon
        );
        byte[] data = msg.serialize();

        assertEquals(11, data.length); // 1 + 1 + 1 + 0 + 4 + 4
        assertEquals(0x01, data[0]); // DATA type
        assertEquals(1, data[1]); // seq
        assertEquals(0, data[2]); // text length

        // Verify GPS coordinates (little-endian)
        ByteBuffer buf = ByteBuffer.wrap(data, 3, 8).order(ByteOrder.LITTLE_ENDIAN);
        assertEquals(37774200, buf.getInt());
        assertEquals(-122419200, buf.getInt());
    }

    @Test
    public void testDataMessageSerialization_ShortText() {
        Protocol.DataMessage msg = new Protocol.DataMessage(
                (byte) 5,
                "Hello",
                40712800, // NYC
                -74006000);
        byte[] data = msg.serialize();

        assertEquals(16, data.length); // 1 + 1 + 1 + 5 + 4 + 4
        assertEquals(0x01, data[0]);
        assertEquals(5, data[1]);
        assertEquals(5, data[2]);

        // Verify text
        String text = new String(data, 3, 5, StandardCharsets.UTF_8);
        assertEquals("Hello", text);
    }

    @Test
    public void testDataMessageSerialization_LongText() {
        // Create a 100-character message
        String longText = "0123456789".repeat(10); // 100 chars
        Protocol.DataMessage msg = new Protocol.DataMessage(
                (byte) 10,
                longText,
                51509800, // London
                -141800);
        byte[] data = msg.serialize();

        assertEquals(111, data.length); // 1 + 1 + 1 + 100 + 4 + 4
        assertEquals(0x01, data[0]);
        assertEquals(10, data[1]);
        assertEquals(100, data[2]);

        String text = new String(data, 3, 100, StandardCharsets.UTF_8);
        assertEquals(longText, text);
    }

    @Test
    public void testDataMessageSerialization_MaxText() {
        // Create a 255-character message (maximum allowed)
        StringBuilder sb = new StringBuilder(255);
        for (int i = 0; i < 255; i++) {
            sb.append((char) ('A' + (i % 26)));
        }
        String maxText = sb.toString();

        Protocol.DataMessage msg = new Protocol.DataMessage(
                (byte) 255,
                maxText,
                35689500, // Tokyo
                139691700);
        byte[] data = msg.serialize();

        assertEquals(266, data.length); // 1 + 1 + 1 + 255 + 4 + 4
        assertEquals(0x01, data[0]);
        assertEquals((byte) 255, data[1]);
        assertEquals((byte) 255, data[2]);

        String text = new String(data, 3, 255, StandardCharsets.UTF_8);
        assertEquals(maxText, text);
    }

    @Test(expected = IllegalArgumentException.class)
    public void testDataMessageSerialization_TextTooLong() {
        // Create a 256-character message (should fail)
        String tooLongText = "X".repeat(256);
        new Protocol.DataMessage((byte) 1, tooLongText, 0, 0);
    }

    // ============================================
    // Data Message Tests - GPS Coordinates
    // ============================================

    @Test
    public void testDataMessage_PositiveCoordinates() {
        // Sydney, Australia: -33.8688° S, 151.2093° E
        int lat = -33868800;
        int lon = 151209300;

        Protocol.DataMessage msg = new Protocol.DataMessage((byte) 1, "Sydney", lat, lon);
        byte[] data = msg.serialize();

        Protocol.Message deserialized = Protocol.Message.deserialize(data);
        assertTrue(deserialized instanceof Protocol.DataMessage);
        Protocol.DataMessage result = (Protocol.DataMessage) deserialized;

        assertEquals(lat, result.lat);
        assertEquals(lon, result.lon);

        // Verify conversion back to degrees
        double latDegrees = result.lat / 1_000_000.0;
        double lonDegrees = result.lon / 1_000_000.0;
        assertEquals(-33.8688, latDegrees, EPSILON);
        assertEquals(151.2093, lonDegrees, EPSILON);
    }

    @Test
    public void testDataMessage_NegativeCoordinates() {
        // Buenos Aires, Argentina: -34.6037° S, -58.3816° W
        int lat = -34603700;
        int lon = -58381600;

        Protocol.DataMessage msg = new Protocol.DataMessage((byte) 2, "BuenosAires", lat, lon);
        byte[] data = msg.serialize();

        Protocol.Message deserialized = Protocol.Message.deserialize(data);
        Protocol.DataMessage result = (Protocol.DataMessage) deserialized;

        assertEquals(lat, result.lat);
        assertEquals(lon, result.lon);
    }

    @Test
    public void testDataMessage_ZeroCoordinates() {
        // Null Island (0°, 0°)
        Protocol.DataMessage msg = new Protocol.DataMessage((byte) 3, "NullIsland", 0, 0);
        byte[] data = msg.serialize();

        Protocol.Message deserialized = Protocol.Message.deserialize(data);
        Protocol.DataMessage result = (Protocol.DataMessage) deserialized;

        assertEquals(0, result.lat);
        assertEquals(0, result.lon);
    }

    @Test
    public void testDataMessage_ExtremeCoordinates() {
        // North Pole: 90° N, and International Date Line: 180° E
        int lat = 90_000_000;
        int lon = 180_000_000;

        Protocol.DataMessage msg = new Protocol.DataMessage((byte) 4, "Extreme", lat, lon);
        byte[] data = msg.serialize();

        Protocol.Message deserialized = Protocol.Message.deserialize(data);
        Protocol.DataMessage result = (Protocol.DataMessage) deserialized;

        assertEquals(lat, result.lat);
        assertEquals(lon, result.lon);
    }

    // ============================================
    // Data Message Tests - UTF-8 Text
    // ============================================

    @Test
    public void testDataMessage_UnicodeText() {
        String unicodeText = "Hello 世界 🌍";
        Protocol.DataMessage msg = new Protocol.DataMessage((byte) 7, unicodeText, 0, 0);
        byte[] data = msg.serialize();

        Protocol.Message deserialized = Protocol.Message.deserialize(data);
        Protocol.DataMessage result = (Protocol.DataMessage) deserialized;

        assertEquals(unicodeText, result.text);
    }

    @Test
    public void testDataMessage_SpecialCharacters() {
        String specialText = "Test!@#$%^&*(){}[]|\\:;\"'<>,.?/~`";
        Protocol.DataMessage msg = new Protocol.DataMessage((byte) 8, specialText, 0, 0);
        byte[] data = msg.serialize();

        Protocol.Message deserialized = Protocol.Message.deserialize(data);
        Protocol.DataMessage result = (Protocol.DataMessage) deserialized;

        assertEquals(specialText, result.text);
    }

    @Test
    public void testDataMessage_NewlineAndTabs() {
        String text = "Line1\nLine2\tTabbed";
        Protocol.DataMessage msg = new Protocol.DataMessage((byte) 9, text, 0, 0);
        byte[] data = msg.serialize();

        Protocol.Message deserialized = Protocol.Message.deserialize(data);
        Protocol.DataMessage result = (Protocol.DataMessage) deserialized;

        assertEquals(text, result.text);
    }

    // ============================================
    // Data Message Tests - Deserialization
    // ============================================

    @Test
    public void testDataMessageDeserialization_Valid() {
        byte[] data = new byte[16]; // 1 + 1 + 1 + 5 + 4 + 4
        data[0] = 0x01; // DATA type
        data[1] = 42; // seq
        data[2] = 5; // text length
        System.arraycopy("Hello".getBytes(StandardCharsets.UTF_8), 0, data, 3, 5);

        ByteBuffer buf = ByteBuffer.wrap(data, 8, 8).order(ByteOrder.LITTLE_ENDIAN);
        buf.putInt(12345678);
        buf.putInt(-87654321);

        Protocol.Message msg = Protocol.Message.deserialize(data);
        assertTrue(msg instanceof Protocol.DataMessage);

        Protocol.DataMessage dataMsg = (Protocol.DataMessage) msg;
        assertEquals(42, dataMsg.seq);
        assertEquals("Hello", dataMsg.text);
        assertEquals(12345678, dataMsg.lat);
        assertEquals(-87654321, dataMsg.lon);
    }

    @Test(expected = IllegalArgumentException.class)
    public void testDataMessageDeserialization_TooShort() {
        byte[] data = new byte[] { 0x01, 1, 5 }; // Missing text and GPS
        Protocol.Message.deserialize(data);
    }

    @Test(expected = IllegalArgumentException.class)
    public void testDataMessageDeserialization_TextTooShort() {
        byte[] data = new byte[13]; // Says 5 bytes text but only has 2
        data[0] = 0x01;
        data[1] = 1;
        data[2] = 5; // Claims 5 bytes of text
        data[3] = 'H';
        data[4] = 'i';
        // Missing 3 bytes of text and GPS coordinates
        Protocol.Message.deserialize(data);
    }

    // ============================================
    // Round-trip Tests
    // ============================================

    @Test
    public void testDataMessageRoundTrip_Simple() {
        Protocol.DataMessage original = new Protocol.DataMessage(
                (byte) 50,
                "Test message",
                48856600, // Paris
                2341200);

        byte[] serialized = original.serialize();
        Protocol.Message deserialized = Protocol.Message.deserialize(serialized);

        assertTrue(deserialized instanceof Protocol.DataMessage);
        Protocol.DataMessage result = (Protocol.DataMessage) deserialized;

        assertEquals(original.seq, result.seq);
        assertEquals(original.text, result.text);
        assertEquals(original.lat, result.lat);
        assertEquals(original.lon, result.lon);
        assertEquals(original, result);
    }

    @Test
    public void testDataMessageRoundTrip_MultipleMessages() {
        Protocol.DataMessage[] messages = new Protocol.DataMessage[] {
                new Protocol.DataMessage((byte) 0, "", 0, 0),
                new Protocol.DataMessage((byte) 1, "A", 1000000, -1000000),
                new Protocol.DataMessage((byte) 127, "Medium length message here", 45000000, 90000000),
                new Protocol.DataMessage((byte) 255, "X".repeat(255), -90000000, -180000000)
        };

        for (Protocol.DataMessage original : messages) {
            byte[] serialized = original.serialize();
            Protocol.Message deserialized = Protocol.Message.deserialize(serialized);

            assertTrue("Failed for message with seq=" + original.seq,
                    deserialized instanceof Protocol.DataMessage);
            Protocol.DataMessage result = (Protocol.DataMessage) deserialized;
            assertEquals(original, result);
        }
    }

    @Test
    public void testAckMessageRoundTrip_AllSeqNumbers() {
        // Test all possible sequence numbers (0-255)
        for (int seq = 0; seq <= 255; seq++) {
            Protocol.AckMessage original = new Protocol.AckMessage((byte) seq);
            byte[] serialized = original.serialize();
            Protocol.Message deserialized = Protocol.Message.deserialize(serialized);

            assertTrue(deserialized instanceof Protocol.AckMessage);
            Protocol.AckMessage result = (Protocol.AckMessage) deserialized;
            assertEquals(original.seq, result.seq);
        }
    }

    // ============================================
    // Error Handling Tests
    // ============================================

    @Test(expected = IllegalArgumentException.class)
    public void testDeserialize_EmptyBuffer() {
        Protocol.Message.deserialize(new byte[0]);
    }

    @Test(expected = IllegalArgumentException.class)
    public void testDeserialize_UnknownMessageType() {
        byte[] data = new byte[] { (byte) 0x99, 1, 2, 3 }; // Invalid type
        Protocol.Message.deserialize(data);
    }

    @Test(expected = IllegalArgumentException.class)
    public void testDeserialize_AckTooShort() {
        byte[] data = new byte[] { 0x02 }; // ACK with no seq
        Protocol.Message.deserialize(data);
    }

    // ============================================
    // MessageType Enum Tests
    // ============================================

    @Test
    public void testMessageType_FromByte() {
        assertEquals(Protocol.MessageType.DATA, Protocol.MessageType.fromByte((byte) 0x01));
        assertEquals(Protocol.MessageType.ACK, Protocol.MessageType.fromByte((byte) 0x02));
    }

    @Test(expected = IllegalArgumentException.class)
    public void testMessageType_FromByte_Invalid() {
        Protocol.MessageType.fromByte((byte) 0x99);
    }

    @Test
    public void testMessageType_GetValue() {
        assertEquals((byte) 0x01, Protocol.MessageType.DATA.getValue());
        assertEquals((byte) 0x02, Protocol.MessageType.ACK.getValue());
    }

    // ============================================
    // Equals and HashCode Tests
    // ============================================

    @Test
    public void testDataMessage_Equals() {
        Protocol.DataMessage msg1 = new Protocol.DataMessage((byte) 1, "Test", 100, 200);
        Protocol.DataMessage msg2 = new Protocol.DataMessage((byte) 1, "Test", 100, 200);
        Protocol.DataMessage msg3 = new Protocol.DataMessage((byte) 2, "Test", 100, 200);

        assertEquals(msg1, msg2);
        assertNotEquals(msg1, msg3);
        assertEquals(msg1, msg1);
        assertNotEquals(msg1, null);
        assertNotEquals(msg1, "Not a message");
    }

    @Test
    public void testDataMessage_HashCode() {
        Protocol.DataMessage msg1 = new Protocol.DataMessage((byte) 1, "Test", 100, 200);
        Protocol.DataMessage msg2 = new Protocol.DataMessage((byte) 1, "Test", 100, 200);

        assertEquals(msg1.hashCode(), msg2.hashCode());
    }

    @Test
    public void testAckMessage_Equals() {
        Protocol.AckMessage ack1 = new Protocol.AckMessage((byte) 42);
        Protocol.AckMessage ack2 = new Protocol.AckMessage((byte) 42);
        Protocol.AckMessage ack3 = new Protocol.AckMessage((byte) 43);

        assertEquals(ack1, ack2);
        assertNotEquals(ack1, ack3);
        assertEquals(ack1, ack1);
        assertNotEquals(ack1, null);
        assertNotEquals(ack1, "Not an ack");
    }

    @Test
    public void testAckMessage_HashCode() {
        Protocol.AckMessage ack1 = new Protocol.AckMessage((byte) 42);
        Protocol.AckMessage ack2 = new Protocol.AckMessage((byte) 42);

        assertEquals(ack1.hashCode(), ack2.hashCode());
    }

    // ============================================
    // ToString Tests
    // ============================================

    @Test
    public void testDataMessage_ToString() {
        Protocol.DataMessage msg = new Protocol.DataMessage((byte) 5, "Hello", 1000, 2000);
        String str = msg.toString();

        assertTrue(str.contains("DataMessage"));
        assertTrue(str.contains("seq=5"));
        assertTrue(str.contains("Hello"));
        assertTrue(str.contains("1000"));
        assertTrue(str.contains("2000"));
    }

    @Test
    public void testAckMessage_ToString() {
        Protocol.AckMessage ack = new Protocol.AckMessage((byte) 42);
        String str = ack.toString();

        assertTrue(str.contains("AckMessage"));
        assertTrue(str.contains("seq=42"));
    }

    // ============================================
    // Real-world Scenario Tests
    // ============================================

    @Test
    public void testRealWorldScenario_EmergencyMessage() {
        // Simulate emergency SOS message with GPS
        Protocol.DataMessage sos = new Protocol.DataMessage(
                (byte) 1,
                "SOS - Need help at this location!",
                37774200, // San Francisco
                -122419200);

        byte[] transmitted = sos.serialize();
        assertTrue("Message should fit in 256 bytes", transmitted.length <= 256);

        Protocol.DataMessage received = (Protocol.DataMessage) Protocol.Message.deserialize(transmitted);
        assertEquals("SOS - Need help at this location!", received.text);

        // Verify GPS coordinates can be converted back
        double lat = received.lat / 1_000_000.0;
        double lon = received.lon / 1_000_000.0;
        assertEquals(37.7742, lat, EPSILON);
        assertEquals(-122.4192, lon, EPSILON);
    }

    @Test
    public void testRealWorldScenario_MessageSequenceWithAcks() {
        // Simulate a conversation with acknowledgments
        byte seq = 0;

        // Send message 1
        Protocol.DataMessage msg1 = new Protocol.DataMessage(seq++, "Hello from device A", 1000, 2000);
        byte[] data1 = msg1.serialize();
        Protocol.DataMessage receivedMsg1 = (Protocol.DataMessage) Protocol.Message.deserialize(data1);
        assertEquals("Hello from device A", receivedMsg1.text);

        // Send ACK for message 1
        Protocol.AckMessage ack1 = new Protocol.AckMessage(receivedMsg1.seq);
        byte[] ackData1 = ack1.serialize();
        Protocol.AckMessage receivedAck1 = (Protocol.AckMessage) Protocol.Message.deserialize(ackData1);
        assertEquals(receivedMsg1.seq, receivedAck1.seq);

        // Send message 2
        Protocol.DataMessage msg2 = new Protocol.DataMessage(seq++, "Hello from device B", 3000, 4000);
        byte[] data2 = msg2.serialize();
        Protocol.DataMessage receivedMsg2 = (Protocol.DataMessage) Protocol.Message.deserialize(data2);

        // Send ACK for message 2
        Protocol.AckMessage ack2 = new Protocol.AckMessage(receivedMsg2.seq);
        byte[] ackData2 = ack2.serialize();
        Protocol.AckMessage receivedAck2 = (Protocol.AckMessage) Protocol.Message.deserialize(ackData2);
        assertEquals(receivedMsg2.seq, receivedAck2.seq);
    }

    @Test
    public void testRealWorldScenario_SequenceNumberWrapAround() {
        // Test that sequence numbers wrap around correctly at 255
        Protocol.DataMessage msg254 = new Protocol.DataMessage((byte) 254, "Message 254", 0, 0);
        Protocol.DataMessage msg255 = new Protocol.DataMessage((byte) 255, "Message 255", 0, 0);
        Protocol.DataMessage msg0 = new Protocol.DataMessage((byte) 0, "Message 0 (wrapped)", 0, 0);

        // All should serialize and deserialize correctly
        assertEquals(msg254, Protocol.Message.deserialize(msg254.serialize()));
        assertEquals(msg255, Protocol.Message.deserialize(msg255.serialize()));
        assertEquals(msg0, Protocol.Message.deserialize(msg0.serialize()));
    }
}
