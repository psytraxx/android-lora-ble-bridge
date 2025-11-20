import { LitElement, html, css } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import { sharedStylesheet } from '../shared-styles';

@customElement('success-toast')
export class SuccessToast extends LitElement {
  @property({ type: Boolean }) show = false;

  static styles = [sharedStylesheet, css`
    :host {
      display: contents;
    }

    @keyframes slideIn {
      from {
        transform: translate(-50%, -60%);
        opacity: 0;
      }
      to {
        transform: translate(-50%, -50%);
        opacity: 1;
      }
    }

    .toast-wrapper {
      animation: slideIn 0.5s ease-out;
    }
  `];

  private hideTimeout?: number;

  updated(changedProperties: Map<string, any>) {
    if (changedProperties.has('show') && this.show) {
      // Auto-hide after 3 seconds
      if (this.hideTimeout) {
        clearTimeout(this.hideTimeout);
      }
      this.hideTimeout = window.setTimeout(() => {
        this.show = false;
        this.dispatchEvent(new CustomEvent('hide', {
          bubbles: true,
          composed: true
        }));
      }, 3000);
    }
  }

  disconnectedCallback() {
    super.disconnectedCallback();
    if (this.hideTimeout) {
      clearTimeout(this.hideTimeout);
    }
  }

  render() {
    if (!this.show) {
      return html``;
    }

    return html`
      <div class="toast toast-center toast-middle z-[9999]">
        <div class="alert alert-success shadow-lg toast-wrapper min-w-[300px] max-w-[400px]">
          <div class="flex flex-col items-center gap-2 w-full">
            <svg xmlns="http://www.w3.org/2000/svg" class="w-12 h-12 stroke-current" fill="none" viewBox="0 0 24 24">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 13l4 4L19 7" />
            </svg>
            <div class="text-center">
              <h3 class="font-bold text-lg">Connected!</h3>
              <p class="text-sm opacity-90">Your device is ready to use</p>
            </div>
            <div class="alert alert-info text-xs mt-2 w-full">
              <svg xmlns="http://www.w3.org/2000/svg" class="w-4 h-4 shrink-0" fill="none" viewBox="0 0 24 24" stroke="currentColor">
                <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
              </svg>
              <span>Type a message below to get started!</span>
            </div>
          </div>
        </div>
      </div>
    `;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'success-toast': SuccessToast;
  }
}
