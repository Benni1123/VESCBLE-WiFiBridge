// VESC UART, TCP, BLE-Weiterleitung und Status-Polling
// Diese Datei wird ueber main.cpp als Unity-Build eingebunden.
// Dadurch bleiben die bisherigen static-Sichtbarkeiten und Abhaengigkeiten exakt erhalten,
// waehrend der Quellcode logisch in einzelne Dateien aufgeteilt ist.
#if defined(VESC_BRIDGE_UNITY_BUILD)
#include "globals.h"
#include "debuglog.h"
#include "wifi-ble.h"
#include "vesc.h"


// ── VESC parsing ──────────────────────────────────────────────────────────────
static float readFloat16(const uint8_t *buf, float scale) {
  int16_t val = ((int16_t)buf[0]<<8)|buf[1];
  return val / scale;
}

static int32_t readInt32(const uint8_t *buf) {
  return ((int32_t)buf[0]<<24) | ((int32_t)buf[1]<<16) | ((int32_t)buf[2]<<8) | (int32_t)buf[3];
}

static String vescFaultToString(int code) {
  switch(code) {
    case 0:  return "OK";
    case 1:  return "OVER_VOLTAGE";
    case 2:  return "UNDER_VOLTAGE";
    case 3:  return "DRV";
    case 4:  return "ABS_OVER_CURRENT";
    case 5:  return "OVER_TEMP_FET";
    case 6:  return "OVER_TEMP_MOTOR";
    case 7:  return "GATE_DRIVER_OVER_VOLTAGE";
    case 8:  return "MCU_UNDER_VOLTAGE";
    case 9:  return "BOOTING_FROM_WATCHDOG_RESET";
    case 10: return "GATE_DRIVER_UNDER_VOLTAGE";
    case 11: return "ENCODER_SPI_FAULT";
    case 12: return "ENCODER_SINCOS_BELOW_MIN_AMPLITUDE";
    case 13: return "ENCODER_SINCOS_ABOVE_MAX_AMPLITUDE";
    case 14: return "FLASH_CORRUPTION";
    case 15: return "HIGH_OFFSET_CURRENT_SENSOR_1";
    case 16: return "HIGH_OFFSET_CURRENT_SENSOR_2";
    case 17: return "HIGH_OFFSET_CURRENT_SENSOR_3";
    case 18: return "UNBALANCED_CURRENTS";
    case 19: return "BRK";
    case 20: return "RESOLVER_LOT";
    case 21: return "RESOLVER_DOS";
    case 22: return "RESOLVER_LOS";
    case 23: return "FLASH_CORRUPTION_APP_CFG";
    case 24: return "FLASH_CORRUPTION_MC_CFG";
    case 25: return "ENCODER_NO_MAGNET";
    case 26: return "ENCODER_MAGNET_TOO_STRONG";
    case 27: return "PHASE_FILTER";
    case 28: return "ENCODER_FAULT";
    case 29: return "LV_OUTPUT_FAULT";
    default: return "UNKNOWN_" + String(code);
  }
}

static void parseGetValues(const uint8_t *payload, size_t len) {
  if (len < 38) return;
  if (payload[0] != 0x04) return;
  vescStatus.tempFet   = readFloat16(payload + 1,  10.0f);
  vescStatus.tempMotor = readFloat16(payload + 3,  10.0f);
  vescStatus.erpm      = readInt32 (payload + 23);          // ERPM int32 big-endian
  vescStatus.voltage   = readFloat16(payload + 27, 10.0f);
  vescStatus.faultCode = payload[37];
  vescStatus.connected  = true;
  vescStatus.lastUpdate = millis();
}

static unsigned long lastVescPoll = 0;
static std::string   vescPollBuffer;
// Non-blocking Poll-State-Machine: nicht mehr bis zu 100ms in pollVesc()
// blockieren (das wuerde den Webserver/handleClient ausbremsen -> ESP zeitweise
// nicht erreichbar). Stattdessen Anfrage senden und die Antwort ueber mehrere
// loop()-Durchlaeufe hinweg einsammeln.
static bool          vescPollWaiting  = false;   // warten wir gerade auf Antwort?
static unsigned long vescPollSentAt   = 0;       // wann wurde die Anfrage gesendet?
static const unsigned long VESC_POLL_RESP_TIMEOUT_MS = 100;

bool webUiActive() {
  return (millis() - lastBrowserPing < 5000);
}

void pollVesc() {
  unsigned long now = millis();

  if (wifiClient && wifiClient.connected()) return;
  if (deviceConnected) return;

  // ── Antwort-Phase: warten wir bereits auf eine VESC-Antwort? ──────────────
  // Non-blocking: jeden loop()-Durchlauf die verfuegbaren UART-Bytes einsammeln,
  // NICHT blockierend warten. Fertig, sobald das Endbyte (0x03) da ist oder der
  // Timeout abgelaufen ist. Waehrenddessen laeuft der Webserver ungestoert weiter.
  if (vescPollWaiting) {
    while (Serial1.available()) {
      vescPollBuffer.push_back(Serial1.read());
    }
    bool complete = (vescPollBuffer.size() > 5 && vescPollBuffer.back() == 0x03);
    bool timedOut = (now - vescPollSentAt >= VESC_POLL_RESP_TIMEOUT_MS);
    if (!complete && !timedOut) return;   // weiter sammeln, spaeter erneut rein

    // Auswertung (identisch zur frueheren blockierenden Version).
    if (vescPollBuffer.size() > 5 && vescPollBuffer[0] == 0x02 && vescPollBuffer.back() == 0x03) {
      uint8_t plen = vescPollBuffer[1];
      if (cfg_debug && (cfg_debug_filter & 4)) { String h="POLL<=VESC: ";for(size_t i=0;i<min(vescPollBuffer.size(),(size_t)40);i++){char x[4];snprintf(x,4,"%02X ",(uint8_t)vescPollBuffer[i]);h+=x;} uartLogAdd(h); }
      if (vescPollBuffer.size() >= (size_t)(plen + 4)) {
        parseGetValues((const uint8_t*)vescPollBuffer.data() + 2, plen);
      }
    } else {
      if (cfg_debug && (cfg_debug_filter & 4)) uartLogAdd("POLL<=VESC: no response ("+String(vescPollBuffer.size())+" bytes)");
      if (now - vescStatus.lastUpdate > 6000) {
        vescStatus.connected = false;
      }
    }
    vescPollWaiting = false;   // Zyklus abgeschlossen
    return;
  }

  // ── Anfrage-Phase: pruefen, ob ueberhaupt gepollt werden soll ─────────────
  // Eine bewegungs-abhaengige Funktion braucht ERPM ZWINGEND — auch wenn der
  // normale "VESC Daten auslesen"-Haken aus ist. Das gilt fuer den BLE-Auto-
  // Modus UND fuer den AP-Auto-Modus (fahren haelt den AP wach / holt ihn
  // zurueck, genau wie BLE-Auto).
  bool needErpmForWake = (cfg_ble_mode == 2) || (cfg_ap_mode == 2);
  if (!cfg_vesc_poll && !needErpmForWake) return;

  bool autoPollActive  = cfg_autopoll_enabled || needErpmForWake;
  bool uiActive        = webUiActive();
  if (!uiActive && !autoPollActive) return;

  unsigned long pollInterval = uiActive ? 3000UL : (unsigned long)cfg_autopoll_interval * 1000UL;
  if (now - lastVescPoll < pollInterval) return;
  lastVescPoll = now;

  // Anfrage senden und in die Antwort-Phase wechseln (KEIN blockierendes Warten).
  Serial1.write(VESC_GET_VALUES_PKT, sizeof(VESC_GET_VALUES_PKT));
  if (cfg_debug && (cfg_debug_filter & 4)) uartLogAdd("POLL=>VESC: 02 01 04 40 84 03");
  vescPollBuffer.clear();
  vescPollSentAt  = now;
  vescPollWaiting = true;
}

static std::string vescBuffer;

// ── VESC-UART initialisieren ──────────────────────────────────────────────────
void vescSetup() {
  Serial1.setRxBufferSize(512);
  Serial1.setTxBufferSize(512);
  Serial1.begin(115200, SERIAL_8N1, cfg_rx_pin, cfg_tx_pin);
  Serial.printf("VESC Serial: RX=GPIO%d TX=GPIO%d\n", cfg_rx_pin, cfg_tx_pin);
}

// ── VESC-TCP-Server starten ───────────────────────────────────────────────────
void vescTcpSetup() {
  server = WiFiServer(cfg_port);
  server.begin();
  server.setNoDelay(true);
  Serial.printf("VESC TCP: port %d\n", cfg_port);
}

// ── VESC-Bridge und Polling ───────────────────────────────────────────────────
void vescLoop() {
  pollVesc();

  // LED-Modul: bekommt aktuellen ERPM fuer spaetere bewegungsabhaengige Effekte.
  // Nur wenn die WS28XX-Steuerung aktiv ist -> bei deaktivierter Steuerung
  // werden keine Effekte mehr getrieben, LEDs bleiben aus.
  // LED-Rendering laeuft jetzt im eigenen Task (Kern 1). Hier nur noch billig
  // den Zustand melden; das eigentliche show() macht der LED-Task.
  ledsUpdateState(cfg_leds_enabled, vescStatus.erpm);

  if (!wifiClient || !wifiClient.connected()) {
    wifiClient = server.available();
    if (wifiClient) {
      wifiClient.setNoDelay(true);
      wifiClient.setTimeout(100);
      dlog("WiFi client connected\n");
    }
  }

  if (wifiClient && wifiClient.connected()) {
    size_t avail = wifiClient.available();
    if (avail > 0) {
      size_t len = wifiClient.readBytes(buf, min(avail, MAX_BUF));
      if (len > 0) {
        if (cfg_debug && (cfg_debug_filter & 2)) { String h="WiFi=>VESC: ";for(size_t i=0;i<len;i++){char x[4];snprintf(x,4,"%02X ",buf[i]);h+=x;} uartLogAdd(h); }
        dlog("WiFi => VESC: %d bytes\n", len);
        Serial1.write(buf, len);
      }
    }
  }

  if (Serial1.available()) {
    while (Serial1.available()) {
      vescBuffer.push_back(Serial1.read());
      if (vescBuffer.length() > MAX_VESC_BUFFER) {
        Serial.println("WARNING: vescBuffer overflow!");
        vescBuffer.clear();
        break;
      }
    }
    if (vescBuffer.length() > 0) {
      if (cfg_debug && (cfg_debug_filter & 2)) { String h="VESC=>: ";for(size_t i=0;i<min(vescBuffer.length(),(size_t)40);i++){char x[4];snprintf(x,4,"%02X ",(uint8_t)vescBuffer[i]);h+=x;} uartLogAdd(h); }
      if (deviceConnected) {
        std::string tmp = vescBuffer;
        while (tmp.length() > 0) {
          size_t chunkSize = min(tmp.length(), (size_t)PACKET_SIZE);
          std::string chunk = tmp.substr(0, chunkSize);
          pCharacteristicVescTx->setValue(chunk);
          pCharacteristicVescTx->notify();
          tmp = tmp.substr(chunkSize);
          delay(5);
        }
      }
      if (wifiClient && wifiClient.connected()) {
        size_t written = wifiClient.write((const uint8_t*)vescBuffer.c_str(), vescBuffer.length());
        if (written > 0) Serial.printf("VESC => WiFi: %d bytes\n", written);
        if (written < vescBuffer.length() && !wifiClient.connected()) {
          dlog("WiFi client disconnected\n");
          wifiClient.stop();
        }
      }
      // Parse VESC values from bridge traffic too
      if (cfg_vesc_poll && vescBuffer.size() > 5 && (uint8_t)vescBuffer[0]==0x02 && (uint8_t)vescBuffer.back()==0x03) {
        uint8_t plen = (uint8_t)vescBuffer[1];
        if (vescBuffer.size() >= (size_t)(plen+4)) parseGetValues((const uint8_t*)vescBuffer.data()+2, plen);
      }
      vescBuffer.clear();
    }
  }

  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    if (cfg_ble_mode == 1 || (cfg_ble_mode == 2 && bleAdvertisingActive())) {
      pServer->startAdvertising();
      dlog("BLE advertising restarted\n");
    }
    oldDeviceConnected = false;
  }
  if (deviceConnected && !oldDeviceConnected) oldDeviceConnected = true;
}

#endif // VESC_BRIDGE_UNITY_BUILD
