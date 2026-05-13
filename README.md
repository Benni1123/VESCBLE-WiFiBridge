# VescBLEBridge

ESP32-S3 BLE/WiFi bridge for VESC motor controllers with web configuration.

Connects a VESC motor controller via UART and exposes it over both Bluetooth Low Energy and WiFi TCP — fully compatible with VESC Tool on Android, iOS, and desktop.

## Flash Firmware

> **Chrome or Edge required** (Web Serial API)

<esp-web-install-button manifest="https://raw.githubusercontent.com/Benni1123/VESCBLE-WiFiBridge/main/manifest.json">
  <button slot="activate" style="padding:10px 20px;background:#00bcd4;color:#111;border:none;border-radius:6px;font-size:14px;font-weight:bold;cursor:pointer">⚡ Flash Firmware</button>
  <span slot="unsupported">Your browser does not support Web Serial. Use Chrome or Edge.</span>
</esp-web-install-button>
<script type="module" src="https://unpkg.com/esp-web-tools@10/dist/web/install-button.js"></script>

---

## Features

**Connectivity**
- BLE bridge using VESC NUS UUIDs — appears natively in VESC Tool
- WiFi TCP bridge on configurable port (default 65101)
- Supports up to 10 WiFi networks with automatic failover and static IP per network
- AP + Client simultaneous mode — Access Point always available alongside WiFi client
- `192.168.4.1` always reachable even when connected to home WiFi
- Captive portal for AP clients, DNS always running
- Fallback to AP-only mode if no configured network is reachable

**VESC Integration**
- Passes all VESC Tool commands transparently — zero modification
- Periodic VESC status polling (voltage, FET temp, motor temp, fault code)
- Polling only active when web UI is open — no unnecessary UART traffic
  
**Web Interface**
- Responsive dark/light web UI (auto-detects browser theme, manual toggle)
- English and German (auto-detects browser language, manual toggle)
- Info tab: live status, VESC data, free RAM, uptime, build version
- Config tab: configure BLE name, AP settings, TCP port, UART pins, WiFi networks, auto reboot, and OTA update URLs
- WiFi password show/hide toggle per network
- OTA Flash tab: server update check with version comparison, manual firmware upload via drag & drop
- API tab: full API reference, UART debug log with channel filter (BLE / WiFi / Poll)

**Reliability**
- Auto reboot configurable: triggers after N seconds with no BLE or WiFi client connected
- Optional: suppress reboot when device is connected to WiFi (even without active VESC Tool session)
- AP clients (e.g. phone connected to the AP) also count as active — no unwanted reboot

**OTA Updates**
- Server-based OTA: checks `version.txt`, installs `firmware.bin` — HTTP and HTTPS supported (GitHub Releases ready)
- Manual OTA: drag & drop `firmware.bin` in the browser
- Emergency OTA on port 8080 — always available, even if main web server is unresponsive:
  ```
  curl -X POST http://<ip>:8080/update -F "firmware=@firmware.bin"
  ```
- Build version auto-increments patch number on every compile

**Debug**
- UART debug log in the web UI — persists across reboots (stored in NVS)
- Per-channel filter: BLE traffic, WiFi traffic, VESC poll — enable individually
- Log accessible via `/api/uart/log`, clearable via `/api/uart/clear`

---

## Hardware

| Component | Details |
|-----------|---------|
| MCU | Waveshare ESP32-S3 Zero (ESP32-S3FH4R2) |
| UART RX | GPIO 6 (configurable) |
| UART TX | GPIO 5 (configurable) |
| Baud Rate | 115200 |

Wiring: connect UART TX/RX to the VESC COMM port. No level shifter needed — both run at 3.3V.

**Tested with**
- Spintend Ubox 85150 (VESC Firmware 6.06) via COMM port

---

## Getting Started

**Requirements**
- PlatformIO (VS Code extension or CLI)
- ESP32-S3 board

**Build & Flash**
```bash
git clone https://github.com/Benni1123/VESCBLE-WiFiBridge.git
cd VESCBLE-WiFiBridge
pio run --target upload
```

After build, two binaries are created in `.pio/build/esp32-s3/`:
- `firmware.bin` — for OTA updates on already-flashed devices
- `firmware_full.bin` — merged binary for first-time flashing (includes bootloader + partitions)

**First Boot**
1. ESP starts in Access Point mode: connect to `VESC-BLE-WiFi`
2. Open `http://192.168.4.1` in your browser
3. Go to Config → add your WiFi network → Save
4. Connect VESC Tool via BLE or WiFi TCP

---

## Configuration

All settings stored in NVS — persist across reboots and OTA updates.

| Setting | Default | Reboot required |
|---------|---------|----------------|
| BLE Name | `VESC-BLE-WiFi` | Yes |
| AP SSID / Password / Timeout | `VESC-BLE-WiFi` | Yes |
| TCP Port | `65101` | Yes |
| UART RX / TX Pin | `6` / `5` | Yes |
| VESC Polling | enabled | No |
| WiFi Networks (up to 10) | — | No |
| Auto Reboot | disabled | No |
| Update URLs | — | No |
| Debug Mode + Filter | disabled | No |

---

## API

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/info` | Device status, VESC data, WiFi/BLE state |
| GET | `/api/config` | All configuration values |
| GET | `/api/update/status` | Firmware version info |
| GET | `/api/update/check` | Fetch version.txt and compare |
| GET | `/api/wifi/scan` | Scan WiFi networks |
| GET | `/api/uart/log` | UART debug log |
| GET | `/api/ping` | Keepalive — activates VESC polling |
| POST | `/api/config` | Save configuration (JSON body) |
| POST | `/api/uart/clear` | Clear debug log |
| POST | `/api/update/install` | Download and flash from update URL |
| POST | `/api/restart` | Restart ESP |
| POST | `/api/factory-reset` | Clear NVS and restart |
| POST | `/update` | Manual OTA (multipart/form-data) |
| POST | `:8080/update` | Emergency OTA (always available) |

---

## OTA Update Server

Place two files on any HTTP/HTTPS server:

```
version.txt   →  1.0.1
firmware.bin  →  compiled binary
```

For GitHub Releases:
```
https://github.com/Benni1123/VESCBLE-WiFiBridge/releases/latest/download/firmware.bin
https://github.com/Benni1123/VESCBLE-WiFiBridge/releases/latest/download/version.txt
```

The build script (`generate_version.py`) automatically creates `version.txt` and increments the patch version in `version.h` after each successful build.

---

## Project Structure

```
src/
  main.cpp            — firmware
  version.h           — firmware version (auto-incremented)
generate_version.py   — post-build: increments version, writes version.txt
merge_firmware.py     — post-build: merges all binaries into firmware_full.bin
manifest.json         — ESP Web Tools manifest for browser flashing
platformio.ini        — build configuration
```

---

## License

MIT
