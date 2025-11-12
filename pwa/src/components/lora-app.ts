/**
 * Main App Web Component
 * Uses DaisyUI components
 */

import { html, LitElement } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { type AckMessage, MESSAGE_TYPE, type Message, type TextMessage } from '../protocol';
import { bleService, ConnectionState } from '../services/BleService';
import { locationService } from '../services/LocationService';
import { AckStatus, type ChatMessage, messageRepository } from '../services/MessageRepository';
import { toastService } from '../services/ToastService';
import './connection-status';
import './message-list';
import './message-input';
import { sharedStylesheet } from '../shared-styles';

@customElement('lora-app')
export class LoraApp extends LitElement {
  @state() private connectionState: ConnectionState = ConnectionState.DISCONNECTED;
  @state() private messages: ChatMessage[] = [];
  @state() private hasGps = false;
  @state() private deviceName: string | null = null;

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
        toastService.show(`Error: ${error.message}`, 'error');
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
      this.connectionState === ConnectionState.CONNECTING;
    const showBleWarning = !bleService.isSupported();

    return html`
      <div class="flex flex-col h-screen">
        <!-- Fixed Header -->
        <header class="fixed top-0 left-0 right-0 z-10">
            <connection-status
              .state=${this.connectionState}
              .deviceName=${this.deviceName}
              @connect=${this.onConnect}
              @disconnect=${this.onDisconnect}
            ></connection-status>
            ${
              isConnecting
                ? html`<progress class="progress progress-primary w-full h-1"></progress>`
                : ''
            }
            ${
              showBleWarning
                ? html`
                  <div role="alert" class="alert alert-warning rounded-none">
                    <svg xmlns="http://www.w3.org/2000/svg" class="stroke-current shrink-0 h-6 w-6" fill="none" viewBox="0 0 24 24">
                      <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
                    </svg>
                    <span>Web Bluetooth not supported. Use Chrome on desktop or Android.</span>
                  </div>
                `
                : ''
            }
        </header>

        <!-- Main content area -->
        <main class="flex-1 mt-20 mb-20 overflow-y-auto">
          <message-list class="h-full" .messages=${this.messages}></message-list>
        </main>

        <!-- Fixed Footer -->
        <footer class="fixed bottom-0 left-0 right-0 z-10">
          <message-input
            ?disabled=${!isConnected}
            .hasGps=${this.hasGps}
            @send=${this.onSendMessage}
          ></message-input>
        </footer>
      </div>
    `;
  }

  private async onConnect() {
    try {
      await bleService.connect();
      toastService.show('Connected successfully!', 'success');

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
        longitude: location?.longitude
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

      // Send ACK after delay (mimics ESP32 behavior)
      setTimeout(() => {
        this.sendAck(message.seq);
      }, 500);
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

  private async sendAck(seq: number) {
    try {
      const ackMessage: AckMessage = {
        type: MESSAGE_TYPE.ACK,
        seq
      };

      await bleService.sendMessage(ackMessage);
      console.log('Sent ACK for seq:', seq);
    } catch (error) {
      console.error('Failed to send ACK:', error);
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
      // Message remains in PENDING state (could add FAILED state if desired)
    }, 5000);

    this.ackTimeouts.set(seq, timeout);
  }
}
