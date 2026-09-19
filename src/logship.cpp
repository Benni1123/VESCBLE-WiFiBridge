// Log-Versand an einen HTTP-Server (NDJSON)
// Diese Datei wird ueber main.cpp als Unity-Build eingebunden.
// Dadurch bleiben die bisherigen static-Sichtbarkeiten und Abhaengigkeiten exakt erhalten,
// waehrend der Quellcode logisch in einzelne Dateien aufgeteilt ist.
#if defined(VESC_BRIDGE_UNITY_BUILD)
#include "globals.h"
#include "debuglog.h"
#include "time-service.h"
#include "logship.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ── Puffergroesse ────────────────────────────────────────────────────────────
// 2000 Slots a 256 Byte Text + 12 Byte Kopf = 536.000 Byte (rund 523 KiB).
// Liegt im PSRAM (2 MB vorhanden), damit der interne Heap fuer WiFi/BLE/TLS
// frei bleibt. Ohne PSRAM wird auf einen kleinen internen Puffer
// zurueckgefallen, damit die Firmware auch auf einem Board ohne PSRAM startet.
//
// 256 statt eines kleineren Werts, weil die laengste Zeile der Firmware der
// periodische [STAT]-Schnappschuss aus wifi-ble.cpp ist: mit 32-Zeichen-SSID
// und unguenstig grossen Diagnosezaehlern misst der 239 Zeichen. Bei 160 oder
// 224 waere ausgerechnet dessen Ende (die Zaehler) abgeschnitten worden.
#define LOGSHIP_LINE_MAX     256
#define LOGSHIP_SLOTS_PSRAM  2000
#define LOGSHIP_SLOTS_HEAP   150
#define LOGSHIP_BATCH_MAX    50

struct LogShipSlot {
  uint32_t seq;         // fortlaufend, erkennt Luecken auf der Serverseite
  uint32_t uptimeSec;   // Laufzeit seit Boot beim Erfassen der Zeile
  uint32_t epoch;       // Unixzeit beim Erfassen, 0 = Uhr war noch ungueltig
  char     text[LOGSHIP_LINE_MAX];
};

static LogShipSlot *shipBuf   = nullptr;
static uint16_t     shipSlots = 0;
static uint16_t     shipHead  = 0;   // naechste Schreibposition
static uint16_t     shipTail  = 0;   // aelteste noch nicht bestaetigte Zeile
static uint16_t     shipCount = 0;   // belegte Slots

static uint32_t shipSeq        = 1;  // naechste zu vergebende Sequenznummer
static uint32_t shipDropped    = 0;  // wegen Pufferueberlauf verworfen
static uint32_t shipSentLines  = 0;  // erfolgreich uebertragene Zeilen
static uint32_t shipBatchesOk  = 0;
static uint32_t shipBatchesErr = 0;
static uint32_t shipBootId     = 0;  // pro Boot zufaellig: trennt Neustarts
static int      shipLastCode   = 0;  // letzter HTTP-Statuscode
static String   shipLastError;
static uint32_t shipLastOkUptime = 0;

static SemaphoreHandle_t shipMutex      = nullptr;
static TaskHandle_t      shipTaskHandle = nullptr;
static volatile bool     shipFlushNow   = false;

// ── Konfigurations-Schnappschuss ─────────────────────────────────────────────
// Der Sende-Task laeuft auf Kern 0, die Webhandler auf Kern 1. Wuerde der Task
// cfg_logship_url/-token direkt lesen, waehrend ein POST auf /api/logship sie
// neu zuweist, laese er waehrend der Neu-Allokation in einen freigegebenen
// Puffer — ein Absturz, der sich hinterher nicht mehr zuordnen laesst.
// Deshalb halten wir modul-eigene Kopien, die ausschliesslich unter shipMutex
// geschrieben und gelesen werden. logShipApplyConfig() uebernimmt sie.
static String shipUrl;
static String shipToken;
static String shipDevName;
static bool   shipEnabled = false;

// Fehler-Backoff: 5s -> 10s -> 20s -> 30s, danach dauerhaft 30s.
static const unsigned long SHIP_BACKOFF_MS[] = { 5000UL, 10000UL, 20000UL, 30000UL };
static const uint8_t SHIP_BACKOFF_LAST =
    (uint8_t)(sizeof(SHIP_BACKOFF_MS) / sizeof(SHIP_BACKOFF_MS[0]) - 1);
static uint8_t       shipBackoffStage = 0;
static unsigned long shipNextTry      = 0;

static inline void shipLock()   { if (shipMutex) xSemaphoreTake(shipMutex, portMAX_DELAY); }
static inline void shipUnlock() { if (shipMutex) xSemaphoreGive(shipMutex); }

// ── Blackbox im RTC-Speicher ────────────────────────────────────────────────
// Der grosse Ringpuffer liegt im PSRAM und ist nach einem Reset leer — er
// ueberbrueckt WLAN-Ausfaelle, aber keine Abstuerze. Ausgerechnet die Minuten
// VOR einem Panic sind damit verloren, also genau das, was man braucht.
//
// RTC_NOINIT ueberlebt Software-Reset, Panic und Watchdog (nur ein echter
// Power-Cycle loescht ihn). Hier liegen deshalb zusaetzlich die letzten paar
// Zeilen. Beim naechsten Start werden sie als Erstes in den Sendepuffer
// gelegt und geschickt — mit dem Praefix [PRE-RESET], damit klar ist, dass
// sie aus dem Leben VOR dem Reset stammen.
//
// Der RTC-Speicher des S3 ist knapp (8 KB gesamt, davon nicht alles frei),
// deshalb bewusst wenige und kuerzere Zeilen: 24 x 160 Byte = rund 3,9 KB.
#ifndef RTC_NOINIT_ATTR
#define RTC_NOINIT_ATTR
#endif

#define LOGSHIP_RTC_SLOTS 24
#define LOGSHIP_RTC_LINE  160
#define LOGSHIP_RTC_MAGIC 0x4C534232UL   // "LSB2"

struct LogShipRtcSlot {
  uint32_t uptimeSec;
  char     text[LOGSHIP_RTC_LINE];
};
struct LogShipRtcRing {
  uint32_t magic;
  uint16_t head;
  uint16_t count;
  LogShipRtcSlot slots[LOGSHIP_RTC_SLOTS];
};
RTC_NOINIT_ATTR static LogShipRtcRing rtcRing;

// Waehrend des Wiedereinspielens darf logShipAdd() die Blackbox nicht erneut
// beschreiben — sonst ueberschreiben die alten Zeilen sich selbst.
static bool shipReplaying = false;

// Bewusst nur Magic + Plausibilitaet der Indizes statt einer Pruefsumme:
// hier wird bei JEDER Logzeile geschrieben, und eine Pruefsumme ueber knapp
// 4 KB bei jeder Zeile waere Verschwendung. Uninitialisierter RTC-Speicher
// muesste zufaellig das Magic UND zwei gueltige Indizes treffen.
static bool rtcRingValid() {
  return rtcRing.magic == LOGSHIP_RTC_MAGIC &&
         rtcRing.head  <  LOGSHIP_RTC_SLOTS &&
         rtcRing.count <= LOGSHIP_RTC_SLOTS;
}

static void rtcRingReset() {
  rtcRing.magic = LOGSHIP_RTC_MAGIC;
  rtcRing.head  = 0;
  rtcRing.count = 0;
}

// ── Puffer anlegen ───────────────────────────────────────────────────────────
void logShipSetup() {
  if (shipBuf) return;                       // idempotent
  if (!shipMutex) shipMutex = xSemaphoreCreateMutex();

  size_t want = (size_t)LOGSHIP_SLOTS_PSRAM * sizeof(LogShipSlot);
  if (ESP.getPsramSize() > 0) {
    shipBuf = (LogShipSlot *)heap_caps_malloc(want, MALLOC_CAP_SPIRAM);
    if (shipBuf) shipSlots = LOGSHIP_SLOTS_PSRAM;
  }
  if (!shipBuf) {
    // Kein PSRAM (oder Allokation gescheitert) -> kleiner Puffer im internen Heap.
    want = (size_t)LOGSHIP_SLOTS_HEAP * sizeof(LogShipSlot);
    shipBuf = (LogShipSlot *)malloc(want);
    if (shipBuf) shipSlots = LOGSHIP_SLOTS_HEAP;
  }
  if (!shipBuf) {
    shipSlots = 0;
    Serial.println("[LOGSHIP] buffer allocation FAILED - log shipping disabled");
    return;
  }

  memset(shipBuf, 0, want);
  shipHead = shipTail = shipCount = 0;
  shipSeq  = 1;

  // Boot-ID: trennt auf dem Server die Logs verschiedener Starts voneinander,
  // auch wenn die Uhr noch nicht synchronisiert ist. esp_random() liefert nach
  // dem WiFi-Start echten Zufall, davor immerhin einen wechselnden Wert.
  shipBootId = esp_random();
  if (shipBootId == 0) shipBootId = (uint32_t)millis() + 1;

  Serial.printf("[LOGSHIP] buffer: %u lines, %u bytes (%s)\n",
                (unsigned)shipSlots, (unsigned)want,
                (shipSlots == LOGSHIP_SLOTS_PSRAM) ? "PSRAM" : "internal heap");

  logShipApplyConfig();   // beim Boot geladene Werte uebernehmen

  // Blackbox des vorherigen Laufs einspielen, BEVOR irgendetwas anderes
  // geloggt wird — diese Zeilen sind aelter als die Bootdiagnose und sollen
  // im Server-Log auch davor stehen.
  if (rtcRingValid() && rtcRing.count > 0) {
    uint16_t n   = rtcRing.count;
    uint16_t idx = (uint16_t)((rtcRing.head + LOGSHIP_RTC_SLOTS - n) % LOGSHIP_RTC_SLOTS);
    shipReplaying = true;
    logShipAdd("[PRE-RESET] --- letzte " + String(n) +
               " Zeilen vor dem Reset, aus dem RTC-Speicher ---");
    for (uint16_t i = 0; i < n; i++) {
      char tmp[LOGSHIP_RTC_LINE];
      memcpy(tmp, rtcRing.slots[idx].text, LOGSHIP_RTC_LINE);
      tmp[LOGSHIP_RTC_LINE - 1] = 0;        // gegen unvollstaendige Eintraege
      if (tmp[0]) {
        logShipAdd("[PRE-RESET up" + String(rtcRing.slots[idx].uptimeSec) + "s] " +
                   String(tmp));
      }
      idx = (uint16_t)((idx + 1) % LOGSHIP_RTC_SLOTS);
    }
    shipReplaying = false;
    Serial.printf("[LOGSHIP] replayed %u lines from RTC black box\n", (unsigned)n);
  }
  rtcRingReset();   // fuer den aktuellen Lauf frisch beginnen
}

// Uebernimmt die aktuelle Konfiguration in den Schnappschuss. Muss nach jeder
// Aenderung an cfg_logship_* aufgerufen werden (Webhandler, Config-POST); am
// Ende von logShipSetup() geschieht das fuer die beim Boot geladenen Werte.
void logShipApplyConfig() {
  if (!shipMutex) shipMutex = xSemaphoreCreateMutex();
  String url = cfg_logship_url;
  url.replace("\\/", "/");          // gleiche Reparatur wie bei den Update-URLs
  shipLock();
  shipUrl     = url;
  shipToken   = cfg_logship_token;
  shipDevName = cfg_ble_name;
  shipEnabled = cfg_logship_enabled && url.length() > 0;
  shipUnlock();
}

// ── Zeile puffern ────────────────────────────────────────────────────────────
void logShipAdd(const String &line) {
  if (!shipBuf || shipSlots == 0) return;

  // Zeilenumbrueche und Steuerzeichen raus: eine Logzeile ist genau eine
  // NDJSON-Zeile. Ein \n im Text wuerde das Format auf der Serverseite zerlegen.
  String clean = line;
  clean.replace("\r", " ");
  clean.replace("\n", " ");
  clean.trim();
  if (clean.isEmpty()) return;

  uint32_t up = (uint32_t)(millis() / 1000UL);
  time_t   now = timeServiceEpoch();

  shipLock();
  if (shipCount >= shipSlots) {
    // Puffer voll -> aelteste Zeile verwerfen. Bewusst so herum: bei einem
    // langen Ausfall ist der aktuelle Verlauf interessanter als der Anfang.
    // Der Zaehler wird mitgesendet, damit die Luecke auf dem Server sichtbar ist.
    shipTail = (uint16_t)((shipTail + 1) % shipSlots);
    shipCount--;
    shipDropped++;
  }
  LogShipSlot &s = shipBuf[shipHead];
  s.seq       = shipSeq++;
  s.uptimeSec = up;
  s.epoch     = (now > 0) ? (uint32_t)now : 0;
  strncpy(s.text, clean.c_str(), LOGSHIP_LINE_MAX);
  s.text[LOGSHIP_LINE_MAX - 1] = 0;
  shipHead = (uint16_t)((shipHead + 1) % shipSlots);
  shipCount++;

  // Zusaetzlich in die reset-feste Blackbox (siehe oben).
  if (!shipReplaying) {
    if (!rtcRingValid()) rtcRingReset();
    LogShipRtcSlot &r = rtcRing.slots[rtcRing.head];
    r.uptimeSec = up;
    strncpy(r.text, clean.c_str(), LOGSHIP_RTC_LINE);
    r.text[LOGSHIP_RTC_LINE - 1] = 0;
    rtcRing.head = (uint16_t)((rtcRing.head + 1) % LOGSHIP_RTC_SLOTS);
    if (rtcRing.count < LOGSHIP_RTC_SLOTS) rtcRing.count++;
  }
  shipUnlock();
}

void logShipClear() {
  if (!shipBuf) return;
  shipLock();
  shipHead = shipTail = shipCount = 0;
  shipDropped = 0;
  shipUnlock();
}

void logShipRequestFlush() {
  shipFlushNow     = true;
  shipBackoffStage = 0;
  shipNextTry      = 0;
}

void logShipSendTestLine() {
  shipLock();
  String dev = shipDevName;
  shipUnlock();
  logShipAdd("[LOGSHIP] test line from " + dev);
  logShipRequestFlush();
}

// Rechnet eine noch undatierte Zeile auf echte Unixzeit zurueck, sobald die Uhr
// gueltig ist. Gleiche Idee wie uartLogResolveUptimeStamp() im UI-Log: die
// Uptime ist die feste Referenz, das Datum wird erst beim Senden berechnet.
static uint32_t shipResolveEpoch(const LogShipSlot &s) {
  if (s.epoch != 0) return s.epoch;
  time_t now = timeServiceEpoch();
  if (now <= 0) return 0;
  uint32_t upNow = (uint32_t)(millis() / 1000UL);
  if (s.uptimeSec > upNow) return 0;
  return (uint32_t)now - (upNow - s.uptimeSec);
}

// Baut den NDJSON-Body aus bis zu LOGSHIP_BATCH_MAX gepufferten Zeilen.
// Laeuft unter dem Mutex, macht aber KEIN I/O -> nur Millisekunden.
static String shipBuildBatch(uint32_t &lastSeqOut, uint16_t &linesOut) {
  String body;
  body.reserve(LOGSHIP_BATCH_MAX * 300);
  lastSeqOut = 0;
  linesOut   = 0;

  shipLock();
  uint16_t idx = shipTail;
  uint16_t n   = shipCount < LOGSHIP_BATCH_MAX ? shipCount : LOGSHIP_BATCH_MAX;
  uint32_t dropped = shipDropped;
  for (uint16_t i = 0; i < n; i++) {
    const LogShipSlot &s = shipBuf[idx];
    body += "{\"seq\":"      + String(s.seq);
    body += ",\"boot\":"     + String(shipBootId);
    body += ",\"uptime\":"   + String(s.uptimeSec);
    uint32_t ep = shipResolveEpoch(s);
    body += ",\"epoch\":"    + String(ep);
    body += ",\"exact_ts\":" + String(s.epoch != 0 ? "true" : "false");
    body += ",\"dev\":\""    + jsonEscapeDebug(shipDevName) + "\"";
    body += ",\"fw\":\""     + String(FIRMWARE_VERSION) + "\"";
    body += ",\"dropped\":"  + String(dropped);
    body += ",\"msg\":\""    + jsonEscapeDebug(String(s.text)) + "\"}\n";
    lastSeqOut = s.seq;
    idx = (uint16_t)((idx + 1) % shipSlots);
  }
  linesOut = n;
  shipUnlock();
  return body;
}

// Bestaetigte Zeilen freigeben. Der Puffer kann waehrend des Sendens
// uebergelaufen sein, deshalb wird ueber die Sequenznummer abgeglichen und
// nicht blind um n Positionen vorgerueckt.
static void shipConfirm(uint32_t lastSeq) {
  shipLock();
  while (shipCount > 0 && shipBuf[shipTail].seq <= lastSeq) {
    shipTail = (uint16_t)((shipTail + 1) % shipSlots);
    shipCount--;
  }
  shipUnlock();
}

// Genau EIN Sendeversuch. Laeuft ausschliesslich im Sende-Task.
static void shipFlushOnce() {
  uint32_t lastSeq = 0;
  uint16_t lines   = 0;
  String   body    = shipBuildBatch(lastSeq, lines);
  if (lines == 0) return;

  // URL und Token unter dem Mutex in lokale Kopien holen. Ab hier arbeitet die
  // Funktion nur noch mit diesen Kopien, auch wenn die Weboberflaeche waehrend
  // des laufenden POST die Konfiguration aendert.
  shipLock();
  String url   = shipUrl;
  String token = shipToken;
  String dev   = shipDevName;
  shipUnlock();
  if (url.isEmpty()) return;

  // TLS braucht rund 40 KB am Stueck. Bei zu wenig zusammenhaengendem Heap gar
  // nicht erst versuchen - ein gescheiterter Handshake kostet nur Zeit.
  bool secure = url.startsWith("https");
  if (secure && ESP.getMaxAllocHeap() < 45000) {
    shipLastError = "low heap for TLS";
    shipBatchesErr++;
    if (shipBackoffStage < SHIP_BACKOFF_LAST) shipBackoffStage++;
    shipNextTry = millis() + SHIP_BACKOFF_MS[shipBackoffStage];
    return;
  }

  // WiFiClientSecure NUR im TLS-Pfad anlegen: das Objekt zieht beim Verbinden
  // mbedTLS-Puffer, die im Klartext-Fall nichts zu suchen haben. Es muss den
  // gesamten POST ueberleben (HTTPClient haelt eine Referenz darauf), deshalb
  // steht es hier im Funktionsrahmen und wird nicht vorzeitig freigegeben.
  HTTPClient http;
  WiFiClientSecure *sc = nullptr;
  bool begun;
  if (secure) {
    sc = new WiFiClientSecure();
    if (!sc) {
      shipLastError = "no memory for TLS client";
      shipBatchesErr++;
      if (shipBackoffStage < SHIP_BACKOFF_LAST) shipBackoffStage++;
      shipNextTry = millis() + SHIP_BACKOFF_MS[shipBackoffStage];
      return;
    }
    sc->setInsecure();
    begun = http.begin(*sc, url);
  } else {
    begun = http.begin(url);
  }
  if (!begun) {
    if (sc) delete sc;
    shipLastError = "begin failed (bad URL?)";
    shipBatchesErr++;
    if (shipBackoffStage < SHIP_BACKOFF_LAST) shipBackoffStage++;
    shipNextTry = millis() + SHIP_BACKOFF_MS[shipBackoffStage];
    return;
  }

  http.setTimeout(8000);
  http.setConnectTimeout(5000);
  http.setReuse(false);
  http.addHeader("Content-Type", "application/x-ndjson");
  http.addHeader("X-Device", dev);
  http.addHeader("X-Boot-Id", String(shipBootId));
  if (token.length() > 0)
    http.addHeader("Authorization", "Bearer " + token);

  int code = http.POST(body);
  http.end();   // Verbindung/TLS sofort freigeben (gegen Heap-Fragmentierung)
  if (sc) delete sc;

  shipLastCode = code;
  if (code >= 200 && code < 300) {
    shipConfirm(lastSeq);
    shipSentLines += lines;
    shipBatchesOk++;
    shipLastError = "";
    shipLastOkUptime = (uint32_t)(millis() / 1000UL);
    shipBackoffStage = 0;
    shipNextTry = 0;
  } else {
    // Nichts bestaetigen -> die Zeilen bleiben im Puffer und gehen beim
    // naechsten Versuch erneut raus. Genau das ist der Sinn der Uebung.
    shipLastError = "HTTP " + String(code);
    shipBatchesErr++;
    if (shipBackoffStage < SHIP_BACKOFF_LAST) shipBackoffStage++;
    shipNextTry = millis() + SHIP_BACKOFF_MS[shipBackoffStage];
  }
}

static void logShipTaskFn(void *) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(250));

    if (!shipEnabled)                    { shipFlushNow = false; continue; }
    if (!shipBuf || shipSlots == 0)      { shipFlushNow = false; continue; }
    if (WiFi.status() != WL_CONNECTED)   { continue; }   // Puffer laeuft weiter voll
    if (shipCount == 0)                  { shipFlushNow = false; continue; }

    unsigned long now = millis();
    if (!shipFlushNow && shipNextTry != 0 && (long)(now - shipNextTry) < 0) continue;
    shipFlushNow = false;

    shipFlushOnce();

    // Grosser Rueckstand nach einem Ausfall: zuegig weitersenden, aber dem
    // WLAN zwischen den Batches Luft lassen.
    if (shipCount > 0 && shipLastError.isEmpty()) vTaskDelay(pdMS_TO_TICKS(150));
    else                                          vTaskDelay(pdMS_TO_TICKS(750));
  }
}

void logShipStartTask() {
  if (shipTaskHandle) return;
  if (!shipBuf || shipSlots == 0) return;
  // Kern 0: der LED-Task haelt Kern 1 belegt, dort hat ein blockierender
  // TLS-Handshake nichts verloren.
  // 16 KB Stack: ein mbedTLS-Handshake (https-Ziel) braucht allein rund 6-8 KB
  // zusaetzlich. Mit 8 KB laeuft der Task bei einem https-Server ueber.
  xTaskCreatePinnedToCore(logShipTaskFn, "logship", 16384, nullptr, 1, &shipTaskHandle, 0);
}

String logShipStatusJson() {
  shipLock();
  uint16_t buffered = shipCount;
  uint16_t slots    = shipSlots;
  uint32_t dropped  = shipDropped;
  String   url      = shipUrl;
  bool     hasToken = shipToken.length() > 0;
  bool     enabled  = shipEnabled;
  shipUnlock();

  String json = "{";
  json += "\"enabled\":"   + String(enabled ? "true" : "false");
  json += ",\"url\":\""    + jsonEscapeDebug(url) + "\"";
  json += ",\"token_set\":" + String(hasToken ? "true" : "false");
  json += ",\"boot_id\":"  + String(shipBootId);
  json += ",\"slots\":"    + String(slots);
  json += ",\"buffered\":" + String(buffered);
  json += ",\"dropped\":"  + String(dropped);
  json += ",\"sent\":"     + String(shipSentLines);
  json += ",\"batches_ok\":"  + String(shipBatchesOk);
  json += ",\"batches_err\":" + String(shipBatchesErr);
  json += ",\"last_code\":"   + String(shipLastCode);
  json += ",\"last_error\":\"" + jsonEscapeDebug(shipLastError) + "\"";
  json += ",\"last_ok_uptime\":" + String(shipLastOkUptime);
  json += ",\"wifi\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
  json += ",\"psram\":" + String(slots == LOGSHIP_SLOTS_PSRAM ? "true" : "false");
  json += "}";
  return json;
}

#endif // VESC_BRIDGE_UNITY_BUILD
