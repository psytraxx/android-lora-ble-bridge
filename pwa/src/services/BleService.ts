/**
 * BLE Service using Web Bluetooth API
 *
 * Handles connection to ESP32-S3 LoRa device
 */

import { deserialize, type Message, serialize } from '../protocol';
import type { DeviceInfo } from '../protocol/types';
import { messageRepository } from './MessageRepository';

/**
 * BLE UUIDs matching ESP32 firmware
 */
const SERVICE_UUID = '00001234-0000-1000-8000-00805f9b34fb';
const TX_CHAR_UUID = '00005678-0000-1000-8000-00805f9b34fb'; // ESP32 → Web (notifications)
const RX_CHAR_UUID = '00005679-0000-1000-8000-00805f9b34fb'; // Web → ESP32 (writes)
const INFO_CHAR_UUID = '0000567a-0000-1000-8000-00805f9b34fb'; // Device info (read-only, 16 bytes)

export enum ConnectionState {
  DISCONNECTED = 'DISCONNECTED',
  SCANNING = 'SCANNING',
  CONNECTING = 'CONNECTING',
  DISCOVERING = 'DISCOVERING',
  ENABLING_NOTIFICATIONS = 'ENABLING_NOTIFICATIONS',
  CONNECTED = 'CONNECTED',
  ERROR = 'ERROR'
}

export interface BleDevice {
  id: string;
  name: string;
  rssi?: number;
}

type StateListener = (state: ConnectionState) => void;
type MessageListener = (message: Message) => void;
type ErrorListener = (error: Error) => void;

/**
 * BLE Service for Web Bluetooth communication
 */
export class BleService {
  private device: BluetoothDevice | null = null;
  private server: BluetoothRemoteGATTServer | null = null;
  private txCharacteristic: BluetoothRemoteGATTCharacteristic | null = null;
  private rxCharacteristic: BluetoothRemoteGATTCharacteristic | null = null;
  private infoCharacteristic: BluetoothRemoteGATTCharacteristic | null = null;

  private state: ConnectionState = ConnectionState.DISCONNECTED;
  private stateListeners = new Set<StateListener>();
  private messageListeners = new Set<MessageListener>();
  private errorListeners = new Set<ErrorListener>();

  private disconnectTimeout: number | null = null;
  private readonly AUTO_DISCONNECT_MS = 60_000; // 60 seconds

  constructor() {
    // Check Web Bluetooth support
    if (!navigator.bluetooth) {
      console.error('Web Bluetooth API not supported');
    }
  }

  /**
   * Get information about the currently selected device
   */
  getDevice(): BleDevice | null {
    if (!this.device) return null;
    return { id: this.device.id, name: this.device.name ?? '' };
  }

  /**
   * Check if Web Bluetooth is supported
   */
  isSupported(): boolean {
    return !!navigator.bluetooth;
  }

  /**
   * Get current connection state
   */
  getState(): ConnectionState {
    return this.state;
  }

  /**
   * Request user to select BLE device and connect
   */
  async connect(): Promise<void> {
    if (!this.isSupported()) {
      throw new Error('Web Bluetooth is not supported in this browser');
    }

    // Clean up any existing connection before starting a new one
    // to prevent listener leaks if connect() is called while already connected
    if (this.device || this.server) {
      this.cleanup();
    }

    this.setState(ConnectionState.SCANNING);

    try {
      // Request device with LoRa service filter
      this.device = await navigator.bluetooth.requestDevice({
        filters: [{ services: [SERVICE_UUID] }]
      });

      const selectedDevice = this.device;
      if (!selectedDevice) {
        throw new Error('No device selected');
      }

      console.log('Device selected:', selectedDevice.name);

      // Listen for disconnection
      selectedDevice.addEventListener('gattserverdisconnected', this.onDisconnected);

      this.setState(ConnectionState.CONNECTING);

      // Connect to GATT server
      if (!selectedDevice.gatt) {
        throw new Error('GATT server not available on device');
      }
      this.server = await selectedDevice.gatt.connect();
      console.log('GATT server connected');

      this.setState(ConnectionState.DISCOVERING);

      // Discover LoRa service
      const service = await this.server.getPrimaryService(SERVICE_UUID);
      console.log('LoRa service discovered');

      // Get characteristics
      this.txCharacteristic = await service.getCharacteristic(TX_CHAR_UUID);
      this.rxCharacteristic = await service.getCharacteristic(RX_CHAR_UUID);
      console.log('Characteristics discovered');

      // Get device info characteristic (optional - don't fail if not present)
      try {
        this.infoCharacteristic = await service.getCharacteristic(INFO_CHAR_UUID);
        console.log('Device info characteristic discovered');
      } catch {
        console.log('Device info characteristic not available');
      }

      this.setState(ConnectionState.ENABLING_NOTIFICATIONS);

      // Enable notifications on TX characteristic
      await this.txCharacteristic.startNotifications();
      this.txCharacteristic.addEventListener('characteristicvaluechanged', this.onNotification);
      console.log('Notifications enabled');

      this.setState(ConnectionState.CONNECTED);
      this.resetDisconnectTimeout();
    } catch (error) {
      console.error('Connection failed:', error);
      this.setState(ConnectionState.ERROR);
      this.emitError(error as Error);
      this.cleanup();
      throw error;
    }
  }

  /**
   * Disconnect from device
   */
  async disconnect(): Promise<void> {
    if (this.server?.connected) {
      this.server.disconnect();
    }
    this.cleanup();
    this.setState(ConnectionState.DISCONNECTED);
  }

  /**
   * Send a message to the ESP32
   */
  async sendMessage(message: Message): Promise<void> {
    if (this.state !== ConnectionState.CONNECTED || !this.rxCharacteristic) {
      throw new Error('Not connected');
    }

    try {
      const data = serialize(message);
      console.log('Sending message:', message, 'bytes:', data.length);

      // @ts-expect-error - Web Bluetooth API typing issue with Uint8Array
      await this.rxCharacteristic.writeValueWithResponse(data);
      this.resetDisconnectTimeout();
    } catch (error) {
      console.error('Failed to send message:', error);
      this.emitError(error as Error);
      throw error;
    }
  }

  /**
   * Read device info from the BLE characteristic
   * Returns battery, RSSI, SNR, and LoRa configuration
   */
  async requestDeviceInfo(): Promise<DeviceInfo | null> {
    if (!this.infoCharacteristic || !this.isConnected()) {
      return null;
    }

    try {
      const value = await this.infoCharacteristic.readValue();
      if (value.byteLength < 16) {
        console.warn('Device info too short:', value.byteLength);
        return null;
      }

      const batteryLevel = value.getUint8(0);
      const rssi = value.getInt16(1, true); // little-endian
      const snrX100 = value.getInt16(3, true);
      const txPower = value.getInt8(5);
      const frequencyHz = value.getUint32(6, true);
      const bandwidthHz = value.getUint32(10, true);
      const spreadingFactor = value.getUint8(14);
      const codingRate = value.getUint8(15);

      this.resetDisconnectTimeout();

      return {
        batteryLevel,
        rssi,
        snr: snrX100 / 100,
        txPower,
        frequencyHz,
        bandwidthHz,
        spreadingFactor,
        codingRate
      };
    } catch (error) {
      console.error('Failed to read device info:', error);
      return null;
    }
  }

  /**
   * Check if currently connected
   */
  isConnected(): boolean {
    return this.state === ConnectionState.CONNECTED && this.server?.connected === true;
  }

  /**
   * Event listeners
   */
  onStateChange(listener: StateListener): () => void {
    this.stateListeners.add(listener);
    return () => this.stateListeners.delete(listener);
  }

  onMessage(listener: MessageListener): () => void {
    this.messageListeners.add(listener);
    return () => this.messageListeners.delete(listener);
  }

  onError(listener: ErrorListener): () => void {
    this.errorListeners.add(listener);
    return () => this.errorListeners.delete(listener);
  }

  /**
   * Private: Handle disconnection
   */
  private onDisconnected = (): void => {
    console.log('Device disconnected');
    // Clear messages when the device disconnects to reflect disconnected state in the UI
    try {
      messageRepository.clearMessages();
    } catch (e) {
      console.warn('Failed to clear messages on disconnect:', e);
    }

    this.cleanup();
    this.setState(ConnectionState.DISCONNECTED);
  };

  /**
   * Private: Handle incoming notification
   */
  private onNotification = (event: Event): void => {
    const characteristic = event.target as BluetoothRemoteGATTCharacteristic;
    const value = characteristic.value;

    if (!value) return;

    try {
      const data = new Uint8Array(value.buffer);
      console.log('Received data:', data);

      const message = deserialize(data);
      console.log('Parsed message:', message);

      this.emitMessage(message);
      this.resetDisconnectTimeout();
    } catch (error) {
      console.warn(
        'Failed to parse received message (possibly corrupt/truncated), ignoring:',
        error
      );
      console.warn('Raw data:', new Uint8Array(value.buffer));
    }
  };

  /**
   * Private: Set state and notify listeners
   */
  private setState(newState: ConnectionState): void {
    if (this.state !== newState) {
      this.state = newState;
      console.log('State changed:', newState);
      this.stateListeners.forEach((listener) => {
        listener(newState);
      });
    }
  }

  /**
   * Private: Emit message to listeners
   */
  private emitMessage(message: Message): void {
    this.messageListeners.forEach((listener) => {
      listener(message);
    });
  }

  /**
   * Private: Emit error to listeners
   */
  private emitError(error: Error): void {
    this.errorListeners.forEach((listener) => {
      listener(error);
    });
  }

  /**
   * Private: Reset auto-disconnect timeout
   */
  private resetDisconnectTimeout(): void {
    if (this.disconnectTimeout !== null) {
      window.clearTimeout(this.disconnectTimeout);
    }

    this.disconnectTimeout = window.setTimeout(() => {
      console.log('Auto-disconnect timeout reached');
      this.disconnect();
    }, this.AUTO_DISCONNECT_MS);
  }

  /**
   * Private: Cleanup resources
   */
  private cleanup(): void {
    const canStopNotifications = this.server?.connected === true;

    if (this.disconnectTimeout !== null) {
      window.clearTimeout(this.disconnectTimeout);
      this.disconnectTimeout = null;
    }

    if (this.txCharacteristic) {
      try {
        this.txCharacteristic.removeEventListener(
          'characteristicvaluechanged',
          this.onNotification
        );
        if (canStopNotifications) {
          void this.txCharacteristic.stopNotifications().catch((error) => {
            console.warn('Error stopping notifications:', error);
          });
        }
      } catch (error) {
        console.warn('Error stopping notifications:', error);
      }
      this.txCharacteristic = null;
    }

    this.rxCharacteristic = null;
    this.infoCharacteristic = null;
    this.server = null;

    if (this.device) {
      this.device.removeEventListener('gattserverdisconnected', this.onDisconnected);
      this.device = null;
    }
  }
}

// Singleton instance
export const bleService = new BleService();
