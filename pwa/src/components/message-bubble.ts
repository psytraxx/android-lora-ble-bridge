/**
 * Message Bubble Web Component
 * Uses DaisyUI chat component
 */

import { html, LitElement } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import { AckStatus, type ChatMessage } from '../services/MessageRepository';
import { formatMapsUrl, formatTime } from '../utils/format';

@customElement('message-bubble')
export class MessageBubble extends LitElement {
  @property({ type: Object }) message!: ChatMessage;

  // Disable shadow DOM to allow Tailwind classes to work
  createRenderRoot() {
    return this;
  }

  render() {
    const chatAlignment = this.message.isSent ? 'chat-end' : 'chat-start';
    const bubbleColor = this.message.isSent ? 'chat-bubble-primary' : 'chat-bubble-secondary';
    const ackIcon = this.getAckIcon();

    return html`
      <div class="chat ${chatAlignment}">
        <div class="chat-bubble ${bubbleColor} ${this.message.ackStatus === AckStatus.PENDING ? 'opacity-60' : ''}">
          <p class="whitespace-pre-wrap">${this.message.text}</p>
          <div class="chat-footer opacity-70 flex gap-2 items-center mt-1">
            <time class="text-xs font-medium">${formatTime(this.message.timestamp)}</time>
            ${
              this.message.hasGps
                ? html`
              <a
                class="inline-flex items-center gap-1 hover:opacity-80 transition-opacity"
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
        return html`<svg xmlns="http://www.w3.org/2000/svg" height="16" viewBox="0 -960 960 960" width="16" fill="currentColor" title="Sending">
					<path d="m612-292 56-56-148-148v-184h-80v216l172 172ZM480-80q-83 0-156-31.5T197-197q-54-54-85.5-127T80-480q0-83 31.5-156T197-763q54-54 127-85.5T480-880q83 0 156 31.5T763-763q54 54 85.5 127T880-480q0 83-31.5 156T763-197q-54 54-127 85.5T480-80Zm0-400Zm0 320q133 0 226.5-93.5T800-480q0-133-93.5-226.5T480-800q-133 0-226.5 93.5T160-480q0 133 93.5 226.5T480-160Z"/>
				</svg>`;
      case AckStatus.DELIVERED:
        return html`<svg xmlns="http://www.w3.org/2000/svg" height="16" viewBox="0 -960 960 960" width="16" fill="currentColor" title="Delivered">
					<path d="M382-240 154-468l57-57 171 171 367-367 57 57-424 424Z"/>
				</svg>`;
      default:
        return '';
    }
  }
}
