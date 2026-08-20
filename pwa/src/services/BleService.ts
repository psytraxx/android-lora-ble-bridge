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
 * localStorage key holding the last-paired device as JSON ({ id, name }), so
 * we can find it again via navigator.bluetooth.getDevices() after a reload
 * and show its name in the UI before any connection exists.
 */
const KNOWN_DEVICE_KEY = 'lora.knownDevice';

/**
 * Written to KNOWN_DEVICE_KEY by forgetDevice(), in place of removing the key.
 *
 * An absent key and an explicitly-forgotten device are not the same thing:
 * absent means "never paired on this browser", which is why findKnownDevice()
 * safely auto-adopts a lone permitted device in that case. This sentinel means
 * "the user deliberately let this one go" - important when testing with a
 * second board, where the origin still has permission for the first and the
 * adoption shortcut must not silently re-pair it.
 */
const DEVICE_FORGOTTEN = 'forgotten';

/**
 * localStorage key holding the user's auto-reconnect preference ('on' | 'off').
 * Absent means "on" - auto-reconnect is the default, opt-out behaviour.
 */
const AUTO_RECONNECT_KEY = 'lora.autoReconnect';

/**
 * Superseded storage scheme: a bare device id, with the sentinel 'none'
 * meaning "user opted out". Migrated into KNOWN_DEVICE_KEY / AUTO_RECONNECT_KEY
 * on first construction so existing installs keep their pairing.
 */
const LEGACY_LAST_DEVICE_ID_KEY = 'lora.lastDeviceId';
const LEGACY_OPTED_OUT = 'none';

/**
 * How long connectToDevice() waits for the GATT/service/characteristic
 * handshake before giving up. Without this, a device that ACKs the radio
 * link but never completes GATT discovery (a real failure mode on some
 * Android BLE stacks) pins the state at CONNECTING forever.
 */
const CONNECT_TIMEOUT_MS = 15_000;

/**
 * Backoff for the no-watchAdvertisements reconnect loop (and for resuming
 * an advertisement watch that failed to arm): starts responsive, backs off
 * to avoid hammering the radio while the device is off for a while, and
 * gives up after RECONNECT_MAX_ATTEMPTS so it does not poll all night.
 */
const RECONNECT_BASE_DELAY_MS = 3_000;
const RECONNECT_MAX_DELAY_MS = 30_000;
const RECONNECT_MAX_ATTEMPTS = 20;

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

/** A remembered pairing, persisted so the app can reconnect without the chooser. */
export interface KnownDevice {
  id: string;
  name: string;
}

type StateListener = (state: ConnectionState) => void;
type MessageListener = (message: Message) => void;
type ErrorListener = (error: Error) => void;
type SettingsListener = () => void;

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
  private settingsListeners = new Set<SettingsListener>();

  private advertisementWatch: AbortController | null = null;
  private reconnectTimer: number | null = null;
  private reconnectAttempt = 0;
  /**
   * The device a watch/poll is currently waiting on. Separate from `device`,
   * which cleanup() nulls out as soon as the link drops - retryNow() needs a
   * handle to retry against even while nothing is connected.
   */
  private waitingDevice: BluetoothDevice | null = null;

  /** Set right before we tear down a link ourselves, so onDisconnected knows not to re-arm. */
  private userInitiatedDisconnect = false;

  constructor() {
    // Check Web Bluetooth support
    if (!navigator.bluetooth) {
      console.error('Web Bluetooth API not supported');
    }

    this.migrateLegacyStorage();
  }

  /**
   * Get information about the currently selected device
   */
  getDevice(): BleDevice | null {
    if (!this.device) return null;
    return { id: this.device.id, name: this.device.name ?? '' };
  }

  /**
   * Get the remembered pairing, if any - available even while disconnected,
   * so the UI can say which device auto-reconnect will target.
   */
  getKnownDevice(): KnownDevice | null {
    try {
      const raw = localStorage.getItem(KNOWN_DEVICE_KEY);
      if (!raw) return null;
      const parsed = JSON.parse(raw) as { id?: unknown; name?: unknown };
      if (typeof parsed.id !== 'string') return null;
      return { id: parsed.id, name: typeof parsed.name === 'string' ? parsed.name : '' };
    } catch {
      return null;
    }
  }

  /**
   * Whether auto-reconnect (startup restore, watch-for-wake, poll-for-wake)
   * is allowed to run. Defaults to on; the user can switch it off to test
   * with a different device without the app racing them for the connection.
   */
  isAutoReconnectEnabled(): boolean {
    try {
      return localStorage.getItem(AUTO_RECONNECT_KEY) !== 'off';
    } catch {
      return true;
    }
  }

  /**
   * Persist the auto-reconnect preference and act on it immediately: turning
   * it on retries right away, turning it off cancels any in-flight watch or
   * poll so it cannot race a manual connect the user is about to start.
   */
  setAutoReconnectEnabled(enabled: boolean): void {
    try {
      localStorage.setItem(AUTO_RECONNECT_KEY, enabled ? 'on' : 'off');
    } catch (error) {
      console.warn('Failed to persist auto-reconnect preference:', error);
    }
    this.emitSettingsChange();

    if (enabled) {
      void this.tryAutoReconnect();
      return;
    }

    this.stopAutoReconnect();
    // WAITING_FOR_DEVICE only exists while a watch/poll is active; both are
    // now stopped, so leaving it set would strand the UI in that state.
    if (this.state === ConnectionState.WAITING_FOR_DEVICE) {
      this.setState(ConnectionState.DISCONNECTED);
    }
  }

  /**
   * Reset the backoff and retry immediately. Used when something changed
   * that makes success more likely right now - the tab regained visibility,
   * or the adapter came back on - rather than waiting out the current delay.
   */
  retryNow(): void {
    if (!this.isAutoReconnectEnabled() || this.isConnected()) return;

    if (
      this.state !== ConnectionState.DISCONNECTED &&
      this.state !== ConnectionState.ERROR &&
      this.state !== ConnectionState.WAITING_FOR_DEVICE
    ) {
      return;
    }

    // this.device is only set while actually connected - while waiting for a
    // reconnect it's already been cleared, so fall back to the device a
    // watch/poll is currently targeting.
    const device = this.device ?? this.waitingDevice;

    this.stopAutoReconnect(); // cancels any pending watch/timer, resets the backoff

    if (device) {
      this.connectToDevice(device).catch(() => {
        this.startAdvertisementWatch(device);
      });
      return;
    }

    void this.tryAutoReconnect();
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
   * persistent-permission support, when auto-reconnect is switched off, or
   * when no device has been paired yet. If the device is asleep, watches for
   * its advertisement and connects as soon as it wakes up.
   */
  async tryAutoReconnect(): Promise<void> {
    console.log('BLE capabilities:', this.getCapabilities());

    if (!this.isAutoReconnectEnabled()) {
      console.log('Auto-reconnect is switched off');
      return;
    }

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
    this.reconnectAttempt = 0;

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
   * This is an explicit user action, so it also stops all automatic
   * reconnection - otherwise we would reconnect against intent. It keeps the
   * remembered device, though: switching auto-reconnect back on should resume
   * without going through the chooser again. Use forgetDevice() to discard
   * the pairing itself.
   */
  async disconnect(): Promise<void> {
    this.stopAutoReconnect();

    try {
      localStorage.setItem(AUTO_RECONNECT_KEY, 'off');
    } catch (error) {
      console.warn('Failed to persist auto-reconnect preference:', error);
    }

    if (this.server?.connected) {
      this.userInitiatedDisconnect = true;
      this.server.disconnect();
    }
    this.cleanup();
    this.setState(ConnectionState.DISCONNECTED);
    this.emitSettingsChange();
  }

  /**
   * Forget the remembered device: stops any auto-reconnect, drops the link if
   * live, and clears the stored pairing so a future reconnect needs the
   * chooser again. Leaves the auto-reconnect preference untouched.
   */
  forgetDevice(): void {
    this.stopAutoReconnect();

    try {
      localStorage.setItem(KNOWN_DEVICE_KEY, DEVICE_FORGOTTEN);
    } catch (error) {
      console.warn('Failed to forget device:', error);
    }

    if (this.server?.connected) {
      this.userInitiatedDisconnect = true;
      this.server.disconnect();
    }
    this.cleanup();
    this.setState(ConnectionState.DISCONNECTED);
    this.emitSettingsChange();
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

  /** Fires when the remembered device or the auto-reconnect preference changes. */
  onSettingsChange(listener: SettingsListener): () => void {
    this.settingsListeners.add(listener);
    return () => this.settingsListeners.delete(listener);
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

      await this.withTimeout(this.establishConnection(device), CONNECT_TIMEOUT_MS);

      this.reconnectAttempt = 0;
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
   * Private: GATT connect + service/characteristic discovery + notifications.
   * Split out from connectToDevice() so it can be raced against a timeout
   * without duplicating the surrounding state-machine/error handling.
   */
  private async establishConnection(device: BluetoothDevice): Promise<void> {
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
  }

  /**
   * Private: Reject with a timeout error if the given promise takes too long.
   * Used to bound connectToDevice() - without it a hung gatt.connect() (a
   * real failure mode on some Android BLE stacks) pins the state at
   * CONNECTING with no way out.
   */
  private withTimeout<T>(promise: Promise<T>, ms: number): Promise<T> {
    return new Promise((resolve, reject) => {
      const timer = window.setTimeout(() => {
        reject(new Error('Connection timed out'));
      }, ms);

      promise.then(
        (value) => {
          window.clearTimeout(timer);
          resolve(value);
        },
        (error: unknown) => {
          window.clearTimeout(timer);
          reject(error as Error);
        }
      );
    });
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

    const known = this.getKnownDevice();
    if (known) {
      return devices.find((d) => d.id === known.id) ?? null;
    }

    // getKnownDevice() also returns null for an explicitly forgotten device
    // (the DEVICE_FORGOTTEN sentinel) - that must not fall through to
    // adoption below, or "forget device" would just re-pair itself whenever
    // only one board happens to be permitted.
    let hasStoredRecord: boolean;
    try {
      hasStoredRecord = localStorage.getItem(KNOWN_DEVICE_KEY) !== null;
    } catch {
      hasStoredRecord = false;
    }
    if (hasStoredRecord) return null;

    // Truly nothing recorded yet, but exactly one device is permitted: it can
    // only be ours, since permission was granted via our service filter.
    return devices.length === 1 ? devices[0] : null;
  }

  /**
   * Private: Wait for the device to start advertising, then reconnect.
   * Used on startup and after an unexpected disconnect, so waking the device
   * with its button is enough to restore the session.
   */
  private startAdvertisementWatch(device: BluetoothDevice): void {
    if (!this.isAutoReconnectEnabled()) return;

    this.waitingDevice = device;

    if (!this.supportsAdvertisementWatch()) {
      // watchAdvertisements is flag-gated in Chrome. Until it ships, poll the
      // handle instead: gatt.connect() needs no flag and succeeds as soon as
      // the device is awake, which is the same outcome a little less promptly.
      this.startReconnectPolling(device);
      return;
    }

    if (this.reconnectAttempt >= RECONNECT_MAX_ATTEMPTS) {
      this.giveUpWaiting();
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
        this.reconnectAttempt += 1;
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
        // The watch itself failed to arm (not just "no advertisement yet") -
        // fall back to polling instead of stranding the user disconnected.
        console.warn('Failed to watch advertisements, falling back to polling:', error);
        this.stopAdvertisementWatch();
        this.startReconnectPolling(device);
      }
    );
  }

  /**
   * Private: Retry gatt.connect() until the device wakes up, backing off
   * between attempts.
   *
   * Fallback for browsers without watchAdvertisements. Only works while the
   * page holds the device handle - a reload loses it and needs getDevices(),
   * which is flag-gated too. Gives up after RECONNECT_MAX_ATTEMPTS so a device
   * that is off for the night does not poll the radio forever.
   */
  private startReconnectPolling(device: BluetoothDevice, attempt = this.reconnectAttempt): void {
    if (!this.isAutoReconnectEnabled()) return;

    this.waitingDevice = device;
    this.stopReconnectPolling();

    if (attempt >= RECONNECT_MAX_ATTEMPTS) {
      this.giveUpWaiting();
      return;
    }

    this.setState(ConnectionState.WAITING_FOR_DEVICE);

    const delay = this.nextReconnectDelay(attempt);
    this.reconnectTimer = window.setTimeout(() => {
      // A manual connect may have landed while this was pending.
      if (this.isConnected()) return;

      this.connectToDevice(device)
        .then(() => {
          console.log('Reconnected after device woke up');
        })
        .catch(() => {
          this.reconnectAttempt = attempt + 1;
          this.startReconnectPolling(device, attempt + 1);
        });
    }, delay);
  }

  /**
   * Private: Exponential backoff with a small jitter, capped so waits stay
   * bounded even after many failed attempts.
   */
  private nextReconnectDelay(attempt: number): number {
    const base = Math.min(RECONNECT_BASE_DELAY_MS * 2 ** attempt, RECONNECT_MAX_DELAY_MS);
    const jitter = Math.random() * Math.min(500, base * 0.1);
    return base + jitter;
  }

  /**
   * Private: Stop waiting after the attempt cap and tell the user how to retry.
   */
  private giveUpWaiting(): void {
    console.log('Gave up waiting for device to wake up');
    this.setState(ConnectionState.DISCONNECTED);
    this.emitError(new Error('Device did not reconnect. Press Connect to try again.'));
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
   * Private: Stop all automatic reconnection, whichever mechanism is in use,
   * and reset the backoff so the next reconnect cycle starts fresh.
   * Call before any user-initiated connect or disconnect so the automation
   * never races the user.
   */
  private stopAutoReconnect(): void {
    this.stopAdvertisementWatch();
    this.stopReconnectPolling();
    this.reconnectAttempt = 0;
    this.waitingDevice = null;
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
      const record: KnownDevice = { id: device.id, name: device.name ?? '' };
      localStorage.setItem(KNOWN_DEVICE_KEY, JSON.stringify(record));
    } catch (error) {
      console.warn('Failed to remember device:', error);
    }
    this.emitSettingsChange();
  }

  /**
   * Private: One-time migration from the old bare-id + 'none'-sentinel scheme
   * to the current { id, name } record plus a separate on/off preference.
   * Runs quietly on construction so existing installs keep their pairing.
   */
  private migrateLegacyStorage(): void {
    try {
      const legacy = localStorage.getItem(LEGACY_LAST_DEVICE_ID_KEY);
      if (legacy === null) return;

      if (legacy === LEGACY_OPTED_OUT) {
        localStorage.setItem(AUTO_RECONNECT_KEY, 'off');
      } else if (localStorage.getItem(KNOWN_DEVICE_KEY) === null) {
        const record: KnownDevice = { id: legacy, name: '' };
        localStorage.setItem(KNOWN_DEVICE_KEY, JSON.stringify(record));
      }

      localStorage.removeItem(LEGACY_LAST_DEVICE_ID_KEY);
    } catch (error) {
      console.warn('Failed to migrate legacy device storage:', error);
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
    const wasUserInitiated = this.userInitiatedDisconnect;
    this.userInitiatedDisconnect = false;

    this.cleanup();
    this.setState(ConnectionState.DISCONNECTED);

    if (device && !wasUserInitiated) {
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
   * Private: Notify listeners that the remembered device or the
   * auto-reconnect preference changed.
   */
  private emitSettingsChange(): void {
    this.settingsListeners.forEach((listener) => {
      listener();
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
