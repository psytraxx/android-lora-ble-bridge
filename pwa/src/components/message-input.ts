/**
 * Message Input Web Component
 */

import { css, html, LitElement } from "lit";
import { customElement, property, query, state } from "lit/decorators.js";
import {
	calculateMessageSize,
	getUnsupportedChars,
	isValidText,
	MAX_TEXT_LENGTH,
} from "../protocol";

// Import Material Web Components
import '@material/web/textfield/filled-text-field.js';
import '@material/web/iconbutton/filled-icon-button.js';
import type { MdFilledTextField } from '@material/web/textfield/filled-text-field.js';

@customElement("message-input")
export class MessageInput extends LitElement {
	@property({ type: Boolean }) disabled = false;
	@property({ type: Boolean }) hasGps = false;
	@state() private text = "";
	@query("md-filled-text-field") input!: MdFilledTextField;

	static styles = css`
    :host {
      display: block;
      padding: 12px 16px;
      background: var(--md-sys-color-surface);
      border-top: 1px solid var(--md-sys-color-outline-variant);
    }

    .input-container {
      display: flex;
      gap: 8px;
      align-items: flex-end;
    }

    .input-wrapper {
      flex: 1;
      display: flex;
      flex-direction: column;
      gap: 4px;
    }

    md-filled-text-field {
      width: 100%;
    }

    .counter {
      font-size: var(--md-sys-typescale-label-small);
      color: var(--md-sys-color-on-surface-variant);
      padding: 0 16px;
      display: flex;
      gap: 8px;
    }

    .counter.warning {
      color: var(--md-sys-color-tertiary);
    }

    .counter.error {
      color: var(--md-sys-color-error);
    }

    md-filled-icon-button {
      margin-bottom: 8px;
    }

    .error-message {
      color: var(--md-sys-color-error);
      font-size: var(--md-sys-typescale-label-small);
      padding: 0 16px;
      margin-top: 4px;
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

		const supportingText = charCount > 0
			? `${charCount}/${MAX_TEXT_LENGTH} chars ${messageSize > 0 ? `(${messageSize} B)` : ''}`
			: '';

		const errorText = unsupportedChars.length > 0
			? `Unsupported characters: ${unsupportedChars.join(", ")}`
			: charCount > MAX_TEXT_LENGTH
			? `Maximum ${MAX_TEXT_LENGTH} characters`
			: '';

		return html`
      <div class="input-container">
        <div class="input-wrapper">
          <md-filled-text-field
            label="Type a message"
            .value=${this.text}
            @input=${this.onInput}
            @keydown=${this.onKeyDown}
            ?disabled=${this.disabled}
            maxlength="${MAX_TEXT_LENGTH + 10}"
            supporting-text="${supportingText}"
            error-text="${errorText}"
            ?error=${!isValid || charCount > MAX_TEXT_LENGTH}
          >
          </md-filled-text-field>
        </div>
        <md-filled-icon-button @click=${this.onSend} ?disabled=${!canSend}>
          <svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 -960 960 960" width="24" fill="currentColor">
            <path d="M120-160v-640l760 320-760 320Zm80-120 474-200-474-200v140l240 60-240 60v140Zm0 0v-400 400Z"/>
          </svg>
        </md-filled-icon-button>
      </div>
    `;
	}

	private onInput(e: Event) {
		this.text = (e.target as MdFilledTextField).value;
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
