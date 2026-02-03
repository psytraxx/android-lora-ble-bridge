/**
 * Toast Service
 * Uses DaisyUI toast component for notifications
 */

import { getIconHtml } from '../utils/icons';

export type ToastType = 'info' | 'success' | 'warning' | 'error';

class ToastService {
  private container: HTMLDivElement | null = null;

  constructor() {
    this.initContainer();
  }

  private initContainer() {
    // Create toast container on body
    this.container = document.createElement('div');
    this.container.className = 'toast toast-center toast-middle z-1000';
    document.body.appendChild(this.container);
  }

  show(message: string, type: ToastType = 'info', duration = 3000) {
    if (!this.container) {
      this.initContainer();
    }

    const alert = document.createElement('div');
    const alertClass = `alert alert-${type}`;
    alert.className = alertClass;

    // Add icon based on type
    const icon = this.getIcon(type);
    alert.innerHTML = `
      ${icon}
      <span>${message}</span>
    `;

    this.container?.appendChild(alert);

    // Auto-remove after duration
    if (duration > 0) {
      setTimeout(() => {
        alert.classList.add('opacity-0', 'transition-opacity', 'duration-300');
        setTimeout(() => {
          alert.remove();
        }, 300);
      }, duration);
    }
  }

  private getIcon(type: ToastType): string {
    switch (type) {
      case 'success':
        return getIconHtml('checkCircle', 'shrink-0 w-6 h-6');
      case 'warning':
        return getIconHtml('exclamationTriangle', 'shrink-0 w-6 h-6');
      case 'error':
        return getIconHtml('xCircle', 'shrink-0 w-6 h-6');
      default:
        return getIconHtml('informationCircle', 'shrink-0 w-6 h-6');
    }
  }

  clear() {
    if (this.container) {
      this.container.innerHTML = '';
    }
  }
}

export const toastService = new ToastService();
