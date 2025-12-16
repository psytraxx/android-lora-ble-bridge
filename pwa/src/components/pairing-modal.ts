import { css, html, LitElement } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import { sharedStylesheet } from '../shared-styles';

@customElement('pairing-modal')
export class PairingModal extends LitElement {
  @property({ type: Boolean }) open = false;

  static styles = [
    sharedStylesheet,
    css`
    :host {
      display: contents;
    }

    .modal-box {
      max-width: 32rem;
      max-height: 90vh;
      overflow-y: auto;
    }
  `
  ];

  private handleClose() {
    this.open = false;
    this.dispatchEvent(
      new CustomEvent('modal-closed', {
        bubbles: true,
        composed: true
      })
    );
  }

  private handleProceed() {
    this.open = false;
    this.dispatchEvent(
      new CustomEvent('proceed-to-pair', {
        bubbles: true,
        composed: true
      })
    );
  }

  render() {
    if (!this.open) {
      return html``;
    }

    return html`
      <dialog class="modal modal-open">
        <div class="modal-box">
          <div class="flex justify-center mb-6">
            <svg class="w-20 h-20 text-primary" xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke="currentColor">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M8.111 16.404a5.5 5.5 0 017.778 0M12 20h.01m-7.08-7.071c3.904-3.905 10.236-3.905 14.141 0M1.394 9.393c5.857-5.857 15.355-5.857 21.213 0" />
            </svg>
          </div>

          <h2 class="text-2xl font-bold mb-4 text-center">Let's Connect Your Device</h2>

          <div class="my-6 space-y-4">
            <div class="flex gap-3 items-start">
              <div class="badge badge-primary shrink-0">1</div>
              <div class="flex-1 text-[15px] leading-relaxed">
                Make sure your <strong class="font-semibold">ESP32 device is powered on</strong> and within range (about 10 meters)
              </div>
            </div>

            <div class="flex gap-3 items-start">
              <div class="badge badge-primary shrink-0">2</div>
              <div class="flex-1 text-[15px] leading-relaxed">
                Click the <strong class="font-semibold">"Got it, Let's Pair!"</strong> button below
              </div>
            </div>

            <div class="flex gap-3 items-start">
              <div class="badge badge-primary shrink-0">3</div>
              <div class="flex-1 text-[15px] leading-relaxed">
                Look for a device named <span class="badge badge-neutral font-mono text-sm">ESP32S3-LoRa</span> in the browser dialog
              </div>
            </div>

            <div class="flex gap-3 items-start">
              <div class="badge badge-primary shrink-0">4</div>
              <div class="flex-1 text-[15px] leading-relaxed">
                Select it and click <strong class="font-semibold">"Pair"</strong>
              </div>
            </div>
          </div>

          <div class="alert alert-warning mt-6">
            <svg xmlns="http://www.w3.org/2000/svg" class="w-5 h-5 shrink-0" fill="none" viewBox="0 0 24 24" stroke="currentColor">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z" />
            </svg>
            <div>
              <h3 class="font-semibold">Don't See Your Device?</h3>
              <ul class="text-sm mt-1 space-y-1 ml-5 list-disc">
                <li>Check that your device is powered on</li>
                <li>Move closer to your device (within 10 meters)</li>
                <li>Try closing and reopening the pairing dialog</li>
                <li>Make sure no other app is connected to it</li>
              </ul>
            </div>
          </div>

          <div class="modal-action justify-center">
            <button @click=${this.handleProceed} class="btn btn-primary">
              Got it, Let's Pair!
            </button>
          </div>
        </div>
        <form method="dialog" class="modal-backdrop">
          <button @click=${this.handleClose}>close</button>
        </form>
      </dialog>
    `;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'pairing-modal': PairingModal;
  }
}
