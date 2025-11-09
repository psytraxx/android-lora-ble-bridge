/**
 * Message Bubble Web Component
 */

import { LitElement, html, css } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import { ChatMessage, AckStatus } from '../services/MessageRepository';
import { formatTime, formatMapsUrl } from '../utils/format';

@customElement('message-bubble')
export class MessageBubble extends LitElement {
  @property({ type: Object }) message!: ChatMessage;

  static styles = css`
    :host {
      display: block;
      margin: 8px 0;
    }

    .bubble {
      max-width: 80%;
      padding: 12px 16px;
      border-radius: 16px;
      word-wrap: break-word;
      position: relative;
      box-shadow: 0 1px 2px rgba(0, 0, 0, 0.1);
    }

    .sent {
      margin-left: auto;
      background: var(--primary-color, #1976d2);
      color: white;
      border-bottom-right-radius: 4px;
    }

    .received {
      margin-right: auto;
      background: var(--surface-color, #f5f5f5);
      color: var(--on-surface-color, #212121);
      border-bottom-left-radius: 4px;
    }

    .text {
      margin: 0 0 8px 0;
      font-size: 15px;
      line-height: 1.4;
    }

    .meta {
      display: flex;
      gap: 8px;
      align-items: center;
      font-size: 12px;
      opacity: 0.8;
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
      gap: 4px;
    }

    .gps-link:hover {
      text-decoration: underline;
    }

    .ack-pending {
      opacity: 0.6;
    }
  `;

  render() {
    const alignClass = this.message.isSent ? 'sent' : 'received';
    const ackIcon = this.getAckIcon();

    return html`
      <div class="bubble ${alignClass} ${this.message.ackStatus === AckStatus.PENDING ? 'ack-pending' : ''}">
        <p class="text">${this.message.text}</p>
        <div class="meta">
          <span class="time">${formatTime(this.message.timestamp)}</span>
          ${this.message.hasGps ? html`
            <a
              class="gps-link"
              href="${formatMapsUrl(this.message.latitude!, this.message.longitude!)}"
              target="_blank"
              rel="noopener noreferrer"
              title="Open in Google Maps"
            >
              📍
            </a>
          ` : ''}
          ${ackIcon}
        </div>
      </div>
    `;
  }

  private getAckIcon() {
    if (!this.message.isSent) return '';

    switch (this.message.ackStatus) {
      case AckStatus.PENDING:
        return html`<span title="Waiting for acknowledgment">⏱</span>`;
      case AckStatus.DELIVERED:
        return html`<span title="Delivered">✓</span>`;
      default:
        return '';
    }
  }
}
