/**
 * Settings Menu Web Component
 *
 * Navbar dropdown holding the auto-reconnect preference and the remembered
 * device, so a user testing multiple boards can switch reconnect off and
 * forget a pairing without those actions being tangled into Disconnect.
 */

import { html, LitElement } from 'lit';
import { customElement, property } from 'lit/decorators.js';
import type { KnownDevice } from '../services/BleService';
import { sharedStylesheet } from '../shared-styles';
import { ellipsisVerticalIcon, exclamationTriangleIcon, trashIcon } from '../utils/icons';

@customElement('settings-menu')
export class SettingsMenu extends LitElement {
  @property({ type: Boolean }) autoReconnect = true;
  @property({ attribute: false }) knownDevice: KnownDevice | null = null;
  @property({ type: Boolean }) supportsPersistentDevices = true;

  static styles = [sharedStylesheet];

  render() {
    return html`
      <div class="dropdown dropdown-end">
        <div tabindex="0" role="button" class="btn btn-ghost btn-sm btn-circle" title="Settings">
          ${ellipsisVerticalIcon('w-5 h-5')}
        </div>
        <div
          tabindex="0"
          class="dropdown-content z-20 menu p-4 shadow-lg bg-base-200 rounded-box w-72 gap-3"
        >
          <label class="flex items-center justify-between gap-3 cursor-pointer">
            <span class="text-sm font-medium">Auto-reconnect</span>
            <input
              type="checkbox"
              class="toggle toggle-primary"
              .checked=${this.autoReconnect}
              @change=${this.onToggleChange}
            />
          </label>

          <div class="text-xs text-base-content/70">
            ${
              this.knownDevice
                ? html`Remembers
                  <span class="font-medium text-base-content"
                    >${this.knownDevice.name || this.knownDevice.id}</span
                  >`
                : 'No device remembered yet.'
            }
          </div>

          <button
            class="btn btn-outline btn-error btn-sm gap-2"
            ?disabled=${!this.knownDevice}
            @click=${this.onForgetClick}
          >
            ${trashIcon('w-4 h-4')} Forget device
          </button>

          ${
            !this.supportsPersistentDevices
              ? html`
                <div role="alert" class="alert alert-warning alert-soft text-xs p-3">
                  ${exclamationTriangleIcon('w-4 h-4 shrink-0')}
                  <span>
                    This browser can't remember pairings across reloads. Enable
                    <code class="font-mono">chrome://flags/#enable-web-bluetooth-new-permissions-backend</code>
                    for zero-tap reconnect.
                  </span>
                </div>
              `
              : ''
          }
        </div>
      </div>
    `;
  }

  private onToggleChange(e: Event) {
    const checked = (e.target as HTMLInputElement).checked;
    this.dispatchEvent(
      new CustomEvent('auto-reconnect-changed', {
        detail: { enabled: checked },
        bubbles: true,
        composed: true
      })
    );
  }

  private onForgetClick() {
    this.dispatchEvent(
      new CustomEvent('forget-device', {
        bubbles: true,
        composed: true
      })
    );
  }
}

declare global {
  interface HTMLElementTagNameMap {
    'settings-menu': SettingsMenu;
  }
}
