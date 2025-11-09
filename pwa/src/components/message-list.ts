/**
 * Message List Web Component
 */

import { css, html, LitElement } from "lit";
import { customElement, property, query } from "lit/decorators.js";
import { repeat } from "lit/directives/repeat.js";
import type { ChatMessage } from "../services/MessageRepository";
import "./message-bubble";

@customElement("message-list")
export class MessageList extends LitElement {
	@property({ type: Array }) messages: ChatMessage[] = [];
	@query(".messages") messagesContainer!: HTMLDivElement;

	static styles = css`
    :host {
      display: flex;
      flex-direction: column;
      height: 100%;
      overflow: hidden;
      background: var(--md-sys-color-background);
    }

    .messages {
      flex: 1;
      overflow-y: auto;
      padding: 20px;
      display: flex;
      flex-direction: column;
      gap: 4px;
    }

    .messages::-webkit-scrollbar {
      width: 8px;
    }

    .messages::-webkit-scrollbar-track {
      background: var(--md-sys-color-surface-container-low);
    }

    .messages::-webkit-scrollbar-thumb {
      background: var(--md-sys-color-outline-variant);
      border-radius: var(--md-sys-shape-corner-full);
    }

    .messages::-webkit-scrollbar-thumb:hover {
      background: var(--md-sys-color-outline);
    }

    .empty-state {
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      height: 100%;
      color: var(--md-sys-color-on-surface-variant);
      text-align: center;
      padding: 48px 32px;
    }

    .empty-state-icon {
      font-size: 64px;
      margin-bottom: 24px;
      opacity: 0.6;
    }

    .empty-state-text {
      font-size: var(--md-sys-typescale-body-large);
      line-height: 1.6;
      margin: 0;
      letter-spacing: 0.15px;
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
					(msg) => html`<message-bubble .message=${msg}></message-bubble>`,
				)}
      </div>
    `;
	}

	updated(changedProperties: Map<string, any>) {
		super.updated(changedProperties);

		if (changedProperties.has("messages") && this.messages.length > 0) {
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
