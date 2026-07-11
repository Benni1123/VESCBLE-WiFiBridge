#define VESC_BRIDGE_UNITY_BUILD 1
#include "globals.h"
#include "config.h"
#include "debuglog.h"
#include "wifi-ble.h"
#include "vesc.h"
#include "webui.h"

// Die Moduldateien werden absichtlich hier eingebunden. Arduino/PlatformIO
// kompiliert sie zusaetzlich als eigene Dateien; ohne VESC_BRIDGE_UNITY_BUILD
// sind diese Einheiten leer. So gibt es keine doppelten Definitionen.
#include "config.cpp"
#include "debuglog.cpp"
#include "wifi-ble.cpp"
#include "vesc.cpp"
#include "webui.cpp"

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n=== VESC BLE/WiFi Bridge ===");

  loadConfig();
  captureBootDiagnostics();
  dlog("BLE Name: %s | WiFi networks: %d\n", cfg_ble_name.c_str(), cfg_wifi.size());
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());

  vescSetup();
  wifiBleSetup();
  webUiSetup();
  vescTcpSetup();

  // Advertising nur (re-)starten wenn der Modus es zulaesst
  if (cfg_ble_mode != 0) NimBLEDevice::startAdvertising();
  Serial.printf("Free heap after init: %d bytes\n", ESP.getFreeHeap());
  Serial.println("=== Ready ===\n");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  // ── Diagnose: Loop-Zeit + Frequenz + Heap-Tiefstand messen ──────────────────
  unsigned long diagLoopT0 = micros();
  {
    uint32_t h = ESP.getFreeHeap();
    if (h < diagMinHeap) diagMinHeap = h;   // niedrigsten freien Heap merken
    unsigned long nowMs = millis();
    if (diagLoopWindowStart == 0) diagLoopWindowStart = nowMs;
    diagLoopWindowCount++;
    if (nowMs - diagLoopWindowStart >= 1000) {   // jede Sekunde Frequenz festhalten
      diagLoopsPerSec     = diagLoopWindowCount;
      diagLoopWindowCount = 0;
      diagLoopWindowStart = nowMs;
      diagMaxLoopUs       = 0;   // Max pro Sekunde zuruecksetzen (aktueller Wert)
    }
  }

  webUiLoop();
  wifiBleLoop();
  vescLoop();

  // Diagnose: Dauer dieses Loop-Durchlaufs; Maximum im aktuellen Sekundenfenster
  // festhalten. Ein hoher Wert = irgendwas blockiert den Loop (Blockade-Indikator).
  unsigned long diagLoopDt = micros() - diagLoopT0;
  if (diagLoopDt > diagMaxLoopUs) diagMaxLoopUs = diagLoopDt;

  yield();
}
