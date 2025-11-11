/**
 * Toast Service
 * Uses DaisyUI toast component for notifications
 */

export type ToastType = 'info' | 'success' | 'warning' | 'error';

class ToastService {
  private container: HTMLDivElement | null = null;

  constructor() {
    this.initContainer();
  }

  private initContainer() {
    // Create toast container on body
    this.container = document.createElement('div');
    this.container.className = 'toast toast-center toast-bottom z-1000';
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
        return `<svg xmlns="http://www.w3.org/2000/svg" class="stroke-current shrink-0 h-6 w-6" fill="none" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12l2 2 4-4m6 2a9 9 0 11-18 0 9 9 0 0118 0z" /></svg>`;
      case 'warning':
        return `<svg xmlns="http://www.w3.org/2000/svg" class="stroke-current shrink-0 h-6 w-6" fill="none" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" /></svg>`;
      case 'error':
        return `<svg xmlns="http://www.w3.org/2000/svg" class="stroke-current shrink-0 h-6 w-6" fill="none" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M10 14l2-2m0 0l2-2m-2 2l-2-2m2 2l2 2m7-2a9 9 0 11-18 0 9 9 0 0118 0z" /></svg>`;
      default:
        return `<svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" class="stroke-current shrink-0 w-6 h-6"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 16h-1v-4h-1m1-4h.01M21 12a9 9 0 11-18 0 9 9 0 0118 0z"></path></svg>`;
    }
  }

  clear() {
    if (this.container) {
      this.container.innerHTML = '';
    }
  }
}

export const toastService = new ToastService();
