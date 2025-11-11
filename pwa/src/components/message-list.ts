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
        <div class="flex flex-col items-center justify-center h-full text-base-content/60 text-center p-12">
          <div class="text-6xl mb-4 opacity-40">💬</div>
          <p class="text-base">No messages yet.<br>Connect and start chatting!</p>
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
