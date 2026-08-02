/**
 * Vitest setup.
 *
 * The jsdom environment used here does not provide localStorage, so supply a
 * minimal in-memory implementation. Real browsers always have one.
 *
 * Node also exposes a built-in localStorage that is inert unless started with
 * --localstorage-file, and whether it exists at all varies by version: absent
 * on Node 26, present but unusable on Node 25 (which CI runs). So feature-test
 * an actual method instead of the binding, and always install over a stub.
 */

const existing = globalThis.localStorage as Storage | undefined;

if (typeof existing?.clear !== 'function' || typeof existing?.getItem !== 'function') {
  const store = new Map<string, string>();

  const memoryStorage: Storage = {
    get length() {
      return store.size;
    },
    key: (index: number) => [...store.keys()][index] ?? null,
    getItem: (key: string) => store.get(key) ?? null,
    setItem: (key: string, value: string) => {
      store.set(key, String(value));
    },
    removeItem: (key: string) => {
      store.delete(key);
    },
    clear: () => {
      store.clear();
    }
  };

  Object.defineProperty(globalThis, 'localStorage', {
    value: memoryStorage,
    configurable: true,
    writable: true
  });
}
