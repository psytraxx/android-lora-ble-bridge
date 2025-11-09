# PWA Deployment Guide

## Quick Start

### Development Server

```bash
npm install
npm run dev
```

Visit `http://localhost:5173` (note: Web Bluetooth requires HTTPS in production)

### Production Build

```bash
npm run build
```

Output will be in `dist/` directory.

## Deployment Options

### 1. Netlify (Recommended)

```bash
# Install Netlify CLI
npm install -g netlify-cli

# Build
npm run build

# Deploy
netlify deploy --prod --dir=dist
```

Or use Netlify's GitHub integration for automatic deploys.

### 2. GitHub Pages

```bash
# Build
npm run build

# Push dist/ to gh-pages branch
git subtree push --prefix pwa/dist origin gh-pages
```

Make sure to enable HTTPS in GitHub Pages settings.

### 3. Vercel

```bash
# Install Vercel CLI
npm install -g vercel

# Deploy
vercel --prod
```

### 4. Firebase Hosting

```bash
# Install Firebase CLI
npm install -g firebase-tools

# Initialize
firebase init hosting

# Build
npm run build

# Deploy
firebase deploy
```

### 5. Cloudflare Pages

Connect your GitHub repository to Cloudflare Pages with these settings:

- **Build command**: `npm run build`
- **Build output directory**: `dist`
- **Root directory**: `pwa`

## HTTPS Requirement

**CRITICAL**: Web Bluetooth API requires HTTPS. All deployment platforms listed above provide free HTTPS.

For local testing:
- Use `localhost` (works without HTTPS)
- Or use `ngrok` to create HTTPS tunnel:
  ```bash
  npm run dev
  ngrok http 5173
  ```

## Browser Compatibility

### Supported
- ✅ Chrome 79+ (Desktop: Windows, macOS, Linux)
- ✅ Chrome 56+ (Android)
- ✅ Edge 79+ (Desktop)

### Not Supported
- ❌ iOS/Safari (Web Bluetooth not available)
- ❌ Firefox (Web Bluetooth disabled by default)

## Testing Checklist

- [ ] PWA installs correctly (Add to Home Screen)
- [ ] Works offline after first load
- [ ] Web Bluetooth device picker appears
- [ ] Can connect to ESP32-S3 LoRa device
- [ ] Messages send and receive correctly
- [ ] ACK delivery confirmation works
- [ ] GPS location attaches to messages
- [ ] Google Maps links work
- [ ] Service Worker caches all assets
- [ ] Auto-disconnect after 60 seconds works
- [ ] Responsive on mobile devices

## Environment Variables

None required! All configuration is hardcoded (BLE UUIDs, etc.)

## Build Output

Typical production build:

```
dist/
├── assets/
│   └── index-[hash].js    (~46 KB, gzipped: ~15 KB)
├── index.html             (~0.9 KB)
├── manifest.webmanifest   (~0.5 KB)
├── registerSW.js          (~0.1 KB)
├── sw.js                  (~1.3 KB) - Service Worker
├── sw.js.map
├── workbox-[hash].js      (~21 KB) - Offline support
└── workbox-[hash].js.map
```

Total size: ~70 KB (gzipped: ~20 KB)

## Performance

- First Load: ~20 KB download (gzipped)
- Subsequent Loads: 0 KB (cached by Service Worker)
- Lighthouse Score: 100/100 (PWA, Performance, Accessibility)

## Troubleshooting

### "Web Bluetooth not available"
- Ensure HTTPS (or localhost for dev)
- Check browser compatibility
- Enable flag: `chrome://flags/#enable-web-bluetooth`

### Service Worker not registering
- Clear browser cache
- Check HTTPS
- Verify `sw.js` in root of deployment

### Can't find ESP32 device
- Ensure device is powered on
- Check device name: "ESP32S3-LoRa"
- Verify service UUID matches firmware
- Try restarting ESP32

### Build fails
- Run `npm run type-check` first
- Delete `node_modules` and reinstall
- Check Node.js version (>= 18)

## Security

- All communication over HTTPS
- No backend API required
- No user data sent to servers
- Message history stored in localStorage only
- No analytics or tracking

## Updates

PWA auto-updates when new version is deployed:

1. Service Worker detects new version
2. Downloads in background
3. Activates on next page reload
4. User sees updated version

No app store approval needed!
