package lora;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/**
 * Unit tests for the LoRa Protocol
 * Tests 6-bit packed text encoding and separate Text/GPS message types
 */
public class ProtocolTest {

    @Test
    public void testTextMessageSerialization_Empty() {
        Protocol.TextMessage msg = new Protocol.TextMessage((byte) 0, "");
        byte[] data = msg.serialize();
        assertEquals(4, data.length);

        Protocol.Message deserialized = Protocol.Message.deserialize(data);
        assertTrue(deserialized instanceof Protocol.TextMessage);
        Protocol.TextMessage result = (Protocol.TextMessage) deserialized;

        assertEquals("", result.text);
        assertEquals((byte) 0, result.seq);
    }

    @Test
    public void testTextMessageSerialization_Short() {
        Protocol.TextMessage msg = new Protocol.TextMessage((byte) 1, "HELLO");
        byte[] data = msg.serialize();
        assertEquals(8, data.length);

        Protocol.Message deserialized = Protocol.Message.deserialize(data);
        assertTrue(deserialized instanceof Protocol.TextMessage);
        Protocol.TextMessage result = (Protocol.TextMessage) deserialized;

        assertEquals("HELLO", result.text);
        assertEquals((byte) 1, result.seq);
    }

    @Test
    public void testGpsMessageSerialization() {
        int lat = 37774200;
        int lon = -122419200;
        Protocol.GpsMessage msg = new Protocol.GpsMessage((byte) 5, lat, lon);
        byte[] data = msg.serialize();

        assertEquals(10, data.length);

        Protocol.Message deserialized = Protocol.Message.deserialize(data);
        assertTrue(deserialized instanceof Protocol.GpsMessage);
        Protocol.GpsMessage result = (Protocol.GpsMessage) deserialized;

        assertEquals(lat, result.lat);
        assertEquals(lon, result.lon);
        assertEquals((byte) 5, result.seq);
    }

    @Test
    public void testAckMessageSerialization() {
        Protocol.AckMessage ack = new Protocol.AckMessage((byte) 42);
        byte[] data = ack.serialize();

        assertEquals(2, data.length);

        Protocol.Message deserialized = Protocol.Message.deserialize(data);
        assertTrue(deserialized instanceof Protocol.AckMessage);
        Protocol.AckMessage result = (Protocol.AckMessage) deserialized;

        assertEquals(42, result.seq);
    }

    @Test
    public void testTextMessage_RoundTrip() {
        Protocol.TextMessage original = new Protocol.TextMessage((byte) 50, "TEST MESSAGE");

        byte[] serialized = original.serialize();
        Protocol.Message deserialized = Protocol.Message.deserialize(serialized);

        assertTrue(deserialized instanceof Protocol.TextMessage);
        Protocol.TextMessage result = (Protocol.TextMessage) deserialized;

        assertEquals(original.seq, result.seq);
        assertEquals(original.text, result.text);
    }

    @Test
    public void testIsTextSupported() {
        assertTrue(Protocol.isTextSupported("HELLO WORLD 123!"));
        assertTrue(Protocol.isTextSupported("hello")); // lowercase is supported (auto-converted to uppercase)
        assertTrue(Protocol.isTextSupported("HeLLo WoRLd")); // mixed case
        assertTrue(Protocol.isTextSupported("TEST@EMAIL.COM")); // @ and . are in charset
    }

    @Test
    public void testIsTextSupported_InvalidChars() {
        // Characters definitely not in the charset
        assertFalse(Protocol.isTextSupported("TEST\nNEWLINE")); // newline
        assertFalse(Protocol.isTextSupported("TEST\tTAB")); // tab
        assertFalse(Protocol.isTextSupported("TEST~TILDE")); // ~ not in charset
    }

    @Test
    public void testCalculatePackedSize() {
        assertEquals(0, Protocol.calculatePackedSize(""));
        assertEquals(1, Protocol.calculatePackedSize("A"));
        assertEquals(4, Protocol.calculatePackedSize("HELLO"));
    }
}
