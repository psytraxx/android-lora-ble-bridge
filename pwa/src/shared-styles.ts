import sharedCss from './style.css?inline';

// Create and export a single constructable stylesheet instance
export const sharedStylesheet = new CSSStyleSheet();
sharedStylesheet.replace(sharedCss);
