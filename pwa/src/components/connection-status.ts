/**
 * Connection Status Web Component
 * Uses DaisyUI navbar component
 */

import { html, LitElement } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import { ConnectionState } from '../services/BleService';
import './theme-switcher';
import { sharedStylesheet } from '../shared-styles';

@customElement('connection-status')
export class ConnectionStatus extends LitElement {
  @property({ type: String }) state: ConnectionState = ConnectionState.DISCONNECTED;
  @property({ type: String }) deviceName: string | null = null;
  @property({ type: String }) deviceId: string | null = null;
  @property({ type: Number }) batteryLevel: number | null = null;

  static styles = [sharedStylesheet];

  render() {
    const badge = this.getStatusDisplay();
    const showConnectButton =
      this.state === ConnectionState.DISCONNECTED || this.state === ConnectionState.ERROR;
    const showDisconnectButton = this.state === ConnectionState.CONNECTED;

    return html`
      <nav class="navbar bg-base-200 shadow-lg relative">
         <div class="flex-1 px-4 flex items-center gap-3">
          <a
            href="https://github.com/psytraxx/android-lora-ble-bridge"
            target="_blank"
            rel="noopener noreferrer"
            class="inline-flex items-center gap-2"
            aria-label="Open GitHub repository"
          >
            <img src="./apple-touch-icon.png" width="38" height="38" alt="App icon" class="block rounded"/>
          </a>
          <h1 class="text-2xl font-bold hidden sm:block">Chat</h1>
        </div>
        <!-- Center the badge and device info in the middle of the navbar -->
        <div class="absolute left-1/2 top-1/2 transform -translate-x-1/2 -translate-y-1/2 z-10 flex items-center gap-3">
          <div class="flex items-center">${badge}</div>
          ${this.deviceName || this.deviceId
        ? html`<div class="flex flex-col text-left text-xs ml-2">
                ${this.deviceName ? html`<div class="font-medium">${this.deviceName}</div>` : ''}
                ${this.deviceId ? html`<div class="text-base-content/70 truncate max-w-xs">${this.deviceId}</div>` : ''}
              </div>`
        : ''
      }
          ${this.batteryLevel !== null
        ? html`<div class="badge badge-ghost gap-1 text-xs">
                <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" class="size-4">
                  <path stroke-linecap="round" stroke-linejoin="round" d="M21 10.5h.375c.621 0 1.125.504 1.125 1.125v2.25c0 .621-.504 1.125-1.125 1.125H21M3.75 18h15A2.25 2.25 0 0 0 21 15.75v-6a2.25 2.25 0 0 0-2.25-2.25h-15A2.25 2.25 0 0 0 1.5 9.75v6A2.25 2.25 0 0 0 3.75 18Z" />
                </svg>
                ${this.batteryLevel}%
              </div>`
        : ''
      }
        </div>

        <div class="flex-none px-4 flex items-center gap-3 ml-auto">
          <theme-switcher></theme-switcher>
          ${showConnectButton
        ? html`<button class="btn btn-primary" @click=${this.onConnect}>
                  Connect
                </button>`
        : ''
      }
          ${showDisconnectButton
        ? html`<button class="btn btn-error btn-sm" @click=${this.onDisconnect}>
                  <span class="sm:hidden">
                    <svg xmlns="http://www.w3.org/2000/svg" height="16" viewBox="0 -960 960 960" width="16" fill="currentColor">
                      <path d="M480-80q-83 0-156-31.5T197-197q-54-54-85.5-127T80-480q0-83 31.5-156T197-763q54-54 127-85.5T480-880q83 0 156 31.5T763-763q54 54 85.5 127T880-480q0 83-31.5 156T763-197q-54 54-127 85.5T480-80Zm0-80q134 0 227-93t93-227q0-134-93-227t-227-93q-134 0-227 93t-93 227q0 134 93 227t227 93Zm0-320Z"/>
                    </svg>
                  </span>
                  <span class="hidden sm:inline">Disconnect</span>
                </button>`
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
      case ConnectionState.DISCOVERING:
        return html`<div class="badge badge-info gap-2">
          <span class="loading loading-spinner loading-xs"></span>
          Discovering
        </div>`;
      case ConnectionState.ENABLING_NOTIFICATIONS:
        return html`<div class="badge badge-info gap-2">
          <span class="loading loading-spinner loading-xs"></span>
          Enabling notifications
        </div>`;
      case ConnectionState.CONNECTED:
        return html`<div class="badge badge-success gap-2">
          <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" class="size-6">
            <path stroke-linecap="round" stroke-linejoin="round" d="M13.19 8.688a4.5 4.5 0 0 1 1.242 7.244l-4.5 4.5a4.5 4.5 0 0 1-6.364-6.364l1.757-1.757m13.35-.622 1.757-1.757a4.5 4.5 0 0 0-6.364-6.364l-4.5 4.5a4.5 4.5 0 0 0 1.242 7.244" />
          </svg>
          Connected
        </div>`;
      case ConnectionState.ERROR:
        return html`<div class="badge badge-error gap-2">
          <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" class="size-6">
            <path stroke-linecap="round" stroke-linejoin="round" d="M12 9v3.75m-9.303 3.376c-.866 1.5.217 3.374 1.948 3.374h14.71c1.73 0 2.813-1.874 1.948-3.374L13.949 3.378c-.866-1.5-3.032-1.5-3.898 0L2.697 16.126ZM12 15.75h.007v.008H12v-.008Z" />
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
