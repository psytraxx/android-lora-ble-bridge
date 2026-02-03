/**
 * Theme Switcher Web Component
 * Uses DaisyUI theme data attributes
 */

import { html, LitElement } from 'lit';
import { customElement, state } from 'lit/decorators.js';
import { sharedStylesheet } from '../shared-styles';
import { moonIcon, sunIcon } from '../utils/icons';

@customElement('theme-switcher')
export class ThemeSwitcher extends LitElement {
  @state() private theme: 'light' | 'dark' = 'light';

  static styles = [sharedStylesheet];

  connectedCallback() {
    super.connectedCallback();

    // Load saved theme or detect system preference
    const savedTheme = localStorage.getItem('theme');
    if (savedTheme === 'light' || savedTheme === 'dark') {
      this.theme = savedTheme;
    } else {
      const prefersDark = window.matchMedia('(prefers-color-scheme: dark)').matches;
      this.theme = prefersDark ? 'dark' : 'light';
    }

    this.applyTheme();
  }

  render() {
    const isDark = this.theme === 'dark';

    return html`
      <label class="swap swap-rotate">
        <input
          type="checkbox"
          class="theme-controller"
          .checked=${isDark}
          @change=${this.toggleTheme}
          aria-label="Toggle theme"
        />

        <!-- Sun icon (light mode) -->
        <span class="swap-off">${sunIcon()}</span>

        <!-- Moon icon (dark mode) -->
        <span class="swap-on">${moonIcon()}</span>
      </label>
    `;
  }

  private toggleTheme() {
    this.theme = this.theme === 'light' ? 'dark' : 'light';
    this.applyTheme();
    localStorage.setItem('theme', this.theme);
  }

  private applyTheme() {
    document.documentElement.setAttribute('data-theme', this.theme);
  }
}
