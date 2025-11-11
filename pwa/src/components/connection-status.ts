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
      <div class="navbar bg-base-200 shadow-lg min-h-16">
        <div class="flex-1">
          <h1 class="text-2xl font-bold tracking-tight px-4">LoRa Chat</h1>
        </div>
        <div class="flex-none">
          <div class="flex items-center gap-4 px-4">
            <div class="flex items-center gap-2 text-sm font-medium">
              <span class="text-xl">${icon}</span>
              <span>${text}</span>
            </div>
            ${
              showConnectButton
                ? html`<button class="btn btn-success btn-sm px-6" @click=${this.onConnect}>Connect</button>`
                : ''
            }
            ${
              showDisconnectButton
                ? html`<button class="btn btn-error btn-outline btn-sm px-6" @click=${this.onDisconnect}>Disconnect</button>`
                : ''
            }
          </div>
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
