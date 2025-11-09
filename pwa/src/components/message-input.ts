/**
 * Message Input Web Component
 */

import { LitElement, html, css } from 'lit';
import { customElement, property, state, query } from 'lit/decorators.js';
import { isValidText, getUnsupportedChars, calculateMessageSize, MAX_TEXT_LENGTH } from '../protocol';

@customElement('message-input')
export class MessageInput extends LitElement {
  @property({ type: Boolean }) disabled = false;
  @property({ type: Boolean }) hasGps = false;
  @state() private text = '';
  @query('input') input!: HTMLInputElement;

  static styles = css`
    :host {
      display: block;
      padding: 16px;
      background: var(--surface-color, white);
      border-top: 1px solid var(--divider-color, #e0e0e0);
    }

    .input-container {
      display: flex;
      gap: 12px;
      align-items: flex-end;
    }

    .input-wrapper {
      flex: 1;
      display: flex;
      flex-direction: column;
      gap: 4px;
    }

    input {
      width: 100%;
      padding: 12px 16px;
      border: 2px solid var(--divider-color, #e0e0e0);
      border-radius: 24px;
      font-size: 15px;
      font-family: inherit;
      outline: none;
      transition: border-color 0.2s;
    }

    input:focus {
      border-color: var(--primary-color, #1976d2);
    }

    input:disabled {
      background: #f5f5f5;
      cursor: not-allowed;
    }

    .counter {
      font-size: 12px;
      color: var(--text-secondary-color, #757575);
      padding: 0 16px;
      display: flex;
      gap: 8px;
    }

    .counter.warning {
      color: #f57c00;
    }

    .counter.error {
      color: #d32f2f;
    }

    button {
      padding: 12px 24px;
      background: var(--primary-color, #1976d2);
      color: white;
      border: none;
      border-radius: 24px;
      font-size: 15px;
      font-weight: 500;
      cursor: pointer;
      transition: background 0.2s, transform 0.1s;
      white-space: nowrap;
    }

    button:hover:not(:disabled) {
      background: var(--primary-dark-color, #1565c0);
    }

    button:active:not(:disabled) {
      transform: scale(0.98);
    }

    button:disabled {
      background: #bdbdbd;
      cursor: not-allowed;
    }

    .error-message {
      color: #d32f2f;
      font-size: 12px;
      padding: 0 16px;
      margin-top: 4px;
    }
  `;

  render() {
    const charCount = this.text.length;
    const messageSize = charCount > 0 ? calculateMessageSize(this.text, this.hasGps) : 0;
    const isValid = charCount === 0 || isValidText(this.text);
    const canSend = charCount > 0 && charCount <= MAX_TEXT_LENGTH && isValid && !this.disabled;

    let counterClass = '';
    if (charCount > MAX_TEXT_LENGTH) {
      counterClass = 'error';
    } else if (charCount > MAX_TEXT_LENGTH * 0.8) {
      counterClass = 'warning';
    }

    const unsupportedChars = !isValid ? getUnsupportedChars(this.text) : [];

    return html`
      <div class="input-container">
        <div class="input-wrapper">
          <input
            type="text"
            placeholder="Type a message..."
            .value=${this.text}
            @input=${this.onInput}
            @keydown=${this.onKeyDown}
            ?disabled=${this.disabled}
            maxlength="${MAX_TEXT_LENGTH + 10}"
          />
          <div class="counter ${counterClass}">
            <span>${charCount}/${MAX_TEXT_LENGTH} chars</span>
            ${messageSize > 0 ? html`<span>(${messageSize} B)</span>` : ''}
          </div>
          ${unsupportedChars.length > 0 ? html`
            <div class="error-message">
              Unsupported characters: ${unsupportedChars.join(', ')}
            </div>
          ` : ''}
        </div>
        <button @click=${this.onSend} ?disabled=${!canSend}>
          Send
        </button>
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

    this.dispatchEvent(new CustomEvent('send', {
      detail: { text },
      bubbles: true,
      composed: true
    }));

    this.text = '';
    this.input.value = '';
  }
}
