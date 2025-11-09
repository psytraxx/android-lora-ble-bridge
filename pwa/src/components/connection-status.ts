/**
 * Connection Status Web Component
 */

import { css, html, LitElement } from "lit";
import { customElement, property } from "lit/decorators.js";
import { ConnectionState } from "../services/BleService";

// Import Material Web Components
import '@material/web/button/filled-button.js';
import '@material/web/button/outlined-button.js';

@customElement("connection-status")
export class ConnectionStatus extends LitElement {
	@property({ type: String }) state: ConnectionState =
		ConnectionState.DISCONNECTED;

	static styles = css`
    :host {
      display: block;
    }

    .status-bar {
      background: var(--md-sys-color-primary);
      color: var(--md-sys-color-on-primary);
      padding: 16px 20px;
      display: flex;
      justify-content: space-between;
      align-items: center;
      box-shadow: var(--md-sys-elevation-level2);
    }

    .title {
      font-size: var(--md-sys-typescale-title-large);
      font-weight: 400;
      margin: 0;
    }

    .status {
      font-size: var(--md-sys-typescale-body-medium);
      display: flex;
      align-items: center;
      gap: 16px;
    }

    .status-text {
      display: flex;
      align-items: center;
      gap: 8px;
    }

    .status-icon {
      font-size: 20px;
    }

    md-filled-button, md-outlined-button {
      --md-sys-color-primary: var(--md-sys-color-on-primary);
      --md-sys-color-on-primary: var(--md-sys-color-primary);
    }
  `;

	render() {
		const { icon, text } = this.getStatusDisplay();
		const showConnectButton =
			this.state === ConnectionState.DISCONNECTED ||
			this.state === ConnectionState.ERROR;
		const showDisconnectButton = this.state === ConnectionState.CONNECTED;

		return html`
      <div class="status-bar">
        <h1 class="title">LoRa Chat</h1>
        <div class="status">
          <div class="status-text">
            <span class="status-icon">${icon}</span>
            <span>${text}</span>
          </div>
          ${
						showConnectButton
							? html`
            <md-filled-button @click=${this.onConnect}>Connect</md-filled-button>
          `
							: ""
					}
          ${
						showDisconnectButton
							? html`
            <md-outlined-button @click=${this.onDisconnect}>Disconnect</md-outlined-button>
          `
							: ""
					}
        </div>
      </div>
    `;
	}

	private getStatusDisplay(): { icon: string; text: string } {
		switch (this.state) {
			case ConnectionState.DISCONNECTED:
				return { icon: "❌", text: "Disconnected" };
			case ConnectionState.SCANNING:
				return { icon: "🔍", text: "Scanning..." };
			case ConnectionState.CONNECTING:
				return { icon: "🔗", text: "Connecting..." };
			case ConnectionState.CONNECTED:
				return { icon: "✅", text: "Ready to send!" };
			case ConnectionState.ERROR:
				return { icon: "⚠️", text: "Connection error" };
			default:
				return { icon: "❓", text: "Unknown" };
		}
	}

	private onConnect() {
		this.dispatchEvent(
			new CustomEvent("connect", {
				bubbles: true,
				composed: true,
			}),
		);
	}

	private onDisconnect() {
		this.dispatchEvent(
			new CustomEvent("disconnect", {
				bubbles: true,
				composed: true,
			}),
		);
	}
}
