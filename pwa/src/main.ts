/**
 * Main entry point
 */

import './components/lora-app';

// Register service worker for PWA offline support
if ('serviceWorker' in navigator) {
  window.addEventListener('load', () => {
    navigator.serviceWorker
      .register('/sw.js')
      .then(registration => {
        console.log('Service Worker registered:', registration);
      })
      .catch(error => {
        console.error('Service Worker registration failed:', error);
      });
  });
}

// Log app info
console.log('LoRa Bridge PWA v1.0.0');
console.log('Web Bluetooth support:', !!navigator.bluetooth);
console.log('Geolocation support:', !!navigator.geolocation);
