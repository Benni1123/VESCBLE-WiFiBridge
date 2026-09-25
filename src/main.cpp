#define VESC_BRIDGE_UNITY_BUILD 1
#include "globals.h"
#include "config.h"
#include "debuglog.h"
#include "time-service.h"
#include "logship.h"
#include "blackbox.h"
#include "coredump.h"
#include "backup.h"
#include "wifi-ble.h"
#include "vesc.h"
#include "webui.h"

// Die Moduldateien werden absichtlich hier eingebunden. Arduino/PlatformIO
// kompiliert sie zusaetzlich als eigene Dateien; ohne VESC_BRIDGE_UNITY_BUILD
// sind diese Einheiten leer. So gibt es keine doppelten Definitionen.
#include "config.cpp"
#include "debuglog.cpp"
#include "time-service.cpp"
#include "logship.cpp"
#include "coredump.cpp"
#include "backup.cpp"
#include "wifi-ble.cpp"
#include "vesc.cpp"
#include "webui.cpp"
// blackbox.cpp ZULETZT: der Stall-Waechter liest den RTC-Ringpuffer aus
// logship.cpp direkt aus. Im Unity-Build ist das dieselbe
// Uebersetzungseinheit, also muss logship.cpp vorher stehen.
#include "blackbox.cpp"

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  // WS2812 SO FRUEH WIE MOEGLICH blanken: noch vor dem Startup-Delay und vor
  // WiFi/BLE. So zeigen die Strips nach dem Power-On nicht sekundenlang den
  // zufaelligen Einschalt-Zustand, bis die spaete LED-Init sie loescht.
  // ledsInitStripsEarly() ist unabhaengig von loadConfig() (eigene "leds"-NVS)
  // und idempotent -> ledsSetup() ruft es spaeter erneut auf (dann No-Op).
  ledsInitStripsEarly();

  delay(2000);
  Serial.println("\n=== VESC BLE/WiFi Bridge ===");

  loadConfig();

  // Log-Sendepuffer VOR der Bootdiagnose anlegen: Resetgrund, Brownout, Panic
  // und Watchdog sind genau die Zeilen, die nach einem Aussetzer zaehlen. Wird
  // der Puffer erst spaeter angelegt, gehen sie verloren.
  logShipSetup();

  // Blackbox des vorherigen Laufs aus dem Flash holen. Muss NACH
  // logShipSetup() laufen (sonst gibt es keinen Puffer, in den die Zeilen
  // koennten) und VOR captureBootDiagnostics(), damit im Serverlog die
  // Vorgeschichte vor dem Bootgrund steht.
  blackboxSetup();

  // Absturzabbild aus der Flash-Partition auswerten. Muss nach logShipSetup()
  // laufen (sonst gibt es keinen Puffer fuer die Zeilen) und vor der
  // Bootdiagnose, damit im Serverlog erst steht WAS abgestuerzt ist und
  // danach der Resetgrund.
  coreDumpSetup();

  captureBootDiagnostics();
  dlog("BLE Name: %s | WiFi networks: %d\n", cfg_ble_name.c_str(), cfg_wifi.size());
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());

  vescSetup();
  wifiBleSetup();
  timeServiceSetup();
  webUiSetup();
  vescTcpSetup();

  // Sende-Task erst starten, wenn WiFi steht — vorher gibt es ohnehin nichts
  // zu senden, und der Puffer haelt alles bis dahin fest.
  logShipStartTask();

  // Advertising nur (re-)starten wenn der Modus es zulaesst
  if (cfg_ble_mode != 0) NimBLEDevice::startAdvertising();
  Serial.printf("Free heap after init: %d bytes\n", ESP.getFreeHeap());
  Serial.println("=== Ready ===\n");

  // Stall-Waechter ZULETZT scharf machen. Waehrend setup() laeuft, gibt es
  // noch kein Lebenszeichen aus dem Loop — ein frueher gestarteter Waechter
  // wuerde ein langes setup() (WLAN-Verbindungsaufbau) als Stillstand werten.
  blackboxStartTask();
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
  // Lebenszeichen fuer den Stall-Waechter (blackbox.cpp). Bleibt dieser Wert
  // stehen, haengt der Loop in einem der Abschnitte weiter unten — und der
  // Waechter schreibt die Blackbox und startet neu.
  blackboxHeartbeat = millis();

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

  // Die Phasenmarker kosten je einen Speicherzugriff und sind der einzige
  // Weg, nach einem Stillstand zu sagen, WELCHER Abschnitt nicht
  // zurueckgekehrt ist. Ohne sie weiss man nur, dass der Loop steht.
  blackboxPhase = BB_PHASE_WEBUI;
  webUiLoop();
  blackboxPhase = BB_PHASE_WIFI;
  wifiBleLoop();
  blackboxPhase = BB_PHASE_TIME;
  timeServiceLoop();
  blackboxPhase = BB_PHASE_VESC;
  vescLoop();
  blackboxPhase = BB_PHASE_IDLE;

  // Diagnose: Dauer dieses Loop-Durchlaufs; Maximum im aktuellen Sekundenfenster
  // festhalten. Ein hoher Wert = irgendwas blockiert den Loop (Blockade-Indikator).
  unsigned long diagLoopDt = micros() - diagLoopT0;
  if (diagLoopDt > diagMaxLoopUs) diagMaxLoopUs = diagLoopDt;
  // Zweites Maximum, das NICHT jede Sekunde verfaellt: nur so taucht ein
  // Haenger in der naechsten [STAT]-Zeile ueberhaupt auf.
  if (diagLoopDt > diagMaxLoopUsStat) {
    diagMaxLoopUsStat = diagLoopDt;
    diagMaxLoopAtSec  = millis() / 1000UL;
  }

  yield();
}
