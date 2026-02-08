/**
 * Main App Web Component
 * Uses DaisyUI components
 */

import { html, LitElement } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { MESSAGE_TYPE, type Message, PROTOCOL, type TextMessage } from '../protocol';
import type { DeviceInfo } from '../protocol/types';
import { bleService, ConnectionState } from '../services/BleService';
import { locationService } from '../services/LocationService';
import { AckStatus, type ChatMessage, messageRepository } from '../services/MessageRepository';
import { toastService } from '../services/ToastService';
import './connection-status';
import './message-list';
import './message-input';
import './empty-state';
import './pairing-modal';
import './success-toast';
import './device-info-modal';
import { sharedStylesheet } from '../shared-styles';
import { exclamationTriangleIcon } from '../utils/icons';

@customElement('lora-app')
export class LoraApp extends LitElement {
  @state() private connectionState: ConnectionState = ConnectionState.DISCONNECTED;
  @state() private messages: ChatMessage[] = [];
  @state() private hasGps = false;
  @state() private deviceName: string | null = null;
  @state() private showPairingModal = false;
  @state() private showSuccessToast = false;
  @state() private showInfoModal = false;
  @state() private deviceInfo: DeviceInfo | null = null;
  @state() private deviceInfoLoading = false;

  private ackTimeouts = new Map<number, number>();
  private unsubscribers: (() => void)[] = [];

  static styles = [sharedStylesheet];

  connectedCallback() {
    super.connectedCallback();

    // Subscribe to BLE state changes
    this.unsubscribers.push(
      bleService.onStateChange((state) => {
        this.connectionState = state;
        // update device info when state changes
        const d = bleService.getDevice();
        this.deviceName = d?.name ?? null;
      })
    );

    // Subscribe to BLE messages
    this.unsubscribers.push(
      bleService.onMessage((message) => {
        this.handleReceivedMessage(message);
      })
    );

    // Subscribe to BLE errors
    this.unsubscribers.push(
      bleService.onError((error) => {
        const friendlyError = this.getFriendlyErrorMessage(error);
        toastService.show(friendlyError, 'error', 5000);
      })
    );

    // Subscribe to message repository updates
    this.unsubscribers.push(
      messageRepository.onMessagesChange((messages) => {
        this.messages = messages;
      })
    );

    // Subscribe to location updates
    this.unsubscribers.push(
      locationService.onLocationChange((location) => {
        this.hasGps = location !== null;
      })
    );

    // Initial state
    this.connectionState = bleService.getState();
    this.messages = messageRepository.getMessages();
    const dev = bleService.getDevice();
    this.deviceName = dev?.name ?? null;

    // Check if Web Bluetooth is supported
    if (!bleService.isSupported()) {
      toastService.show(
        'Web Bluetooth is not supported in this browser. Please use Chrome on desktop or Android.',
        'warning',
        5000
      );
    }
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.unsubscribers.forEach((unsub) => {
      unsub();
    });
    this.unsubscribers = [];

    // Clear all ACK timeouts
    this.ackTimeouts.forEach((timeout) => {
      window.clearTimeout(timeout);
    });
    this.ackTimeouts.clear();
  }

  render() {
    const isConnected = this.connectionState === ConnectionState.CONNECTED;
    const isConnecting =
      this.connectionState === ConnectionState.SCANNING ||
      this.connectionState === ConnectionState.CONNECTING ||
      this.connectionState === ConnectionState.DISCOVERING ||
      this.connectionState === ConnectionState.ENABLING_NOTIFICATIONS;
    const showBleWarning = !bleService.isSupported();
    const showEmptyState = !isConnected && this.messages.length === 0;

    return html`
      <div class="flex flex-col h-screen">
        <!-- Fixed Header -->
        <header class="fixed top-0 left-0 right-0 z-10">
            <connection-status
              .state=${this.connectionState}
              .deviceName=${this.deviceName}
              @connect=${this.onConnectDirect}
              @disconnect=${this.onDisconnect}
              @info-request=${this.onInfoRequest}
            ></connection-status>
            ${isConnecting
        ? html`<progress class="progress progress-primary w-full h-1"></progress>`
        : ''
      }
            ${showBleWarning
        ? html`
                  <div role="alert" class="alert alert-warning rounded-none">
                    ${exclamationTriangleIcon('shrink-0 w-6 h-6')}
                    <span>Web Bluetooth not supported. Use Chrome on desktop or Android.</span>
                  </div>
                `
        : ''
      }
        </header>

        <!-- Main content area -->
        <main class="flex-1 mt-20 mb-20 overflow-y-auto">
          ${showEmptyState
        ? html`<empty-state
                class="h-full"
                @connect-requested=${this.onConnectRequest}
              ></empty-state>`
        : html`<message-list class="h-full" .messages=${this.messages}></message-list>`
      }
        </main>

        <!-- Fixed Footer -->
        <footer class="fixed bottom-0 left-0 right-0 z-10">
          <message-input
            ?disabled=${!isConnected}
            .hasGps=${this.hasGps}
            @send=${this.onSendMessage}
          ></message-input>
        </footer>

        <!-- Pairing Modal -->
        <pairing-modal
          .open=${this.showPairingModal}
          @modal-closed=${this.onModalClosed}
          @proceed-to-pair=${this.onProceedToPair}
        ></pairing-modal>

        <!-- Device Info Modal -->
        <device-info-modal
          .open=${this.showInfoModal}
          .info=${this.deviceInfo}
          .loading=${this.deviceInfoLoading}
          @modal-closed=${this.onInfoModalClosed}
        ></device-info-modal>

        <!-- Success Toast -->
        <success-toast
          .show=${this.showSuccessToast}
          @hide=${() => {
        this.showSuccessToast = false;
      }}
        ></success-toast>
      </div>
    `;
  }

  private onConnectRequest() {
    // Show pairing instructions modal first (from empty state button)
    this.showPairingModal = true;
  }

  private async onConnectDirect() {
    // Direct connect without modal (from navbar button)
    await this.onConnect();
  }

  private onModalClosed() {
    this.showPairingModal = false;
  }

  private async onProceedToPair() {
    this.showPairingModal = false;
    await this.onConnect();
  }

  private async onInfoRequest() {
    this.showInfoModal = true;
    this.deviceInfoLoading = true;
    this.deviceInfo = null;

    try {
      this.deviceInfo = await bleService.requestDeviceInfo();
    } catch (error) {
      console.error('Failed to request device info:', error);
      toastService.show('Failed to read device info', 'error');
    } finally {
      this.deviceInfoLoading = false;
    }
  }

  private onInfoModalClosed() {
    this.showInfoModal = false;
  }

  private getFriendlyErrorMessage(error: Error): string {
    const message = error.message.toLowerCase();

    // User cancelled the pairing dialog
    if (message.includes('user cancel') || message.includes('cancelled')) {
      return 'Pairing cancelled. Click "Connect Device" to try again.';
    }

    // Device not found or not in range
    if (message.includes('no device') || message.includes('not found')) {
      return 'Device not found. Make sure your ESP32 is powered on and nearby, then try again.';
    }

    // Connection timeout
    if (message.includes('timeout') || message.includes('timed out')) {
      return 'Connection timed out. Move closer to your device and try again.';
    }

    // Device already connected elsewhere
    if (message.includes('in use') || message.includes('already connected')) {
      return 'Device is already connected. Disconnect from other apps first.';
    }

    // Bluetooth not available
    if (message.includes('not supported') || message.includes('not available')) {
      return 'Bluetooth not available. Please use Chrome on desktop or Android.';
    }

    // GATT errors
    if (message.includes('gatt')) {
      return 'Connection lost. Make sure your device is nearby and try reconnecting.';
    }

    // Generic fallback with retry hint
    return `Connection failed: ${error.message}. Please try again.`;
  }

  private async onConnect() {
    try {
      await bleService.connect();

      // Show success celebration
      this.showSuccessToast = true;

      // Update device info for UI
      const dev = bleService.getDevice();
      this.deviceName = dev?.name ?? null;

      // Try to get initial GPS location
      await locationService.getCurrentLocation();
    } catch (error) {
      console.error('Connection failed:', error);
      // Error already shown via onError handler
    }
  }

  private async onDisconnect() {
    await bleService.disconnect();
    toastService.show('Disconnected', 'info');
    // Clear device info
    this.deviceName = null;
  }

  private async onSendMessage(e: CustomEvent) {
    const { text } = e.detail;

    try {
      // Get current location
      const location = await locationService.getCurrentLocation();

      // Create TextMessage
      const seq = messageRepository.getNextSeq();
      const textMessage: TextMessage = {
        type: MESSAGE_TYPE.TEXT,
        seq,
        text: text.toUpperCase(), // Protocol uses uppercase
        hasGps: location !== null,
        latitude: location?.latitude,
        longitude: location?.longitude,
        senderTime: Math.floor(Date.now() / 1000)
      };

      // Send via BLE
      await bleService.sendMessage(textMessage);

      // Add to repository
      messageRepository.addSentMessage(
        text,
        textMessage.hasGps,
        textMessage.latitude,
        textMessage.longitude
      );

      // Set ACK timeout
      this.setAckTimeout(seq);
    } catch (error) {
      console.error('Failed to send message:', error);
      toastService.show(`Failed to send: ${(error as Error).message}`, 'error');
    }
  }

  private handleReceivedMessage(message: Message) {
    if (message.type === MESSAGE_TYPE.TEXT) {
      // Received a text message
      console.log('Received text message:', message);

      messageRepository.addReceivedMessage(
        message.text,
        message.seq,
        message.hasGps,
        message.latitude,
        message.longitude
      );
    } else if (message.type === MESSAGE_TYPE.ACK) {
      // Received an ACK
      console.log('Received ACK for seq:', message.seq);

      // Clear timeout
      const timeout = this.ackTimeouts.get(message.seq);
      if (timeout !== undefined) {
        window.clearTimeout(timeout);
        this.ackTimeouts.delete(message.seq);
      }

      // Update message status
      messageRepository.updateAckStatus(message.seq, AckStatus.DELIVERED);
    }
  }

  private setAckTimeout(seq: number) {
    // Clear any existing timeout for this sequence
    const existing = this.ackTimeouts.get(seq);
    if (existing !== undefined) {
      window.clearTimeout(existing);
    }

    // Set new timeout (5 seconds)
    const timeout = window.setTimeout(() => {
      console.log('ACK timeout for seq:', seq);
      this.ackTimeouts.delete(seq);
      // Mark message as failed
      messageRepository.updateAckStatus(seq, AckStatus.FAILED);
    }, PROTOCOL.ACK_TIMEOUT_MS);

    this.ackTimeouts.set(seq, timeout);
  }
}
