/**
 * Connection Status Web Component
 * Uses DaisyUI navbar component
 */

import { html, LitElement } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import { ConnectionState } from '../services/BleService';

@customElement('connection-status')
export class ConnectionStatus extends LitElement {
  @property({ type: String }) state: ConnectionState = ConnectionState.DISCONNECTED;

  // Disable shadow DOM to allow Tailwind classes to work
  createRenderRoot() {
    return this;
  }

  render() {
    const { icon, text } = this.getStatusDisplay();
    const showConnectButton =
      this.state === ConnectionState.DISCONNECTED || this.state === ConnectionState.ERROR;
    const showDisconnectButton = this.state === ConnectionState.CONNECTED;

    return html`
      <div class="navbar bg-primary text-primary-content shadow-lg">
        <div class="navbar-center flex-col gap-1">
          <h1 class="text-2xl font-normal">LoRa Chat</h1>
          <div class="text-xs opacity-90">${icon} ${text}</div>
        </div>
        <div class="navbar-end gap-2">
          ${
            showConnectButton
              ? html`<button class="btn btn-accent btn-sm" @click=${this.onConnect}>Connect</button>`
              : ''
          }
          ${
            showDisconnectButton
              ? html`<button class="btn btn-outline btn-sm" @click=${this.onDisconnect}>Disconnect</button>`
              : ''
          }
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
    this.dispatchEvent(
      new CustomEvent('connect', {
        bubbles: true,
        composed: true
      })
    );
  }

  private onDisconnect() {
    this.dispatchEvent(
      new CustomEvent('disconnect', {
        bubbles: true,
        composed: true
      })
    );
  }
}
