/**
 * Location Service using Web Geolocation API
 *
 * Provides GPS coordinates for message sending
 */

export interface LocationData {
  latitude: number;
  longitude: number;
  accuracy: number;
  timestamp: number;
}

type LocationListener = (location: LocationData | null) => void;

/**
 * Location Service for GPS coordinates
 */
export class LocationService {
  private cachedLocation: LocationData | null = null;
  private cacheTimeout = 60_000; // 1 minute cache
  private listeners = new Set<LocationListener>();

  constructor() {
    // Check Geolocation support
    if (!navigator.geolocation) {
      console.warn('Geolocation API not supported');
    }
  }

  /**
   * Check if Geolocation is supported
   */
  isSupported(): boolean {
    return !!navigator.geolocation;
  }

  /**
   * Get current location (with caching)
   */
  async getCurrentLocation(): Promise<LocationData | null> {
    if (!this.isSupported()) {
      console.warn('Geolocation not supported');
      return null;
    }

    // Return cached location if fresh
    if (this.cachedLocation && Date.now() - this.cachedLocation.timestamp < this.cacheTimeout) {
      console.log('Using cached location');
      return this.cachedLocation;
    }

    try {
      const position = await this.requestPosition();
      const location: LocationData = {
        latitude: position.coords.latitude,
        longitude: position.coords.longitude,
        accuracy: position.coords.accuracy,
        timestamp: Date.now()
      };

      this.cachedLocation = location;
      this.notifyListeners(location);

      console.log('Location acquired:', location);
      return location;
    } catch (error) {
      console.error('Failed to get location:', error);
      this.notifyListeners(null);
      return null;
    }
  }

  /**
   * Get cached location without requesting new one
   */
  getCachedLocation(): LocationData | null {
    if (this.cachedLocation && Date.now() - this.cachedLocation.timestamp < this.cacheTimeout) {
      return this.cachedLocation;
    }
    return null;
  }

  /**
   * Request location permission and start watching (optional)
   */
  async requestPermission(): Promise<boolean> {
    if (!this.isSupported()) {
      return false;
    }

    try {
      await this.requestPosition();
      return true;
    } catch (error) {
      console.error('Location permission denied:', error);
      return false;
    }
  }

  /**
   * Clear cached location
   */
  clearCache(): void {
    this.cachedLocation = null;
    this.notifyListeners(null);
  }

  /**
   * Subscribe to location updates
   */
  onLocationChange(listener: LocationListener): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  /**
   * Private: Request position using Geolocation API
   */
  private requestPosition(): Promise<GeolocationPosition> {
    return new Promise((resolve, reject) => {
      navigator.geolocation.getCurrentPosition(resolve, reject, {
        enableHighAccuracy: true,
        timeout: 10_000,
        maximumAge: 0
      });
    });
  }

  /**
   * Private: Notify listeners of location change
   */
  private notifyListeners(location: LocationData | null): void {
    this.listeners.forEach((listener) => {
      listener(location);
    });
  }
}

// Singleton instance
export const locationService = new LocationService();
