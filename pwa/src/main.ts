/**
 * Main entry point
 */

import './style.css';
import './components/lora-app';
import { registerSW } from 'virtual:pwa-register';

// Register service worker using vite-plugin-pwa
// This handles the base path automatically
registerSW({
  onNeedRefresh() {
    console.log('New version available');
  },
  onOfflineReady() {
    console.log('App ready to work offline');
  },
});

// Log app info
console.log('LoRa Bridge PWA v1.0.0');
console.log('Web Bluetooth support:', !!navigator.bluetooth);
console.log('Geolocation support:', !!navigator.geolocation);
