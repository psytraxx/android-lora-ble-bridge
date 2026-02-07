/**
 * Device Info Modal Web Component
 * Displays battery, signal quality, and LoRa configuration
 */

import { html, LitElement, nothing } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import type { DeviceInfo } from '../protocol/types';
import { sharedStylesheet } from '../shared-styles';

@customElement('device-info-modal')
export class DeviceInfoModal extends LitElement {
  @property({ type: Boolean }) open = false;
  @property({ type: Object }) info: DeviceInfo | null = null;
  @property({ type: Boolean }) loading = false;

  static styles = [sharedStylesheet];

  render() {
    if (!this.open) return nothing;

    return html`
      <dialog class="modal modal-open">
        <div class="modal-box">
          <h3 class="text-lg font-bold mb-4">Device Info</h3>

          ${this.loading
            ? html`<div class="flex justify-center py-8">
                <span class="loading loading-spinner loading-lg"></span>
              </div>`
            : this.info
              ? this.renderInfo(this.info)
              : html`<p class="text-base-content/60">No device info available</p>`
          }

          <div class="modal-action">
            <button class="btn" @click=${this.onClose}>Close</button>
          </div>
        </div>
        <form method="dialog" class="modal-backdrop">
          <button @click=${this.onClose}>close</button>
        </form>
      </dialog>
    `;
  }

  private renderInfo(info: DeviceInfo) {
    const batteryColor =
      info.batteryLevel > 50 ? 'text-success' :
      info.batteryLevel > 20 ? 'text-warning' : 'text-error';

    const freqMHz = (info.frequencyHz / 1_000_000).toFixed(2);
    const bwKHz = info.bandwidthHz / 1_000;

    return html`
      <div class="space-y-3">
        <div class="flex justify-between items-center">
          <span class="text-base-content/70">Battery</span>
          <span class="font-mono font-bold ${batteryColor}">${info.batteryLevel}%</span>
        </div>
        <div class="divider my-0"></div>
        <div class="flex justify-between items-center">
          <span class="text-base-content/70">RSSI</span>
          <span class="font-mono">${info.rssi} dBm</span>
        </div>
        <div class="flex justify-between items-center">
          <span class="text-base-content/70">SNR</span>
          <span class="font-mono">${info.snr.toFixed(2)} dB</span>
        </div>
        <div class="divider my-0"></div>
        <div class="flex justify-between items-center">
          <span class="text-base-content/70">Frequency</span>
          <span class="font-mono">${freqMHz} MHz</span>
        </div>
        <div class="flex justify-between items-center">
          <span class="text-base-content/70">Bandwidth</span>
          <span class="font-mono">${bwKHz} kHz</span>
        </div>
        <div class="flex justify-between items-center">
          <span class="text-base-content/70">Spreading Factor</span>
          <span class="font-mono">SF${info.spreadingFactor}</span>
        </div>
        <div class="flex justify-between items-center">
          <span class="text-base-content/70">Coding Rate</span>
          <span class="font-mono">4/${info.codingRate}</span>
        </div>
        <div class="flex justify-between items-center">
          <span class="text-base-content/70">TX Power</span>
          <span class="font-mono">${info.txPower} dBm</span>
        </div>
      </div>
    `;
  }

  private onClose() {
    this.dispatchEvent(
      new CustomEvent('modal-closed', {
        bubbles: true,
        composed: true
      })
    );
  }
}
