/**
 * Message List Web Component
 * Uses DaisyUI styling
 */

import { html, LitElement } from 'lit';
import { customElement, property, query } from 'lit/decorators.js';
import { repeat } from 'lit/directives/repeat.js';
import type { ChatMessage } from '../services/MessageRepository';
import './message-bubble';

@customElement('message-list')
export class MessageList extends LitElement {
  @property({ type: Array }) messages: ChatMessage[] = [];
  @query('.messages') messagesContainer!: HTMLDivElement;

  // Disable shadow DOM to allow Tailwind classes to work
  createRenderRoot() {
    return this;
  }

  render() {
    if (this.messages.length === 0) {
      return html`
        <div class="flex flex-col items-center justify-center h-full p-12">
          <div class="card bg-base-200 shadow-xl max-w-md">
            <div class="card-body items-center text-center">
              <div class="text-6xl mb-4 opacity-60">💬</div>
              <h2 class="card-title">No messages yet</h2>
              <p class="text-base-content/70">
                Connect to your ESP32 LoRa device and start sending messages over long-range radio.
              </p>
              <div class="card-actions mt-4">
                <div class="stats shadow">
                  <div class="stat place-items-center">
                    <div class="stat-title">Max Range</div>
                    <div class="stat-value text-primary text-2xl">5-15 km</div>
                  </div>
                  <div class="stat place-items-center">
                    <div class="stat-title">Max Length</div>
                    <div class="stat-value text-secondary text-2xl">50 chars</div>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>
      `;
    }

    return html`
      <div class="messages flex-1 overflow-y-auto p-4 flex flex-col gap-1 bg-base-200">
        ${repeat(
          this.messages,
          (msg) => msg.id,
          (msg) => html`<message-bubble .message=${msg}></message-bubble>`
        )}
      </div>
    `;
  }

  updated(changedProperties: Map<string, any>) {
    super.updated(changedProperties);

    if (changedProperties.has('messages') && this.messages.length > 0) {
      this.scrollToBottom();
    }
  }

  private scrollToBottom() {
    requestAnimationFrame(() => {
      if (this.messagesContainer) {
        this.messagesContainer.scrollTop = this.messagesContainer.scrollHeight;
      }
    });
  }
}
