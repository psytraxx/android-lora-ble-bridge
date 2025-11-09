/**
 * Connection Status Web Component
 * Matches Android CenterAlignedTopAppBar design
 */

import { css, html, LitElement } from "lit";
import { customElement, property } from "lit/decorators.js";
import { ConnectionState } from "../services/BleService";

@customElement("connection-status")
export class ConnectionStatus extends LitElement {
	@property({ type: String }) state: ConnectionState =
		ConnectionState.DISCONNECTED;

	static styles = css`
    :host {
      display: block;
    }

    /* Top app bar - matches Android CenterAlignedTopAppBar */
    .top-app-bar {
      background: var(--md-sys-color-primary);
      color: var(--md-sys-color-on-primary);
      padding: 12px 16px;
      display: flex;
      justify-content: space-between;
      align-items: center;
      min-height: 64px;
      box-shadow: 0 2px 4px rgba(0,0,0,0.1);
    }

    .title-container {
      flex: 1;
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 4px;
    }

    .title {
      font-size: 22px;
      font-weight: 400;
      margin: 0;
      letter-spacing: 0;
    }

    .status-text {
      font-size: 12px;
      opacity: 0.9;
    }

    .actions {
      display: flex;
      gap: 8px;
    }

    /* Button - matches Android Material3 Button */
    button {
      padding: 10px 24px;
      border: none;
      border-radius: 20px;
      font-size: 14px;
      font-weight: 500;
      cursor: pointer;
      transition: all 0.2s;
      font-family: inherit;
      letter-spacing: 0.1px;
      text-transform: none;
    }

    button:active {
      transform: scale(0.96);
    }

    .connect-btn {
      background: var(--md-sys-color-on-primary);
      color: var(--md-sys-color-primary);
    }

    .connect-btn:hover {
      box-shadow: 0 1px 3px rgba(0,0,0,0.2);
    }

    .disconnect-btn {
      background: transparent;
      color: var(--md-sys-color-on-primary);
      border: 1px solid var(--md-sys-color-on-primary);
    }

    .disconnect-btn:hover {
      background: rgba(255,255,255,0.1);
    }
  `;

	render() {
		const { icon, text } = this.getStatusDisplay();
		const showConnectButton =
			this.state === ConnectionState.DISCONNECTED ||
			this.state === ConnectionState.ERROR;
		const showDisconnectButton = this.state === ConnectionState.CONNECTED;

		return html`
      <div class="top-app-bar">
        <div class="title-container">
          <h1 class="title">LoRa Chat</h1>
          <div class="status-text">${icon} ${text}</div>
        </div>
        <div class="actions">
          ${
						showConnectButton
							? html`<button class="connect-btn" @click=${this.onConnect}>Connect</button>`
							: ""
					}
          ${
						showDisconnectButton
							? html`<button class="disconnect-btn" @click=${this.onDisconnect}>Disconnect</button>`
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
