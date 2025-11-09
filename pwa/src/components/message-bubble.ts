/**
 * Message Bubble Web Component
 */

import { css, html, LitElement } from "lit";
import { customElement, property } from "lit/decorators.js";
import { AckStatus, type ChatMessage } from "../services/MessageRepository";
import { formatMapsUrl, formatTime } from "../utils/format";

// Import Material Web Components
import '@material/web/elevation/elevation.js';

@customElement("message-bubble")
export class MessageBubble extends LitElement {
	@property({ type: Object }) message!: ChatMessage;

	static styles = css`
    :host {
      display: block;
      margin: 4px 0;
    }

    .bubble {
      max-width: 85%;
      padding: 12px 16px;
      border-radius: var(--md-sys-shape-corner-large);
      word-wrap: break-word;
      position: relative;
      transition: box-shadow 200ms;
    }

    .bubble md-elevation {
      --md-elevation-level: 1;
    }

    .bubble:hover md-elevation {
      --md-elevation-level: 2;
    }

    .sent {
      margin-left: auto;
      background: var(--md-sys-color-primary-container);
      color: var(--md-sys-color-on-primary-container);
      border-bottom-right-radius: var(--md-sys-shape-corner-extra-small);
    }

    .received {
      margin-right: auto;
      background: var(--md-sys-color-secondary-container);
      color: var(--md-sys-color-on-secondary-container);
      border-bottom-left-radius: var(--md-sys-shape-corner-extra-small);
    }

    .text {
      margin: 0 0 4px 0;
      font-size: var(--md-sys-typescale-body-medium);
      line-height: 1.5;
      white-space: pre-wrap;
    }

    .meta {
      display: flex;
      gap: 8px;
      align-items: center;
      font-size: var(--md-sys-typescale-label-small);
      opacity: 0.7;
    }

    .time {
      font-weight: 500;
    }

    .gps-link {
      cursor: pointer;
      text-decoration: none;
      color: inherit;
      display: inline-flex;
      align-items: center;
      gap: 2px;
      padding: 2px;
      border-radius: var(--md-sys-shape-corner-extra-small);
      transition: opacity 200ms;
    }

    .gps-link:hover {
      opacity: 0.8;
    }

    .ack-pending {
      opacity: 0.6;
    }
  `;

	render() {
		const alignClass = this.message.isSent ? "sent" : "received";
		const ackIcon = this.getAckIcon();

		return html`
      <div class="bubble ${alignClass} ${this.message.ackStatus === AckStatus.PENDING ? "ack-pending" : ""}">
        <md-elevation></md-elevation>
        <p class="text">${this.message.text}</p>
        <div class="meta">
          <span class="time">${formatTime(this.message.timestamp)}</span>
          ${
						this.message.hasGps
							? html`
            <a
              class="gps-link"
              href="${formatMapsUrl(this.message.latitude!, this.message.longitude!)}"
              target="_blank"
              rel="noopener noreferrer"
              title="Open in Google Maps"
            >
              <svg xmlns="http://www.w3.org/2000/svg" height="16" viewBox="0 -960 960 960" width="16" fill="currentColor">
                <path d="M480-480q33 0 56.5-23.5T560-560q0-33-23.5-56.5T480-640q-33 0-56.5 23.5T400-560q0 33 23.5 56.5T480-480Zm0 294q122-112 181-203.5T720-552q0-109-69.5-178.5T480-800q-101 0-170.5 69.5T240-552q0 71 59 162.5T480-186Zm0 106Q319-217 239.5-334.5T160-552q0-150 96.5-239T480-880q127 0 223.5 89T800-552q0 100-79.5 217.5T480-80Zm0-480Z"/>
              </svg>
            </a>
          `
							: ""
					}
          ${ackIcon}
        </div>
      </div>
    `;
	}

	private getAckIcon() {
		if (!this.message.isSent) return "";

		switch (this.message.ackStatus) {
			case AckStatus.PENDING:
				return html`<svg xmlns="http://www.w3.org/2000/svg" height="16" viewBox="0 -960 960 960" width="16" fill="currentColor" title="Sending">
					<path d="m612-292 56-56-148-148v-184h-80v216l172 172ZM480-80q-83 0-156-31.5T197-197q-54-54-85.5-127T80-480q0-83 31.5-156T197-763q54-54 127-85.5T480-880q83 0 156 31.5T763-763q54 54 85.5 127T880-480q0 83-31.5 156T763-197q-54 54-127 85.5T480-80Zm0-400Zm0 320q133 0 226.5-93.5T800-480q0-133-93.5-226.5T480-800q-133 0-226.5 93.5T160-480q0 133 93.5 226.5T480-160Z"/>
				</svg>`;
			case AckStatus.DELIVERED:
				return html`<svg xmlns="http://www.w3.org/2000/svg" height="16" viewBox="0 -960 960 960" width="16" fill="currentColor" title="Delivered">
					<path d="M382-240 154-468l57-57 171 171 367-367 57 57-424 424Z"/>
				</svg>`;
			default:
				return "";
		}
	}
}
