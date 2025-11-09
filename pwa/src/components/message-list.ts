/**
 * Message List Web Component
 */

import { LitElement, html, css } from 'lit';
import { customElement, property, query } from 'lit/decorators.js';
import { repeat } from 'lit/directives/repeat.js';
import { ChatMessage } from '../services/MessageRepository';
import './message-bubble';

@customElement('message-list')
export class MessageList extends LitElement {
  @property({ type: Array }) messages: ChatMessage[] = [];
  @query('.messages') messagesContainer!: HTMLDivElement;

  static styles = css`
    :host {
      display: flex;
      flex-direction: column;
      height: 100%;
      overflow: hidden;
    }

    .messages {
      flex: 1;
      overflow-y: auto;
      padding: 16px;
      display: flex;
      flex-direction: column;
      gap: 4px;
    }

    .empty-state {
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      height: 100%;
      color: var(--text-secondary-color, #757575);
      text-align: center;
      padding: 32px;
    }

    .empty-state-icon {
      font-size: 48px;
      margin-bottom: 16px;
    }

    .empty-state-text {
      font-size: 16px;
      margin: 0;
    }
  `;

  render() {
    if (this.messages.length === 0) {
      return html`
        <div class="empty-state">
          <div class="empty-state-icon">💬</div>
          <p class="empty-state-text">No messages yet.<br>Connect and start chatting!</p>
        </div>
      `;
    }

    return html`
      <div class="messages">
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
