/**
 * Message Input Web Component
 * Matches Android TextField + IconButton design
 */

import { css, html, LitElement } from "lit";
import { customElement, property, query, state } from "lit/decorators.js";
import {
	calculateMessageSize,
	getUnsupportedChars,
	isValidText,
	MAX_TEXT_LENGTH,
} from "../protocol";

@customElement("message-input")
export class MessageInput extends LitElement {
	@property({ type: Boolean }) disabled = false;
	@property({ type: Boolean }) hasGps = false;
	@state() private text = "";
	@query("input") input!: HTMLInputElement;

	static styles = css`
    :host {
      display: block;
      padding: 8px;
      background: var(--md-sys-color-surface);
      border-top: 1px solid var(--md-sys-color-outline-variant);
    }

    .input-row {
      display: flex;
      gap: 8px;
      align-items: center;
    }

    .input-wrapper {
      flex: 1;
      position: relative;
    }

    /* TextField - matches Android Material3 */
    input {
      width: 100%;
      padding: 16px;
      border: 1px solid var(--md-sys-color-outline);
      border-radius: 4px;
      background: var(--md-sys-color-surface);
      color: var(--md-sys-color-on-surface);
      font-size: 16px;
      font-family: inherit;
      outline: none;
      transition: border-color 0.2s;
    }

    input:focus {
      border-color: var(--md-sys-color-primary);
      border-width: 2px;
      padding: 15px;
    }

    input:disabled {
      background: var(--md-sys-color-surface-variant);
      color: var(--md-sys-color-on-surface);
      opacity: 0.38;
      cursor: not-allowed;
    }

    .supporting-text {
      font-size: 12px;
      color: var(--md-sys-color-on-surface-variant);
      margin: 4px 16px 0;
    }

    .supporting-text.error {
      color: var(--md-sys-color-error);
    }

    /* IconButton - matches Android */
    .send-btn {
      width: 48px;
      height: 48px;
      border: none;
      border-radius: 24px;
      background: var(--md-sys-color-primary);
      color: var(--md-sys-color-on-primary);
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
      transition: all 0.2s;
    }

    .send-btn:hover:not(:disabled) {
      box-shadow: 0 2px 4px rgba(0,0,0,0.2);
    }

    .send-btn:active:not(:disabled) {
      transform: scale(0.92);
    }

    .send-btn:disabled {
      background: var(--md-sys-color-on-surface);
      opacity: 0.12;
      cursor: not-allowed;
    }

    .send-icon {
      width: 24px;
      height: 24px;
    }
  `;

	render() {
		const charCount = this.text.length;
		const messageSize =
			charCount > 0 ? calculateMessageSize(this.text, this.hasGps) : 0;
		const isValid = charCount === 0 || isValidText(this.text);
		const canSend =
			charCount > 0 &&
			charCount <= MAX_TEXT_LENGTH &&
			isValid &&
			!this.disabled;

		let counterClass = "";
		if (charCount > MAX_TEXT_LENGTH) {
			counterClass = "error";
		} else if (charCount > MAX_TEXT_LENGTH * 0.8) {
			counterClass = "warning";
		}

		const unsupportedChars = !isValid ? getUnsupportedChars(this.text) : [];

		const supportingText = unsupportedChars.length > 0
			? `Unsupported characters: ${unsupportedChars.join(", ")}`
			: `${charCount}/${MAX_TEXT_LENGTH} chars${messageSize > 0 ? ` (${messageSize} B)` : ''}`;

		return html`
      <div class="input-row">
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
          ${supportingText ? html`
            <div class="supporting-text ${!isValid || charCount > MAX_TEXT_LENGTH ? 'error' : ''}">
              ${supportingText}
            </div>
          ` : ''}
        </div>
        <button class="send-btn" @click=${this.onSend} ?disabled=${!canSend} title="Send">
          <svg class="send-icon" viewBox="0 -960 960 960" fill="currentColor">
            <path d="M120-160v-640l760 320-760 320Zm80-120 474-200-474-200v140l240 60-240 60v140Zm0 0v-400 400Z"/>
          </svg>
        </button>
      </div>
    `;
	}

	private onInput(e: Event) {
		this.text = (e.target as HTMLInputElement).value;
	}

	private onKeyDown(e: KeyboardEvent) {
		if (e.key === "Enter" && !e.shiftKey) {
			e.preventDefault();
			this.onSend();
		}
	}

	private onSend() {
		if (this.text.trim().length === 0) return;

		const text = this.text.trim();

		this.dispatchEvent(
			new CustomEvent("send", {
				detail: { text },
				bubbles: true,
				composed: true,
			}),
		);

		this.text = "";
		if (this.input) {
			this.input.value = "";
		}
	}
}
