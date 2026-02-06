/**
 * Connection Status Web Component
 * Uses DaisyUI navbar component
 */

import { html, LitElement } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import { ConnectionState } from '../services/BleService';
import './theme-switcher';
import { sharedStylesheet } from '../shared-styles';
import {
  exclamationTriangleIcon,
  informationCircleIcon,
  linkIcon,
  stopCircleIcon,
  xMarkIcon
} from '../utils/icons';

@customElement('connection-status')
export class ConnectionStatus extends LitElement {
  @property({ type: String }) state: ConnectionState = ConnectionState.DISCONNECTED;
  @property({ type: String }) deviceName: string | null = null;
  @property({ type: String }) deviceId: string | null = null;

  static styles = [sharedStylesheet];

  render() {
    const badge = this.getStatusDisplay();
    const showConnectButton =
      this.state === ConnectionState.DISCONNECTED || this.state === ConnectionState.ERROR;
    const showDisconnectButton = this.state === ConnectionState.CONNECTED;
    const showInfoButton = this.state === ConnectionState.CONNECTED;

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
          ${showInfoButton
        ? html`<button class="btn btn-ghost btn-xs gap-1" @click=${this.onInfoRequest} title="Device Info">
                ${informationCircleIcon('w-4 h-4')}
                <span class="hidden sm:inline">Info</span>
              </button>`
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
                    ${stopCircleIcon('w-4 h-4')}
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
          ${xMarkIcon('w-4 h-4')}
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
          ${linkIcon('w-4 h-4')}
          Connected
        </div>`;
      case ConnectionState.ERROR:
        return html`<div class="badge badge-error gap-2">
          ${exclamationTriangleIcon('w-4 h-4')}
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

  private onInfoRequest() {
    this.dispatchEvent(
      new CustomEvent('info-request', {
        bubbles: true,
        composed: true
      })
    );
  }
}
