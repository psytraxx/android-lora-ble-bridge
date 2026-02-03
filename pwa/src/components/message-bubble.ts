/**
 * Message Bubble Web Component
 * Uses DaisyUI chat component
 */

import { html, LitElement } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import { AckStatus, type ChatMessage } from '../services/MessageRepository';
import { sharedStylesheet } from '../shared-styles';
import { formatMapsUrl, formatTime } from '../utils/format';
import { checkIcon, clockIcon, exclamationCircleIcon, mapPinIcon } from '../utils/icons';

@customElement('message-bubble')
export class MessageBubble extends LitElement {
  @property({ type: Object }) message!: ChatMessage;

  static styles = [sharedStylesheet];

  render() {
    const chatAlignment = this.message.isSent ? 'chat-end' : 'chat-start';
    const bubbleColor = this.message.isSent ? 'chat-bubble-primary' : 'chat-bubble-secondary';
    const ackIcon = this.getAckIcon();

    return html`
      <div class="chat ${chatAlignment}">
        <div class="chat-bubble ${bubbleColor} ${this.message.ackStatus === AckStatus.PENDING ? 'opacity-60' : ''} ${this.message.ackStatus === AckStatus.FAILED ? 'border-2 border-error' : ''}">
          <p class="whitespace-pre-wrap">${this.message.text}</p>
          <div class="chat-footer opacity-70 flex gap-2 items-center mt-1">
            <time class="text-xs font-medium">${formatTime(this.message.timestamp)}</time>
            ${
              this.message.hasGps &&
              this.message.latitude !== undefined &&
              this.message.longitude !== undefined
                ? html`
              <div class="tooltip tooltip-top" data-tip="Location: ${this.message.latitude.toFixed(6)}, ${this.message.longitude.toFixed(6)}">
                <a
                  class="inline-flex items-center gap-1 hover:opacity-80 transition-opacity"
                  href="${formatMapsUrl(this.message.latitude, this.message.longitude)}"
                  target="_blank"
                  rel="noopener noreferrer"
                  aria-label="Open location in Google Maps"
                >
                  ${mapPinIcon('w-4 h-4')}
                </a>
              </div>
            `
                : ''
            }
            ${ackIcon}
          </div>
        </div>
      </div>
    `;
  }

  private getAckIcon() {
    if (!this.message.isSent) return '';

    switch (this.message.ackStatus) {
      case AckStatus.PENDING:
        return html`
          <div class="tooltip tooltip-top" data-tip="Waiting for acknowledgment">
            ${clockIcon('w-4 h-4')}
          </div>
        `;
      case AckStatus.DELIVERED:
        return html`
          <div class="tooltip tooltip-top" data-tip="Delivered">
            ${checkIcon('w-4 h-4')}
          </div>
        `;
      case AckStatus.FAILED:
        return html`
          <div class="tooltip tooltip-top" data-tip="Delivery failed">
            ${exclamationCircleIcon('w-4 h-4 text-error')}
          </div>
        `;
      default:
        return '';
    }
  }
}
