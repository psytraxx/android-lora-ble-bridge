/**
 * Main entry point
 */

import './style.css';
import './components/lora-app';
import { registerSW } from 'virtual:pwa-register';
import { version } from '../package.json';

// Register service worker using vite-plugin-pwa
// This handles the base path automatically
registerSW({
  onNeedRefresh() {
    console.log('New version available');
  },
  onOfflineReady() {
    console.log('App ready to work offline');
  }
});

// Log app info
console.log(`LoRa Chat PWA v${version}`);
console.log('Web Bluetooth support:', !!navigator.bluetooth);
console.log('Geolocation support:', !!navigator.geolocation);
