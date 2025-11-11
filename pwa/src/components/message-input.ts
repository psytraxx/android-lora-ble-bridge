/**
 * Message Input Web Component
 * Uses DaisyUI input and button components
 */

import { html, LitElement } from 'lit';
import { customElement, property, query, state } from 'lit/decorators.js';
import {
  calculateMessageSize,
  getUnsupportedChars,
  isValidText,
  MAX_TEXT_LENGTH
} from '../protocol';

@customElement('message-input')
export class MessageInput extends LitElement {
  @property({ type: Boolean }) disabled = false;
  @property({ type: Boolean }) hasGps = false;
  @state() private text = '';
  @query('input') input!: HTMLInputElement;

  // Disable shadow DOM to allow Tailwind classes to work
  createRenderRoot() {
    return this;
  }

  render() {
    const charCount = this.text.length;
    const messageSize = charCount > 0 ? calculateMessageSize(this.text, this.hasGps) : 0;
    const isValid = charCount === 0 || isValidText(this.text);
    const canSend = charCount > 0 && charCount <= MAX_TEXT_LENGTH && isValid && !this.disabled;

    const unsupportedChars = !isValid ? getUnsupportedChars(this.text) : [];

    const supportingText =
      unsupportedChars.length > 0
        ? `Unsupported characters: ${unsupportedChars.join(', ')}`
        : `${charCount}/${MAX_TEXT_LENGTH} chars${messageSize > 0 ? ` (${messageSize} B)` : ''}`;

    return html`
      <div class="p-4 bg-base-100 border-t border-base-300">
        <div class="flex gap-2 items-start">
          <div class="flex flex-col flex-1 gap-1">
            <input
              type="text"
              placeholder="Type a message..."
              class="input input-bordered w-full ${!isValid || charCount > MAX_TEXT_LENGTH ? 'input-error' : ''}"
              .value=${this.text}
              @input=${this.onInput}
              @keydown=${this.onKeyDown}
              ?disabled=${this.disabled}
              maxlength="${MAX_TEXT_LENGTH + 10}"
              aria-label="Message input"
            />
            <span class="text-xs ${!isValid || charCount > MAX_TEXT_LENGTH ? 'text-error' : 'text-base-content/70'}" role="status">
              ${supportingText}
            </span>
          </div>
          <button
            class="btn btn-primary btn-circle"
            @click=${this.onSend}
            ?disabled=${!canSend}
            aria-label="Send message"
          >
            <svg class="w-6 h-6" viewBox="0 -960 960 960" fill="currentColor">
              <path d="M120-160v-640l760 320-760 320Zm80-120 474-200-474-200v140l240 60-240 60v140Zm0 0v-400 400Z"/>
            </svg>
          </button>
        </div>
      </div>
    `;
  }

  private onInput(e: Event) {
    this.text = (e.target as HTMLInputElement).value;
  }

  private onKeyDown(e: KeyboardEvent) {
    if (e.key === 'Enter' && !e.shiftKey) {
      e.preventDefault();
      this.onSend();
    }
  }

  private onSend() {
    if (this.text.trim().length === 0) return;

    const text = this.text.trim();

    this.dispatchEvent(
      new CustomEvent('send', {
        detail: { text },
        bubbles: true,
        composed: true
      })
    );

    this.text = '';
    if (this.input) {
      this.input.value = '';
    }
  }
}
