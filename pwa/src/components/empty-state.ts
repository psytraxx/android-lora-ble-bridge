import { css, html, LitElement } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import { sharedStylesheet } from '../shared-styles';

@customElement('empty-state')
export class EmptyState extends LitElement {
  /** Device is already paired and we're waiting for it to wake up */
  @property({ type: Boolean }) waiting = false;

  static styles = [
    sharedStylesheet,
    css`
    :host {
      display: flex;
      flex-direction: column;
      align-items: center;
      justify-content: center;
      height: 100%;
    }
  `
  ];

  private handleConnect() {
    this.dispatchEvent(
      new CustomEvent('connect-requested', {
        bubbles: true,
        composed: true
      })
    );
  }

  render() {
    if (this.waiting) {
      return html`
        <div class="text-center p-8">
          <span class="loading loading-spinner loading-lg mb-4"></span>

          <h1 class="text-3xl font-bold mb-4">Waiting for your device</h1>

          <p class="text-base-content/70 max-w-md">
            Your device is paired but asleep. Press the button on the device to
            wake it &mdash; the app will reconnect automatically.
          </p>
        </div>
      `;
    }

    return html`
      <div class="text-center p-8">
        <h1 class="text-3xl font-bold mb-4">Connect Your Device</h1>

        <p class="text-base-content/70 mb-8 max-w-md">
          Connect your ESP32 LoRa device via Bluetooth to start sending and receiving messages.
          You only need to pair once &mdash; after that the app reconnects on its own whenever the
          device wakes up.
        </p>

        <button
          @click=${this.handleConnect}
          class="btn btn-primary btn-lg"
        >
          Connect Bluetooth Device
        </button>
      </div>
    `;
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'empty-state': EmptyState;
  }
}
