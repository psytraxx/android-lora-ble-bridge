/**
 * Connection Status Web Component
 */

import { LitElement, html, css } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import { ConnectionState } from '../services/BleService';

@customElement('connection-status')
export class ConnectionStatus extends LitElement {
  @property({ type: String }) state: ConnectionState = ConnectionState.DISCONNECTED;

  static styles = css`
    :host {
      display: block;
    }

    .status-bar {
      background: var(--primary-color, #1976d2);
      color: white;
      padding: 16px;
      display: flex;
      justify-content: space-between;
      align-items: center;
      box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
    }

    .title {
      font-size: 20px;
      font-weight: 500;
      margin: 0;
    }

    .status {
      font-size: 14px;
      display: flex;
      align-items: center;
      gap: 8px;
    }

    .status-icon {
      font-size: 18px;
    }

    button {
      padding: 8px 16px;
      background: rgba(255, 255, 255, 0.2);
      color: white;
      border: 1px solid rgba(255, 255, 255, 0.5);
      border-radius: 16px;
      font-size: 14px;
      font-weight: 500;
      cursor: pointer;
      transition: background 0.2s;
    }

    button:hover {
      background: rgba(255, 255, 255, 0.3);
    }

    button:disabled {
      opacity: 0.5;
      cursor: not-allowed;
    }
  `;

  render() {
    const { icon, text } = this.getStatusDisplay();
    const showConnectButton = this.state === ConnectionState.DISCONNECTED || this.state === ConnectionState.ERROR;
    const showDisconnectButton = this.state === ConnectionState.CONNECTED;

    return html`
      <div class="status-bar">
        <h1 class="title">LoRa Chat</h1>
        <div class="status">
          <span class="status-icon">${icon}</span>
          <span>${text}</span>
          ${showConnectButton ? html`
            <button @click=${this.onConnect}>Connect</button>
          ` : ''}
          ${showDisconnectButton ? html`
            <button @click=${this.onDisconnect}>Disconnect</button>
          ` : ''}
        </div>
      </div>
    `;
  }

  private getStatusDisplay(): { icon: string; text: string } {
    switch (this.state) {
      case ConnectionState.DISCONNECTED:
        return { icon: '❌', text: 'Disconnected' };
      case ConnectionState.SCANNING:
        return { icon: '🔍', text: 'Scanning...' };
      case ConnectionState.CONNECTING:
        return { icon: '🔗', text: 'Connecting...' };
      case ConnectionState.CONNECTED:
        return { icon: '✅', text: 'Ready to send!' };
      case ConnectionState.ERROR:
        return { icon: '⚠️', text: 'Connection error' };
      default:
        return { icon: '❓', text: 'Unknown' };
    }
  }

  private onConnect() {
    this.dispatchEvent(new CustomEvent('connect', {
      bubbles: true,
      composed: true
    }));
  }

  private onDisconnect() {
    this.dispatchEvent(new CustomEvent('disconnect', {
      bubbles: true,
      composed: true
    }));
  }
}
