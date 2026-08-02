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

/**
 * Name prefixes advertised by the supported boards (BASE_DEVICE_NAME in
 * firmware/platformio.ini); firmware appends "-<MAC suffix>", e.g.
 * "HellTecLite-LoRa-FADC".
 *
 * Every board name contains "LoRa", but Web Bluetooth only matches prefixes,
 * not substrings - so the prefixes are listed individually. These supplement
 * the service filter: a device whose advertisement omits the service UUID
 * (packet truncation, or a scan response the host didn't merge) is still
 * offered in the chooser.
 */
const DEVICE_NAME_PREFIXES = ['HellTecLite-LoRa', 'WirelessStick-LoRa', 'nRF52-LoRa'];

/**
 * localStorage key holding the id of the last successfully paired device,
 * so we can find it again via navigator.bluetooth.getDevices() after a reload.
 */
const LAST_DEVICE_ID_KEY = 'lora.lastDeviceId';

/**
 * Written to LAST_DEVICE_ID_KEY when the user disconnects on purpose.
 *
 * The browser keeps the pairing permission regardless, so an absent key and an
 * opted-out user are not the same thing: absent means "never paired here" (a
 * lone permitted device is safe to adopt), while this sentinel means "do not
 * reconnect until asked".
 */
const DEVICE_OPTED_OUT = 'none';

/**
 * Polling cadence for the no-watchAdvertisements fallback.
 *
 * 3s keeps the wake feeling responsive without hammering the radio; 100
 * attempts is ~5 minutes, long enough to cover someone walking back to the
 * device but short of polling all night.
 */
const RECONNECT_INTERVAL_MS = 3_000;
const RECONNECT_MAX_ATTEMPTS = 100;

export enum ConnectionState {
  DISCONNECTED = 'DISCONNECTED',
  /** Device is paired but asleep; waiting for it to advertise again. */
  WAITING_FOR_DEVICE = 'WAITING_FOR_DEVICE',
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

  private advertisementWatch: AbortController | null = null;
  private reconnectTimer: number | null = null;

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
   * Which auto-reconnect capabilities this browser exposes.
   *
   * Both are flag-gated in Chrome and gate different behaviour, so report them
   * separately: without watchAdvertisements there is no zero-tap reconnect at
   * all, while without getDevices a pairing simply cannot survive a reload.
   */
  getCapabilities(): { webBluetooth: boolean; getDevices: boolean; watchAdvertisements: boolean } {
    return {
      webBluetooth: this.isSupported(),
      getDevices: this.supportsPersistentDevices(),
      watchAdvertisements: this.supportsAdvertisementWatch()
    };
  }

  /**
   * Get current connection state
   */
  getState(): ConnectionState {
    return this.state;
  }

  /**
   * Request user to select BLE device and connect.
   *
   * Requires a user gesture: the browser always shows its device chooser here.
   * Once paired, tryAutoReconnect() can reconnect without any interaction.
   */
  async connect(): Promise<void> {
    if (!this.isSupported()) {
      throw new Error('Web Bluetooth is not supported in this browser');
    }

    this.stopAutoReconnect();

    // Clean up any existing connection before starting a new one
    // to prevent listener leaks if connect() is called while already connected
    if (this.device || this.server) {
      this.cleanup();
    }

    this.setState(ConnectionState.SCANNING);

    let selectedDevice: BluetoothDevice;
    try {
      // Match on the service UUID or on any known board name prefix, so a
      // device that advertises one but not the other still shows up.
      selectedDevice = await navigator.bluetooth.requestDevice({
        filters: [
          { services: [SERVICE_UUID] },
          ...DEVICE_NAME_PREFIXES.map((namePrefix) => ({ namePrefix }))
        ],
        // Name-matched devices still need the service to be usable.
        optionalServices: [SERVICE_UUID]
      });
    } catch (error) {
      console.error('Device selection failed:', error);
      this.setState(ConnectionState.ERROR);
      this.emitError(error as Error);
      this.cleanup();
      throw error;
    }

    console.log('Device selected:', selectedDevice.name);
    await this.connectToDevice(selectedDevice);
  }

  /**
   * Reconnect to a previously paired device without any user interaction.
   *
   * Safe to call on startup: it resolves quietly when the browser lacks
   * persistent-permission support or when no device has been paired yet.
   * If the device is asleep, watches for its advertisement and connects as
   * soon as it wakes up.
   */
  async tryAutoReconnect(): Promise<void> {
    console.log('BLE capabilities:', this.getCapabilities());

    if (!this.supportsPersistentDevices()) {
      console.log(
        'getDevices() unavailable; cannot restore a pairing across reloads. ' +
          'Enable chrome://flags/#enable-web-bluetooth-new-permissions-backend'
      );
      return;
    }

    if (this.state !== ConnectionState.DISCONNECTED && this.state !== ConnectionState.ERROR) {
      return;
    }

    const device = await this.findKnownDevice();
    if (!device) {
      console.log('No previously paired device available');
      return;
    }

    console.log('Found previously paired device:', device.name);

    try {
      await this.connectToDevice(device);
      return;
    } catch {
      // Device is most likely asleep - wait for it to advertise again.
      console.log('Device not reachable, waiting for it to wake up');
    }

    this.startAdvertisementWatch(device);
  }

  /**
   * Disconnect from device.
   *
   * This is an explicit user action, so it also forgets the device and stops
   * all automatic reconnection: otherwise we would reconnect against intent.
   */
  async disconnect(): Promise<void> {
    this.stopAutoReconnect();
    this.forgetDevice();

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
   * Private: Connect to an already-selected device and set up characteristics.
   * Shared by the chooser path (connect) and the auto-reconnect path.
   */
  private async connectToDevice(device: BluetoothDevice): Promise<void> {
    this.device = device;

    try {
      // Listen for disconnection
      device.addEventListener('gattserverdisconnected', this.onDisconnected);

      this.setState(ConnectionState.CONNECTING);

      // Connect to GATT server
      if (!device.gatt) {
        throw new Error('GATT server not available on device');
      }
      this.server = await device.gatt.connect();
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

      this.rememberDevice(device);
      this.setState(ConnectionState.CONNECTED);
    } catch (error) {
      console.error('Connection failed:', error);
      this.setState(ConnectionState.ERROR);
      this.emitError(error as Error);
      this.cleanup();
      throw error;
    }
  }

  /**
   * Private: Whether the browser exposes persistent device permissions and
   * passive advertisement watching (Chrome's new Web Bluetooth permissions
   * backend). Without these, only the manual chooser flow is available.
   */
  private supportsPersistentDevices(): boolean {
    return this.isSupported() && typeof navigator.bluetooth.getDevices === 'function';
  }

  /**
   * Private: Whether devices can be watched for advertisements.
   *
   * Deliberately independent of getDevices(): the two ship behind different
   * flags. Re-arming a watch after a disconnect only needs an existing device
   * handle, so it must not be blocked by the absence of getDevices().
   */
  private supportsAdvertisementWatch(): boolean {
    // @types/web-bluetooth declares BluetoothDevice as a type only, so reach
    // for the runtime constructor through globalThis.
    const deviceCtor = (globalThis as { BluetoothDevice?: { prototype: object } }).BluetoothDevice;

    return this.isSupported() && !!deviceCtor && 'watchAdvertisements' in deviceCtor.prototype;
  }

  /**
   * Private: Look up a previously paired device among those this origin has
   * permission for.
   */
  private async findKnownDevice(): Promise<BluetoothDevice | null> {
    let devices: BluetoothDevice[];
    try {
      devices = await navigator.bluetooth.getDevices();
    } catch (error) {
      console.warn('Failed to list known devices:', error);
      return null;
    }

    if (devices.length === 0) return null;

    const storedId = this.readStoredDeviceId();

    // The user disconnected on purpose - stay disconnected until they ask.
    if (storedId === DEVICE_OPTED_OUT) return null;

    const match = devices.find((d) => d.id === storedId);
    if (match) return match;

    // No stored id (or a stale one), but exactly one device is permitted:
    // it can only be ours, since permission was granted via our service filter.
    return devices.length === 1 ? devices[0] : null;
  }

  /**
   * Private: Wait for the device to start advertising, then reconnect.
   * Used on startup and after an unexpected disconnect, so waking the device
   * with its button is enough to restore the session.
   */
  private startAdvertisementWatch(device: BluetoothDevice): void {
    if (!this.supportsAdvertisementWatch()) {
      // watchAdvertisements is flag-gated in Chrome. Until it ships, poll the
      // handle instead: gatt.connect() needs no flag and succeeds as soon as
      // the device is awake, which is the same outcome a little less promptly.
      this.startReconnectPolling(device);
      return;
    }

    this.stopAdvertisementWatch();

    const controller = new AbortController();
    this.advertisementWatch = controller;

    const onAdvertisement = () => {
      console.log('Device is advertising again, reconnecting');
      this.stopAdvertisementWatch();
      this.connectToDevice(device).catch((error: unknown) => {
        console.warn('Auto-reconnect failed, resuming watch:', error);
        this.startAdvertisementWatch(device);
      });
    };

    device.addEventListener('advertisementreceived', onAdvertisement, { once: true });
    controller.signal.addEventListener('abort', () => {
      device.removeEventListener('advertisementreceived', onAdvertisement);
    });

    device.watchAdvertisements({ signal: controller.signal }).then(
      () => {
        this.setState(ConnectionState.WAITING_FOR_DEVICE);
      },
      (error: unknown) => {
        console.warn('Failed to watch advertisements:', error);
        this.stopAdvertisementWatch();
        this.setState(ConnectionState.DISCONNECTED);
      }
    );
  }

  /**
   * Private: Retry gatt.connect() until the device wakes up.
   *
   * Fallback for browsers without watchAdvertisements. Only works while the
   * page holds the device handle - a reload loses it and needs getDevices(),
   * which is flag-gated too. Gives up after RECONNECT_MAX_ATTEMPTS so a device
   * that is off for the night does not poll the radio forever.
   */
  private startReconnectPolling(device: BluetoothDevice, attempt = 0): void {
    this.stopReconnectPolling();

    if (attempt >= RECONNECT_MAX_ATTEMPTS) {
      console.log('Gave up waiting for device to wake up');
      this.setState(ConnectionState.DISCONNECTED);
      return;
    }

    this.setState(ConnectionState.WAITING_FOR_DEVICE);

    this.reconnectTimer = window.setTimeout(() => {
      // A manual connect may have landed while this was pending.
      if (this.isConnected()) return;

      this.connectToDevice(device)
        .then(() => {
          console.log('Reconnected after device woke up');
        })
        .catch(() => {
          this.startReconnectPolling(device, attempt + 1);
        });
    }, RECONNECT_INTERVAL_MS);
  }

  /**
   * Private: Cancel any pending reconnect attempt
   */
  private stopReconnectPolling(): void {
    if (this.reconnectTimer !== null) {
      window.clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
  }

  /**
   * Private: Stop all automatic reconnection, whichever mechanism is in use.
   * Call before any user-initiated connect or disconnect so the automation
   * never races the user.
   */
  private stopAutoReconnect(): void {
    this.stopAdvertisementWatch();
    this.stopReconnectPolling();
  }

  /**
   * Private: Cancel any in-flight advertisement watch
   */
  private stopAdvertisementWatch(): void {
    if (this.advertisementWatch) {
      this.advertisementWatch.abort();
      this.advertisementWatch = null;
    }
  }

  private rememberDevice(device: BluetoothDevice): void {
    try {
      localStorage.setItem(LAST_DEVICE_ID_KEY, device.id);
    } catch (error) {
      console.warn('Failed to remember device:', error);
    }
  }

  private forgetDevice(): void {
    try {
      localStorage.setItem(LAST_DEVICE_ID_KEY, DEVICE_OPTED_OUT);
    } catch (error) {
      console.warn('Failed to forget device:', error);
    }
  }

  private readStoredDeviceId(): string | null {
    try {
      return localStorage.getItem(LAST_DEVICE_ID_KEY);
    } catch {
      return null;
    }
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

    // Capture the handle before cleanup() clears it, so we can keep watching
    // for the device to come back (e.g. after it wakes from deep sleep).
    const device = this.device;

    this.cleanup();
    this.setState(ConnectionState.DISCONNECTED);

    if (device) {
      this.startAdvertisementWatch(device);
    }
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
   * Private: Cleanup resources
   */
  private cleanup(): void {
    const canStopNotifications = this.server?.connected === true;

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
