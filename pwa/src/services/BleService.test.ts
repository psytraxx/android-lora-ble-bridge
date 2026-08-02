import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { BleService, ConnectionState } from './BleService';

const LAST_DEVICE_ID_KEY = 'lora.lastDeviceId';

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
      : vi.fn().mockRejectedValue(new Error('User cancelled'))
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

  it('remembers the device id after a manual connect', async () => {
    const device = new FakeDevice('device-abc');
    stubBluetooth({ requestDevice: device });

    const service = new BleService();
    await service.connect();

    expect(service.getState()).toBe(ConnectionState.CONNECTED);
    expect(localStorage.getItem(LAST_DEVICE_ID_KEY)).toBe('device-abc');
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
    localStorage.setItem(LAST_DEVICE_ID_KEY, 'device-abc');

    const service = new BleService();
    await service.tryAutoReconnect();

    expect(service.getState()).toBe(ConnectionState.CONNECTED);
    expect(target.gatt.connect).toHaveBeenCalled();
    expect(other.gatt.connect).not.toHaveBeenCalled();
    // The whole point: no chooser is shown.
    expect(bluetooth.requestDevice).not.toHaveBeenCalled();
  });

  it('falls back to the only permitted device when no id is stored', async () => {
    const device = new FakeDevice('device-abc');
    stubBluetooth({ devices: [device] });

    const service = new BleService();
    await service.tryAutoReconnect();

    expect(service.getState()).toBe(ConnectionState.CONNECTED);
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

  it('forgets the device on explicit disconnect so it does not reconnect', async () => {
    const device = new FakeDevice('device-abc');
    stubBluetooth({ requestDevice: device, devices: [device] });

    const service = new BleService();
    await service.connect();
    await service.disconnect();

    expect(service.getState()).toBe(ConnectionState.DISCONNECTED);
    expect(localStorage.getItem(LAST_DEVICE_ID_KEY)).not.toBe('device-abc');

    // A subsequent auto-reconnect must not silently re-pair, even though the
    // browser still lists the device as permitted.
    const fresh = new BleService();
    await fresh.tryAutoReconnect();
    await flush();
    expect(fresh.getState()).not.toBe(ConnectionState.CONNECTED);
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
      await vi.advanceTimersByTimeAsync(7000);
      expect(device.gatt.connect).toHaveBeenCalled();
      expect(service.getState()).toBe(ConnectionState.WAITING_FOR_DEVICE);

      // User presses the wake button.
      device.gatt.connect = vi.fn().mockResolvedValue(makeServer());
      await vi.advanceTimersByTimeAsync(3500);

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
      await vi.advanceTimersByTimeAsync(30_000);
      expect((device.gatt.connect as ReturnType<typeof vi.fn>).mock.calls.length).toBe(
        callsAtDisconnect
      );
      expect(service.getState()).toBe(ConnectionState.DISCONNECTED);
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
});
