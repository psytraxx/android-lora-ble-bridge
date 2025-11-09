/**
 * Main App Web Component
 */

import { LitElement, html, css } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { bleService, ConnectionState } from '../services/BleService';
import { locationService } from '../services/LocationService';
import { messageRepository, AckStatus, type ChatMessage } from '../services/MessageRepository';
import { MESSAGE_TYPE, type TextMessage, type AckMessage } from '../protocol';
import './connection-status';
import './message-list';
import './message-input';

@customElement('lora-app')
export class LoraApp extends LitElement {
  @state() private connectionState: ConnectionState = ConnectionState.DISCONNECTED;
  @state() private messages: ChatMessage[] = [];
  @state() private hasGps = false;

  private ackTimeouts = new Map<number, number>();
  private unsubscribers: (() => void)[] = [];

  static styles = css`
    :host {
      display: flex;
      flex-direction: column;
      height: 100vh;
      width: 100%;
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
      background: var(--background-color, #fafafa);
      --primary-color: #1976d2;
      --primary-dark-color: #1565c0;
      --surface-color: #ffffff;
      --on-surface-color: #212121;
      --text-secondary-color: #757575;
      --divider-color: #e0e0e0;
    }

    .content {
      flex: 1;
      display: flex;
      flex-direction: column;
      overflow: hidden;
    }

    message-list {
      flex: 1;
    }

    .toast {
      position: fixed;
      bottom: 80px;
      left: 50%;
      transform: translateX(-50%);
      background: #323232;
      color: white;
      padding: 12px 24px;
      border-radius: 24px;
      font-size: 14px;
      box-shadow: 0 4px 8px rgba(0, 0, 0, 0.3);
      animation: slideUp 0.3s ease-out;
      z-index: 1000;
    }

    @keyframes slideUp {
      from {
        transform: translateX(-50%) translateY(20px);
        opacity: 0;
      }
      to {
        transform: translateX(-50%) translateY(0);
        opacity: 1;
      }
    }

    .error-banner {
      background: #d32f2f;
      color: white;
      padding: 12px 16px;
      text-align: center;
      font-size: 14px;
    }
  `;

  connectedCallback() {
    super.connectedCallback();

    // Subscribe to BLE state changes
    this.unsubscribers.push(
      bleService.onStateChange((state) => {
        this.connectionState = state;
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
        this.showToast(`Error: ${error.message}`);
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

    // Check if Web Bluetooth is supported
    if (!bleService.isSupported()) {
      this.showToast('Web Bluetooth is not supported in this browser. Please use Chrome on desktop or Android.');
    }
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    this.unsubscribers.forEach(unsub => unsub());
    this.unsubscribers = [];

    // Clear all ACK timeouts
    this.ackTimeouts.forEach(timeout => window.clearTimeout(timeout));
    this.ackTimeouts.clear();
  }

  render() {
    const isConnected = this.connectionState === ConnectionState.CONNECTED;
    const showBleWarning = !bleService.isSupported();

    return html`
      <connection-status
        .state=${this.connectionState}
        @connect=${this.onConnect}
        @disconnect=${this.onDisconnect}
      ></connection-status>

      ${showBleWarning ? html`
        <div class="error-banner">
          Web Bluetooth is not supported. Use Chrome on desktop or Android.
        </div>
      ` : ''}

      <div class="content">
        <message-list .messages=${this.messages}></message-list>
        <message-input
          ?disabled=${!isConnected}
          .hasGps=${this.hasGps}
          @send=${this.onSendMessage}
        ></message-input>
      </div>
    `;
  }

  private async onConnect() {
    try {
      await bleService.connect();
      this.showToast('Connected successfully!');

      // Try to get initial GPS location
      await locationService.getCurrentLocation();
    } catch (error) {
      console.error('Connection failed:', error);
      // Error already shown via onError handler
    }
  }

  private async onDisconnect() {
    await bleService.disconnect();
    this.showToast('Disconnected');
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
      this.showToast(`Failed to send: ${(error as Error).message}`);
    }
  }

  private handleReceivedMessage(message: TextMessage | AckMessage) {
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

  private showToast(message: string) {
    const toast = document.createElement('div');
    toast.className = 'toast';
    toast.textContent = message;
    this.shadowRoot?.appendChild(toast);

    setTimeout(() => {
      toast.remove();
    }, 3000);
  }
}
