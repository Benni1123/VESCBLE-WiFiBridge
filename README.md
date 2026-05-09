# VescBLEBridge

ESP32-S3 BLE/WiFi bridge for VESC motor controllers with web configuration.

Connects a VESC motor controller via UART and exposes it over both Bluetooth Low Energy and WiFi TCP — fully compatible with VESC Tool on Android, iOS, and desktop.

---

## Features

**Connectivity**
- BLE bridge using VESC NUS UUIDs — appears natively in VESC Tool
- WiFi TCP bridge on configurable port (default 65101)
- Supports up to 10 WiFi networks with automatic failover
- Fallback Access Point mode with captive portal if no network is available
- Static IP support per WiFi network

**VESC Integration**
- Passes all VESC Tool commands transparently to the controller
- Periodic VESC status polling (voltage, FET temp, motor temp, fault code)
- Polling only active when web interface is open — no unnecessary UART traffic
- Polling paused automatically when VESC Tool is connected

**Web Interface**
- Responsive dark/light web UI (auto-detects browser theme, manual toggle)
- English and German language support (auto-detects browser language, manual toggle)
- Info tab: live status, VESC data, uptime, build version
- Config tab: BLE name, AP settings, TCP port, UART pins, WiFi networks, update server URLs
- OTA Flash tab: server update check with version comparison, manual firmware upload via drag & drop
<img width="753" height="775" alt="grafik" src="https://github.com/user-attachments/assets/e75c4ac6-92cc-47fa-a4a9-eb6913f9646d" />
<img width="735" height="1821" alt="grafik" src="https://github.com/user-attachments/assets/6ffaca73-7bfc-445a-b288-c63fafa40f26" />
<img width="757" height="785" alt="grafik" src="https://github.com/user-attachments/assets/d7d52651-041b-4eac-9adc-c0501e360286" />


**OTA Updates**
- Server-based OTA: checks version.txt, installs firmware.bin — supports HTTP and HTTPS (GitHub Releases)
- Manual OTA: drag & drop firmware.bin directly in the browser
- Emergency OTA via curl if web interface is unavailable:
  ```
  curl.exe -X POST http://<ip>/update -F "firmware=@firmware.bin"
  ```
- Build version auto-increments patch number on every compile

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
# Clone the repository
git clone https://github.com/Benni1123/VESCBLE-WiFiBridge.git
cd VescBLEBridge

# Build and flash
pio run --target upload
```

**First Boot**
1. ESP starts in Access Point mode: connect to `VESC-BLE-WiFi`
2. Open `http://192.168.4.1` in your browser
3. Go to Config → add your WiFi network → Save & Restart
4. Connect VESC Tool via BLE or WiFi TCP

---

## Configuration

All settings are stored in NVS (non-volatile storage) and persist across reboots and OTA updates.

| Setting | Default | Description |
|---------|---------|-------------|
| BLE Name | `VESC-BLE-WiFi` | Device name visible in VESC Tool |
| AP SSID | `VESC-BLE-WiFi` | Fallback access point name |
| TCP Port | `65101` | VESC Tool TCP connection port |
| UART RX Pin | `6` | GPIO pin for UART RX |
| UART TX Pin | `5` | GPIO pin for UART TX |
| VESC Polling | enabled | Read VESC data when web UI is open |
| Version URL | — | URL to version.txt for OTA checks |
| Firmware URL | — | URL to firmware.bin for OTA install |

---

## OTA Update Server

Place two files on any HTTP/HTTPS server:

```
version.txt   →  1.0.1
firmware.bin  →  compiled binary
```

For GitHub Releases, use:
```
https://github.com/Benni1123/VESCBLE-WiFiBridge/releases/latest/download/firmware.bin
https://github.com/Benni1123/VESCBLE-WiFiBridge/releases/latest/download/version.txt
```

The build script (`generate_version.py`) automatically creates `version.txt` and increments the patch version in `version.h` after each successful build.

---

## Project Structure

```
src/
  main.cpp          — firmware
  version.h         — firmware version (auto-incremented)
generate_version.py — post-build script
platformio.ini      — build configuration
```

---

## License

MIT
