import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { BleService, ConnectionState } from './BleService';

const KNOWN_DEVICE_KEY = 'lora.knownDevice';
const LEGACY_LAST_DEVICE_ID_KEY = 'lora.lastDeviceId';

/** A GATT server exposing the service and characteristics BleService expects. */
function makeServer() {
  const characteristic = {
    startNotifications: vi.fn().mockResolvedValue(undefined),
    stopNotifications: vi.fn().mockResolvedValue(undefined),
    addEventListener: vi.fn(),
    removeEventListener: vi.fn()
  };

  return {
    connected: true,
    disconnect: vi.fn(),
    getPrimaryService: vi.fn().mockResolvedValue({
      getCharacteristic: vi.fn().mockResolvedValue(characteristic)
    })
  };
}

/**
 * Minimal stand-in for a BluetoothDevice. Extends EventTarget so the service's
 * real addEventListener/dispatchEvent wiring is exercised.
 */
class FakeDevice extends EventTarget {
  gatt: { connect: () => Promise<unknown> };
  watchAdvertisements: (options?: { signal?: AbortSignal }) => Promise<void>;

  constructor(
    public id: string,
    public name = 'ESP32S3-LoRa',
    connectable = true
  ) {
    super();

    this.gatt = {
      connect: connectable
        ? vi.fn().mockResolvedValue(makeServer())
        : vi.fn().mockRejectedValue(new Error('GATT connect failed: device unreachable'))
    };

    this.watchAdvertisements = vi.fn().mockResolvedValue(undefined);
  }
}

/** Install a fake Web Bluetooth implementation with the given capabilities. */
function stubBluetooth(options: {
  devices?: FakeDevice[];
  requestDevice?: FakeDevice;
  persistent?: boolean;
}) {
  const { devices = [], requestDevice, persistent = true } = options;

  const bluetooth: Record<string, unknown> = {
    requestDevice: requestDevice
      ? vi.fn().mockResolvedValue(requestDevice)
      : vi.fn().mockRejectedValue(new Error('User cancelled')),
    addEventListener: vi.fn(),
    removeEventListener: vi.fn()
  };

  if (persistent) {
    bluetooth.getDevices = vi.fn().mockResolvedValue(devices);
    // supportsPersistentDevices() probes the global constructor's prototype
    (globalThis as Record<string, unknown>).BluetoothDevice = class {
      watchAdvertisements() {}
    };
  } else {
    delete (globalThis as Record<string, unknown>).BluetoothDevice;
  }

  Object.defineProperty(navigator, 'bluetooth', {
    value: bluetooth,
    configurable: true,
    writable: true
  });

  return bluetooth;
}

/** Let queued promise callbacks settle. */
const flush = () => new Promise((resolve) => setTimeout(resolve, 0));

describe('BleService auto-reconnect', () => {
  beforeEach(() => {
    localStorage.clear();
  });

  afterEach(() => {
    vi.restoreAllMocks();
    delete (globalThis as Record<string, unknown>).BluetoothDevice;
  });

  it('remembers the device after a manual connect', async () => {
    const device = new FakeDevice('device-abc', 'HellTecLite-LoRa-FADC');
    stubBluetooth({ requestDevice: device });

    const service = new BleService();
    await service.connect();

    expect(service.getState()).toBe(ConnectionState.CONNECTED);
    expect(service.getKnownDevice()).toEqual({ id: 'device-abc', name: 'HellTecLite-LoRa-FADC' });
  });

  it('offers devices matching either the service UUID or a board name prefix', async () => {
    const device = new FakeDevice('device-abc', 'HellTecLite-LoRa-FADC');
    const bluetooth = stubBluetooth({ requestDevice: device });

    const service = new BleService();
    await service.connect();

    const options = (bluetooth.requestDevice as ReturnType<typeof vi.fn>).mock.calls[0][0];
    const prefixes = options.filters
      .filter((f: { namePrefix?: string }) => f.namePrefix)
      .map((f: { namePrefix: string }) => f.namePrefix);

    expect(options.filters).toContainEqual({
      services: ['00001234-0000-1000-8000-00805f9b34fb']
    });
    // Every board name embeds "LoRa"; each variant must be reachable.
    expect(prefixes).toEqual(['HellTecLite-LoRa', 'WirelessStick-LoRa', 'nRF52-LoRa']);
    expect(prefixes.every((p: string) => p.includes('LoRa'))).toBe(true);
    // Name-matched devices still need access to the service.
    expect(options.optionalServices).toContain('00001234-0000-1000-8000-00805f9b34fb');
  });

  it('reconnects to the remembered device with no user interaction', async () => {
    const other = new FakeDevice('device-other');
    const target = new FakeDevice('device-abc');
    const bluetooth = stubBluetooth({ devices: [other, target] });
    localStorage.setItem(KNOWN_DEVICE_KEY, JSON.stringify({ id: 'device-abc', name: '' }));

    const service = new BleService();
    await service.tryAutoReconnect();

    expect(service.getState()).toBe(ConnectionState.CONNECTED);
    expect(target.gatt.connect).toHaveBeenCalled();
    expect(other.gatt.connect).not.toHaveBeenCalled();
    // The whole point: no chooser is shown.
    expect(bluetooth.requestDevice).not.toHaveBeenCalled();
  });

  it('falls back to the only permitted device when nothing is remembered yet', async () => {
    const device = new FakeDevice('device-abc');
    stubBluetooth({ devices: [device] });

    const service = new BleService();
    await service.tryAutoReconnect();

    expect(service.getState()).toBe(ConnectionState.CONNECTED);
  });

  it('does not adopt a stray device once a different one is remembered (two-board case)', async () => {
    // Simulates testing with a second board: both are permitted origin-wide,
    // but only the recorded one should ever be auto-reconnected to.
    const remembered = new FakeDevice('device-abc');
    const other = new FakeDevice('device-other');
    stubBluetooth({ devices: [remembered, other] });
    localStorage.setItem(
      KNOWN_DEVICE_KEY,
      JSON.stringify({ id: 'device-not-permitted', name: '' })
    );

    const service = new BleService();
    await service.tryAutoReconnect();

    expect(service.getState()).not.toBe(ConnectionState.CONNECTED);
    expect(remembered.gatt.connect).not.toHaveBeenCalled();
    expect(other.gatt.connect).not.toHaveBeenCalled();
  });

  it('waits for an advertisement when the device is asleep, then reconnects on wake', async () => {
    // Unreachable at first: this is what a deep-sleeping device looks like.
    const device = new FakeDevice('device-abc', 'ESP32S3-LoRa', false);
    stubBluetooth({ devices: [device] });

    const service = new BleService();
    await service.tryAutoReconnect();
    await flush();

    expect(device.watchAdvertisements).toHaveBeenCalled();
    expect(service.getState()).toBe(ConnectionState.WAITING_FOR_DEVICE);

    // The user presses the wake button; the device starts advertising again.
    device.gatt.connect = vi.fn().mockResolvedValue(makeServer());

    device.dispatchEvent(new Event('advertisementreceived'));
    await flush();

    expect(service.getState()).toBe(ConnectionState.CONNECTED);
  });

  it('does nothing when no device has been paired yet', async () => {
    const bluetooth = stubBluetooth({ devices: [] });

    const service = new BleService();
    await service.tryAutoReconnect();

    expect(service.getState()).toBe(ConnectionState.DISCONNECTED);
    expect(bluetooth.requestDevice).not.toHaveBeenCalled();
  });

  it('degrades gracefully when the browser lacks persistent permissions', async () => {
    const bluetooth = stubBluetooth({ persistent: false });

    const service = new BleService();
    await service.tryAutoReconnect();

    expect(service.getState()).toBe(ConnectionState.DISCONNECTED);
    expect(bluetooth.requestDevice).not.toHaveBeenCalled();
    expect(bluetooth.getDevices).toBeUndefined();
  });

  it('disconnect() keeps the remembered device but switches auto-reconnect off', async () => {
    const device = new FakeDevice('device-abc');
    stubBluetooth({ requestDevice: device, devices: [device] });

    const service = new BleService();
    await service.connect();
    await service.disconnect();

    expect(service.getState()).toBe(ConnectionState.DISCONNECTED);
    expect(service.getKnownDevice()).toEqual({ id: 'device-abc', name: 'ESP32S3-LoRa' });
    expect(service.isAutoReconnectEnabled()).toBe(false);

    // A subsequent auto-reconnect on a fresh instance must not silently
    // re-pair while the preference is off, even though the device is known.
    const fresh = new BleService();
    await fresh.tryAutoReconnect();
    await flush();
    expect(fresh.getState()).not.toBe(ConnectionState.CONNECTED);
  });

  it('switching auto-reconnect back on resumes without the chooser', async () => {
    const device = new FakeDevice('device-abc');
    const bluetooth = stubBluetooth({ requestDevice: device, devices: [device] });

    const service = new BleService();
    await service.connect();
    await service.disconnect();
    expect(service.isAutoReconnectEnabled()).toBe(false);

    (bluetooth.requestDevice as ReturnType<typeof vi.fn>).mockClear();
    device.gatt.connect = vi.fn().mockResolvedValue(makeServer());

    service.setAutoReconnectEnabled(true);
    await flush();

    expect(service.isAutoReconnectEnabled()).toBe(true);
    expect(service.getState()).toBe(ConnectionState.CONNECTED);
    expect(bluetooth.requestDevice).not.toHaveBeenCalled();
  });

  it('switching auto-reconnect off cancels an in-flight wait', async () => {
    const device = new FakeDevice('device-abc', 'ESP32S3-LoRa', false);
    stubBluetooth({ devices: [device] });

    const service = new BleService();
    await service.tryAutoReconnect();
    await flush();
    expect(service.getState()).toBe(ConnectionState.WAITING_FOR_DEVICE);

    service.setAutoReconnectEnabled(false);

    expect(service.getState()).toBe(ConnectionState.DISCONNECTED);

    // The device waking up afterwards must not reconnect.
    device.gatt.connect = vi.fn().mockResolvedValue(makeServer());
    device.dispatchEvent(new Event('advertisementreceived'));
    await flush();
    expect(service.getState()).toBe(ConnectionState.DISCONNECTED);
  });

  it('forgetDevice() clears the pairing and stops reconnecting', async () => {
    const device = new FakeDevice('device-abc');
    stubBluetooth({ requestDevice: device, devices: [device] });

    const service = new BleService();
    await service.connect();

    service.forgetDevice();

    expect(service.getState()).toBe(ConnectionState.DISCONNECTED);
    expect(service.getKnownDevice()).toBeNull();

    const fresh = new BleService();
    await fresh.tryAutoReconnect();
    await flush();
    expect(fresh.getState()).not.toBe(ConnectionState.CONNECTED);
  });

  it('migrates a legacy remembered device id on construction', async () => {
    localStorage.setItem(LEGACY_LAST_DEVICE_ID_KEY, 'device-abc');

    const service = new BleService();

    expect(service.getKnownDevice()).toEqual({ id: 'device-abc', name: '' });
    expect(localStorage.getItem(LEGACY_LAST_DEVICE_ID_KEY)).toBeNull();
    expect(service.isAutoReconnectEnabled()).toBe(true);
  });

  it('migrates the legacy opt-out sentinel into the auto-reconnect preference', async () => {
    localStorage.setItem(LEGACY_LAST_DEVICE_ID_KEY, 'none');

    const service = new BleService();

    expect(service.getKnownDevice()).toBeNull();
    expect(localStorage.getItem(LEGACY_LAST_DEVICE_ID_KEY)).toBeNull();
    expect(service.isAutoReconnectEnabled()).toBe(false);
  });

  it('polls to reconnect when watchAdvertisements is unavailable', async () => {
    // Today's Chrome for Android: neither flag enabled. gatt.connect() still
    // works on a handle we already hold, so a wake must still be picked up.
    vi.useFakeTimers();
    try {
      const device = new FakeDevice('device-abc');
      stubBluetooth({ requestDevice: device });
      delete (globalThis as Record<string, unknown>).BluetoothDevice;

      const service = new BleService();
      await service.connect();
      expect(service.getState()).toBe(ConnectionState.CONNECTED);

      // Device sleeps and drops the link, and is unreachable while asleep.
      device.gatt.connect = vi.fn().mockRejectedValue(new Error('unreachable'));
      device.dispatchEvent(new Event('gattserverdisconnected'));
      await vi.advanceTimersByTimeAsync(0);

      expect(service.getState()).toBe(ConnectionState.WAITING_FOR_DEVICE);

      // A couple of failed attempts while it is still asleep.
      await vi.advanceTimersByTimeAsync(20000);
      expect(device.gatt.connect).toHaveBeenCalled();
      expect(service.getState()).toBe(ConnectionState.WAITING_FOR_DEVICE);

      // User presses the wake button.
      device.gatt.connect = vi.fn().mockResolvedValue(makeServer());
      await vi.advanceTimersByTimeAsync(15000);

      expect(service.getState()).toBe(ConnectionState.CONNECTED);
    } finally {
      vi.useRealTimers();
    }
  });

  it('stops polling when the user disconnects on purpose', async () => {
    vi.useFakeTimers();
    try {
      const device = new FakeDevice('device-abc');
      stubBluetooth({ requestDevice: device });
      delete (globalThis as Record<string, unknown>).BluetoothDevice;

      const service = new BleService();
      await service.connect();

      device.gatt.connect = vi.fn().mockRejectedValue(new Error('unreachable'));
      device.dispatchEvent(new Event('gattserverdisconnected'));
      await vi.advanceTimersByTimeAsync(0);
      expect(service.getState()).toBe(ConnectionState.WAITING_FOR_DEVICE);

      await service.disconnect();
      const callsAtDisconnect = (device.gatt.connect as ReturnType<typeof vi.fn>).mock.calls.length;

      // Polling must not resume behind the user's back.
      await vi.advanceTimersByTimeAsync(60_000);
      expect((device.gatt.connect as ReturnType<typeof vi.fn>).mock.calls.length).toBe(
        callsAtDisconnect
      );
      expect(service.getState()).toBe(ConnectionState.DISCONNECTED);
    } finally {
      vi.useRealTimers();
    }
  });

  it('gives up after the attempt cap and reports an error', async () => {
    vi.useFakeTimers();
    try {
      const device = new FakeDevice('device-abc');
      stubBluetooth({ requestDevice: device });
      delete (globalThis as Record<string, unknown>).BluetoothDevice;

      const service = new BleService();
      const onError = vi.fn();
      service.onError(onError);

      await service.connect();
      expect(service.getState()).toBe(ConnectionState.CONNECTED);

      // Device sleeps and never comes back within the attempt cap.
      device.gatt.connect = vi.fn().mockRejectedValue(new Error('unreachable'));
      device.dispatchEvent(new Event('gattserverdisconnected'));
      await vi.advanceTimersByTimeAsync(0);
      expect(service.getState()).toBe(ConnectionState.WAITING_FOR_DEVICE);

      // Exhaust every attempt; backoff is capped at 30s per attempt.
      await vi.advanceTimersByTimeAsync(20 * 30_000);

      expect(service.getState()).toBe(ConnectionState.DISCONNECTED);
      expect(onError).toHaveBeenCalled();
    } finally {
      vi.useRealTimers();
    }
  });

  it('times out a hung gatt.connect() instead of hanging forever', async () => {
    vi.useFakeTimers();
    try {
      const device = new FakeDevice('device-abc');
      // Never resolves or rejects - simulates a stuck Android BLE stack.
      device.gatt.connect = vi.fn(() => new Promise(() => {}));
      stubBluetooth({ requestDevice: device });

      const service = new BleService();
      // Attach the rejection handler immediately: it settles inside the
      // upcoming advanceTimersByTimeAsync call, before this line could
      // otherwise run again to catch it.
      const connectPromise = service.connect().catch((error: unknown) => error);

      await vi.advanceTimersByTimeAsync(0);
      expect(service.getState()).toBe(ConnectionState.CONNECTING);

      await vi.advanceTimersByTimeAsync(15_000);
      await connectPromise;

      expect(service.getState()).not.toBe(ConnectionState.CONNECTING);
    } finally {
      vi.useRealTimers();
    }
  });

  it('falls back to polling when watchAdvertisements fails to arm', async () => {
    vi.useFakeTimers();
    try {
      const device = new FakeDevice('device-abc');
      stubBluetooth({ requestDevice: device });
      device.watchAdvertisements = vi.fn().mockRejectedValue(new Error('not allowed'));

      const service = new BleService();
      await service.connect();

      device.gatt.connect = vi.fn().mockRejectedValue(new Error('unreachable'));
      device.dispatchEvent(new Event('gattserverdisconnected'));
      await vi.advanceTimersByTimeAsync(0);

      expect(device.watchAdvertisements).toHaveBeenCalled();
      expect(service.getState()).toBe(ConnectionState.WAITING_FOR_DEVICE);

      device.gatt.connect = vi.fn().mockResolvedValue(makeServer());
      await vi.advanceTimersByTimeAsync(5000);

      expect(service.getState()).toBe(ConnectionState.CONNECTED);
    } finally {
      vi.useRealTimers();
    }
  });

  it('watches for the device to return even when getDevices() is unavailable', async () => {
    // Chrome ships getDevices() and watchAdvertisements() behind different
    // flags, so a device already paired in this session must still be watched.
    const device = new FakeDevice('device-abc');
    stubBluetooth({ requestDevice: device });
    delete (navigator.bluetooth as unknown as Record<string, unknown>).getDevices;

    const service = new BleService();
    await service.connect();

    device.dispatchEvent(new Event('gattserverdisconnected'));
    await flush();

    expect(device.watchAdvertisements).toHaveBeenCalled();
    expect(service.getState()).toBe(ConnectionState.WAITING_FOR_DEVICE);
  });

  it('watches for the device to return after an unexpected disconnect', async () => {
    const device = new FakeDevice('device-abc');
    stubBluetooth({ requestDevice: device });

    const service = new BleService();
    await service.connect();

    // Firmware went to sleep and dropped the link.
    device.dispatchEvent(new Event('gattserverdisconnected'));
    await flush();

    expect(device.watchAdvertisements).toHaveBeenCalled();
    expect(service.getState()).toBe(ConnectionState.WAITING_FOR_DEVICE);
  });

  it('retryNow() resets the backoff and retries immediately', async () => {
    vi.useFakeTimers();
    try {
      const device = new FakeDevice('device-abc');
      stubBluetooth({ requestDevice: device });
      delete (globalThis as Record<string, unknown>).BluetoothDevice;

      const service = new BleService();
      await service.connect();

      device.gatt.connect = vi.fn().mockRejectedValue(new Error('unreachable'));
      device.dispatchEvent(new Event('gattserverdisconnected'));
      await vi.advanceTimersByTimeAsync(0);
      expect(service.getState()).toBe(ConnectionState.WAITING_FOR_DEVICE);

      device.gatt.connect = vi.fn().mockResolvedValue(makeServer());
      service.retryNow();
      await vi.advanceTimersByTimeAsync(0);

      expect(service.getState()).toBe(ConnectionState.CONNECTED);
    } finally {
      vi.useRealTimers();
    }
  });
});
