/**
 * Connection Status Web Component
 * Uses DaisyUI navbar component
 */

import { html, LitElement } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import { ConnectionState } from '../services/BleService';
import './theme-switcher';

@customElement('connection-status')
export class ConnectionStatus extends LitElement {
  @property({ type: String }) state: ConnectionState = ConnectionState.DISCONNECTED;

  // Disable shadow DOM to allow Tailwind classes to work
  createRenderRoot() {
    return this;
  }

  render() {
    const badge = this.getStatusDisplay();
    const showConnectButton =
      this.state === ConnectionState.DISCONNECTED || this.state === ConnectionState.ERROR;
    const showDisconnectButton = this.state === ConnectionState.CONNECTED;

    return html`
      <nav class="navbar bg-base-200 shadow-lg">
        <div class="flex-1 px-4">
          <h1 class="text-2xl font-bold">LoRa Chat</h1>
        </div>
        <div class="flex-none gap-3 px-4">
          ${badge}
          <theme-switcher></theme-switcher>
          ${
            showConnectButton
              ? html`<button class="btn btn-primary btn-sm" @click=${this.onConnect}>Connect</button>`
              : ''
          }
          ${
            showDisconnectButton
              ? html`<button class="btn btn-error btn-sm" @click=${this.onDisconnect}>Disconnect</button>`
              : ''
          }
        </div>
      </nav>
    `;
  }

  private getStatusDisplay(): ReturnType<typeof html> {
    switch (this.state) {
      case ConnectionState.DISCONNECTED:
        return html`<div class="badge badge-error gap-2">
          <svg xmlns="http://www.w3.org/2000/svg" height="16" viewBox="0 -960 960 960" width="16" fill="currentColor">
            <path d="m256-200-56-56 224-224-224-224 56-56 224 224 224-224 56 56-224 224 224 224-56 56-224-224-224 224Z"/>
          </svg>
          Disconnected
        </div>`;
      case ConnectionState.SCANNING:
        return html`<div class="badge badge-warning gap-2">
          <span class="loading loading-spinner loading-xs"></span>
          Scanning
        </div>`;
      case ConnectionState.CONNECTING:
        return html`<div class="badge badge-info gap-2">
          <span class="loading loading-spinner loading-xs"></span>
          Connecting
        </div>`;
      case ConnectionState.CONNECTED:
        return html`<div class="badge badge-success gap-2">
          <svg xmlns="http://www.w3.org/2000/svg" height="16" viewBox="0 -960 960 960" width="16" fill="currentColor">
            <path d="M382-240 154-468l57-57 171 171 367-367 57 57-424 424Z"/>
          </svg>
          Connected
        </div>`;
      case ConnectionState.ERROR:
        return html`<div class="badge badge-error gap-2">
          <svg xmlns="http://www.w3.org/2000/svg" height="16" viewBox="0 -960 960 960" width="16" fill="currentColor">
            <path d="M480-280q17 0 28.5-11.5T520-320q0-17-11.5-28.5T480-360q-17 0-28.5 11.5T440-320q0 17 11.5 28.5T480-280Zm-40-160h80v-240h-80v240Zm40 360q-83 0-156-31.5T197-197q-54-54-85.5-127T80-480q0-83 31.5-156T197-763q54-54 127-85.5T480-880q83 0 156 31.5T763-763q54 54 85.5 127T880-480q0 83-31.5 156T763-197q-54 54-127 85.5T480-80Zm0-80q134 0 227-93t93-227q0-134-93-227t-227-93q-134 0-227 93t-93 227q0 134 93 227t227 93Zm0-320Z"/>
          </svg>
          Error
        </div>`;
      default:
        return html`<div class="badge badge-ghost">Unknown</div>`;
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
