/**
 * Message List Web Component
 * Uses DaisyUI styling
 */

import { html, LitElement } from 'lit';
import { customElement, property, query } from 'lit/decorators.js';
import { repeat } from 'lit/directives/repeat.js';
import type { ChatMessage } from '../services/MessageRepository';
import './message-bubble';
import { sharedStylesheet } from '../shared-styles';

@customElement('message-list')
export class MessageList extends LitElement {
  @property({ type: Array }) messages: ChatMessage[] = [];
  @query('.messages') messagesContainer!: HTMLDivElement;

  static styles = [sharedStylesheet];

  render() {
    if (this.messages.length === 0) {
      return html`
        <div class="flex items-center justify-center h-full p-8">
          <div class="card bg-base-200 shadow-xl max-w-md">
            <div class="card-body items-center text-center gap-4">
              <div class="text-6xl opacity-50">💬</div>
              <h2 class="card-title">No messages yet</h2>
              <p class="text-base-content/70">
                Connect to your ESP32 LoRa device and start sending messages over long-range radio.
              </p>
               <p class="text-base-content/70">
                Contribute to the project on <a href="https://github.com/psytraxx/android-lora-ble-bridge" target="_blank" rel="noopener noreferrer" class="underline">GitHub</a>.
              </p>
              <div class="stats shadow">
                <div class="stat place-items-center py-3">
                  <div class="stat-title text-xs">Max Range</div>
                  <div class="stat-value text-primary text-xl">10-25 km</div>
                </div>
                <div class="stat place-items-center py-3">
                  <div class="stat-title text-xs">Max Length</div>
                  <div class="stat-value text-secondary text-xl">50 chars</div>
                </div>
              </div>
            </div>
          </div>
        </div>
      `;
    }

    return html`
      <div class="messages h-full overflow-y-auto p-4 flex flex-col gap-2 bg-base-200">
        ${repeat(
          this.messages,
          (msg) => msg.id,
          (msg) => html`<message-bubble .message=${msg}></message-bubble>`
        )}
      </div>
    `;
  }

  updated(changedProperties: Map<string, unknown>) {
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
