import { css, html, LitElement } from 'lit';
import { customElement } from 'lit/decorators.js';
import { sharedStylesheet } from '../shared-styles';

@customElement('empty-state')
export class EmptyState extends LitElement {
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
    return html`
      <div class="text-center p-8">
        <h1 class="text-3xl font-bold mb-4">Connect Your Device</h1>

        <p class="text-base-content/70 mb-8 max-w-md">
          Connect your ESP32 LoRa device via Bluetooth to start sending and receiving messages.
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
