/**
 * Formatting utilities
 */

/**
 * Format timestamp to HH:mm
 */
export function formatTime(timestamp: number): string {
  const date = new Date(timestamp);
  const hours = date.getHours().toString().padStart(2, '0');
  const minutes = date.getMinutes().toString().padStart(2, '0');
  return `${hours}:${minutes}`;
}

/**
 * Format GPS coordinates for Google Maps URL
 */
export function formatMapsUrl(latitude: number, longitude: number): string {
  return `https://www.google.com/maps?q=${latitude},${longitude}`;
}

/**
 * Format byte size
 */
export function formatBytes(bytes: number): string {
  return `${bytes} B`;
}
