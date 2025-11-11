/**
 * BLE Service using Web Bluetooth API
 *
 * Handles connection to ESP32-S3 LoRa device
 */

import { deserialize, type Message, serialize } from '../protocol';

/**
 * BLE UUIDs matching ESP32 firmware
 */
const SERVICE_UUID = '00001234-0000-1000-8000-00805f9b34fb';
const TX_CHAR_UUID = '00005678-0000-1000-8000-00805f9b34fb'; // ESP32 → Web (notifications)
const RX_CHAR_UUID = '00005679-0000-1000-8000-00805f9b34fb'; // Web → ESP32 (writes)

export enum ConnectionState {
  DISCONNECTED = 'DISCONNECTED',
  SCANNING = 'SCANNING',
  CONNECTING = 'CONNECTING',
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

    this.setState(ConnectionState.SCANNING);

    try {
      // Request device with LoRa service filter
      this.device = await navigator.bluetooth.requestDevice({
        filters: [{ services: [SERVICE_UUID] }, { name: 'ESP32S3-LoRa' }],
        optionalServices: [SERVICE_UUID]
      });

      console.log('Device selected:', this.device.name);

      // Listen for disconnection
      this.device.addEventListener('gattserverdisconnected', this.onDisconnected);

      this.setState(ConnectionState.CONNECTING);

      // Connect to GATT server
      if (!this.device.gatt) {
        throw new Error('GATT server not available on device');
      }
      this.server = await this.device.gatt.connect();
      console.log('GATT server connected');

      // Discover LoRa service
      const service = await this.server.getPrimaryService(SERVICE_UUID);
      console.log('LoRa service discovered');

      // Get characteristics
      this.txCharacteristic = await service.getCharacteristic(TX_CHAR_UUID);
      this.rxCharacteristic = await service.getCharacteristic(RX_CHAR_UUID);
      console.log('Characteristics discovered');

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
      console.error('Failed to parse received message:', error);
      this.emitError(error as Error);
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
        this.txCharacteristic.stopNotifications();
      } catch (error) {
        console.warn('Error stopping notifications:', error);
      }
      this.txCharacteristic = null;
    }

    this.rxCharacteristic = null;
    this.server = null;

    if (this.device) {
      this.device.removeEventListener('gattserverdisconnected', this.onDisconnected);
      this.device = null;
    }
  }
}

// Singleton instance
export const bleService = new BleService();
