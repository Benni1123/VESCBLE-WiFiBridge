// Stall-Waechter und Blackbox im Flash (NVS)
// Diese Datei wird ueber main.cpp als Unity-Build eingebunden.
// Dadurch bleiben die bisherigen static-Sichtbarkeiten und Abhaengigkeiten exakt erhalten,
// waehrend der Quellcode logisch in einzelne Dateien aufgeteilt ist.
#if defined(VESC_BRIDGE_UNITY_BUILD)
#include "globals.h"
#include "debuglog.h"
#include "logship.h"
#include "blackbox.h"

#include <Preferences.h>
#include <Update.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ── RTC-Watchdog (Hardware) ─────────────────────────────────────────────────
// Der Kopfpfad hat sich zwischen den IDF-Versionen verschoben; beide Varianten
// abdecken, statt sich auf eine festzulegen. Ist keiner vorhanden, entfaellt
// Stufe 2 — der Rest funktioniert unveraendert, und beim Boot steht eine
// Zeile im Log, damit man nicht faelschlich mit dem Netz darunter rechnet.
#if defined(__has_include)
#  if __has_include(<esp_private/rtc_wdt.h>)
#    include <esp_private/rtc_wdt.h>
#    define BB_HAVE_RTC_WDT 1
#  elif __has_include(<soc/rtc_wdt.h>)
#    include <soc/rtc_wdt.h>
#    define BB_HAVE_RTC_WDT 1
#  endif
#endif
#ifndef BB_HAVE_RTC_WDT
#  define BB_HAVE_RTC_WDT 0
#endif

// ── Kennzahlen ───────────────────────────────────────────────────────────────
//
// BLACKBOX_STALL_MS grosszuegig gewaehlt. Der Loop laeuft im Normalbetrieb
// knapp 1000-mal pro Sekunde, das laengste gemessene Maximum liegt bei 9 ms.
// Einzelne Abschnitte duerfen aber legitim lange brauchen: ein OTA-Upload
// haengt im Webserver-Handler, ein Restore arbeitet eine Datei zeilenweise ab,
// ein blockierender Kanalscan kostet Sekunden. 60 Sekunden liegen weit ueber
// allem davon und weit unter der Zeit, die das Geraet sonst tot herumsteht.
#define BLACKBOX_STALL_MS     60000UL
#define BLACKBOX_CHECK_MS      2000UL

// Beim ersten Start des Waechters darf nicht sofort zugeschlagen werden:
// blackboxHeartbeat ist dann noch 0, und der Loop hat vielleicht gerade erst
// begonnen. Erst nach dieser Anlaufzeit wird geprueft.
#define BLACKBOX_GRACE_MS     30000UL

// Stufe 2: Hardware-Netz unter dem Waechter.
//
// Der Waechter-Task selbst ist Software. Steht der Scheduler komplett, ist
// auch er weg und niemand schreibt mehr etwas. Der RTC-Watchdog laeuft in der
// RTC-Domaene mit eigenem Takt neben der CPU her und zieht den Reset auch
// dann. Gefuettert wird er AUSSCHLIESSLICH vom Waechter-Task — nicht vom
// Hauptloop. Dadurch staffeln sich die beiden sauber:
//
//   Loop haengt, Waechter lebt   -> Stufe 1 nach 60 s, mit Blackbox
//   Waechter ebenfalls tot       -> Stufe 2 nach 180 s, harter Reset
//
// 180 s mit reichlich Abstand zu den 60 s: der Waechter hat Prioritaet 3, der
// WLAN-Task 23 und NimBLE 21. Wird er kurzzeitig verdraengt, darf daraus kein
// Reset werden.
#define BLACKBOX_RTCWDT_MS   180000UL

// NVS-Groesse. 2 KB fassen die Kopfzeilen plus rund ein Dutzend Logzeilen.
// Mehr bringt wenig: die interessante Information steht in den letzten
// Sekunden vor dem Stillstand, nicht in den letzten Minuten.
#define BLACKBOX_TEXT_MAX      2048

static const char BLACKBOX_NS[]  = "vescbb";
static const char BLACKBOX_KEY[] = "bb";

volatile uint32_t blackboxHeartbeat = 0;
volatile uint8_t  blackboxPhase     = BB_PHASE_IDLE;

static TaskHandle_t bbTaskHandle   = nullptr;
static bool         bbArmed        = false;   // Waechter scharf (Anlaufzeit vorbei)
static uint32_t     bbLastAgeMs    = 0;       // fuer die Statusanzeige
static bool         bbHadPrevious  = false;   // beim Boot eine Blackbox gefunden
static bool         bbWriteFailed  = false;
static bool         bbPendingClear = false;   // gelesen, aber noch nicht zugestellt

// ── Letzte Logzeilen aus dem RTC-Ring ────────────────────────────────────────
//
// Greift direkt auf den Ringpuffer aus logship.cpp zu. Das ist im Unity-Build
// dieselbe Uebersetzungseinheit — logship.cpp wird in main.cpp vor dieser
// Datei eingebunden, rtcRing und rtcRingValid() sind hier also sichtbar.
// Absichtlich der RTC-Ring und nicht der grosse Sendepuffer: der RTC-Ring
// haelt die letzten Zeilen unabhaengig davon, ob sie schon verschickt wurden.
//
// Kein Mutex: hier wird nur gelesen, und zwar in genau dem Moment, in dem
// ohnehin etwas haengt. Auf einen Mutex zu warten, den womoeglich der
// blockierte Task haelt, waere der sichere Weg in gar keine Blackbox. Im
// schlimmsten Fall ist eine Zeile halb geschrieben — fuer eine Diagnose
// belanglos, das Nullbyte am Ende wird ohnehin selbst gesetzt.
static size_t bbCopyRtcLines(char *dst, size_t cap, uint8_t maxLines) {
  if (!dst || cap < 2) return 0;
  dst[0] = 0;
  if (!rtcRingValid() || rtcRing.count == 0) return 0;

  uint16_t n = rtcRing.count;
  if (n > maxLines) n = maxLines;
  uint16_t idx = (uint16_t)((rtcRing.head + LOGSHIP_RTC_SLOTS - n) % LOGSHIP_RTC_SLOTS);

  size_t used = 0;
  for (uint16_t i = 0; i < n; i++) {
    char tmp[LOGSHIP_RTC_LINE];
    memcpy(tmp, rtcRing.slots[idx].text, LOGSHIP_RTC_LINE);
    tmp[LOGSHIP_RTC_LINE - 1] = 0;
    if (tmp[0]) {
      int w = snprintf(dst + used, cap - used, "up%lus %s\n",
                       (unsigned long)rtcRing.slots[idx].uptimeSec, tmp);
      if (w < 0) break;
      if ((size_t)w >= cap - used) {       // passt nicht mehr -> sauber abbrechen
        dst[cap - 1] = 0;
        used = strlen(dst);
        break;
      }
      used += (size_t)w;
    }
    idx = (uint16_t)((idx + 1) % LOGSHIP_RTC_SLOTS);
  }
  return used;
}

// ── Stufe 2: RTC-Watchdog scharf machen und fuettern ────────────────────────
static bool bbRtcWdtOn = false;

static void bbRtcWdtStart() {
#if BB_HAVE_RTC_WDT
  rtc_wdt_protect_off();
  rtc_wdt_disable();
  rtc_wdt_set_length_of_reset_signal(RTC_WDT_SYS_RESET_SIG, RTC_WDT_LENGTH_3_2us);
  rtc_wdt_set_stage(RTC_WDT_STAGE0, RTC_WDT_STAGE_ACTION_RESET_SYSTEM);
  rtc_wdt_set_time(RTC_WDT_STAGE0, BLACKBOX_RTCWDT_MS);
  rtc_wdt_enable();
  // rtc_wdt_protect_on() wird bewusst NICHT gerufen: mit aktivem Schreibschutz
  // laeuft jedes Fuettern ins Leere, und ein Watchdog, den niemand mehr
  // beruhigen kann, startet das Geraet im Minutentakt neu.
  bbRtcWdtOn = true;
  Serial.printf("[BLACKBOX] RTC-Watchdog aktiv (%lus)\n",
                (unsigned long)(BLACKBOX_RTCWDT_MS / 1000UL));
#else
  Serial.println("[BLACKBOX] RTC-Watchdog nicht verfuegbar - nur Stufe 1 aktiv");
#endif
}

static inline void bbRtcWdtFeed() {
#if BB_HAVE_RTC_WDT
  if (bbRtcWdtOn) rtc_wdt_feed();
#endif
}

static const char *phaseName(uint8_t p) {
  switch (p) {
    case BB_PHASE_WEBUI: return "webUiLoop";
    case BB_PHASE_WIFI:  return "wifiBleLoop";
    case BB_PHASE_TIME:  return "timeServiceLoop";
    case BB_PHASE_VESC:  return "vescLoop";
    default:             return "idle";
  }
}

// ── Schreiben ────────────────────────────────────────────────────────────────
//
// Bewusst OHNE jeden Aufruf in den WLAN-Stack. Diese Funktion laeuft genau
// dann, wenn irgendetwas im System haengt — und wenn ausgerechnet eine
// WiFi-API den blockierten Mutex braucht, haengt der Waechter mit und es gibt
// weder Blackbox noch Neustart. Die WLAN-Zustaende stehen ohnehin in der
// letzten [STAT]-Zeile, und die kommt ueber den RTC-Ring mit.
void blackboxWrite(const char *reason) {
  char *text = (char *)malloc(BLACKBOX_TEXT_MAX);
  if (!text) {
    bbWriteFailed = true;
    return;
  }

  uint32_t now    = millis();
  uint32_t hb     = blackboxHeartbeat;
  uint32_t age    = (hb == 0) ? 0 : (now - hb);
  uint8_t  phase  = blackboxPhase;

  size_t used = 0;
  int w = snprintf(text, BLACKBOX_TEXT_MAX,
                   "grund=%s\n"
                   "up=%lus phase=%s hb_alter=%lums\n"
                   "heap=%lu minheap=%lu maxblk=%lu loopmax=%lums@%lus loops=%lu\n",
                   (reason && reason[0]) ? reason : "unbekannt",
                   (unsigned long)(now / 1000UL),
                   phaseName(phase),
                   (unsigned long)age,
                   (unsigned long)ESP.getFreeHeap(),
                   (unsigned long)diagMinHeap,
                   (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                   (unsigned long)(diagMaxLoopUsStat / 1000UL),
                   (unsigned long)diagMaxLoopAtSec,
                   (unsigned long)diagLoopsPerSec);
  if (w > 0) used = (size_t)w;
  if (used >= BLACKBOX_TEXT_MAX) used = BLACKBOX_TEXT_MAX - 1;

  // Die letzten Logzeilen aus dem RTC-Ring. Der ist die richtige Quelle: er
  // enthaelt die letzten Zeilen unabhaengig davon, ob sie schon gesendet
  // wurden. Der grosse Sendepuffer waere bei stehendem WLAN zwar voll, bei
  // laufendem WLAN aber leer — genau dann haette man nichts.
  if (used + 48 < BLACKBOX_TEXT_MAX) {
    int h = snprintf(text + used, BLACKBOX_TEXT_MAX - used, "--- letzte Zeilen ---\n");
    if (h > 0) used += (size_t)h;
    used += bbCopyRtcLines(text + used, BLACKBOX_TEXT_MAX - used, 14);
  }
  text[BLACKBOX_TEXT_MAX - 1] = 0;

  // Sofort auch auf die serielle Schnittstelle: haengt man zufaellig per USB
  // dran, sieht man es live, ohne auf den Neustart zu warten.
  Serial.println("\n[BLACKBOX] --------------------------------");
  Serial.print(text);
  Serial.println("[BLACKBOX] --------------------------------");

  Preferences p;
  if (p.begin(BLACKBOX_NS, false)) {
    if (p.putString(BLACKBOX_KEY, text) == 0) bbWriteFailed = true;
    p.end();
  } else {
    bbWriteFailed = true;
  }

  free(text);
}

// ── Beim Boot auslesen ───────────────────────────────────────────────────────
void blackboxSetup() {
  Preferences p;
  if (!p.begin(BLACKBOX_NS, false)) return;

  String stored = p.getString(BLACKBOX_KEY, "");
  if (stored.length() == 0) {
    p.end();
    return;
  }

  p.end();

  bbHadPrevious  = true;
  // NICHT sofort loeschen. Nach einem Stillstand ist die Wahrscheinlichkeit
  // hoch, dass auch der naechste Lauf kein WLAN bekommt — dann laege die
  // einzige Spur nur noch im fluechtigen Sendepuffer und waere beim naechsten
  // Stromausfall weg. Der Eintrag bleibt deshalb im Flash stehen, bis
  // mindestens ein Batch nachweislich beim Server angekommen ist. Ein neuer
  // Stillstand ueberschreibt ihn ohnehin, der neueste Fall gewinnt also.
  bbPendingClear = true;

  Serial.println("\n[BLACKBOX] Eintrag aus dem Flash gefunden:");
  Serial.println(stored);

  // uartLogAdd() statt logShipAdd(): die Zeilen gehoeren in BEIDE Puffer.
  // logShipAdd() allein hiesse, dass die Blackbox nur auf dem Server
  // auftaucht — ausgerechnet dann unsichtbar, wenn nach dem Neustart wieder
  // kein WLAN da ist und man per AP oder USB nachsieht.
  uartLogAdd("[BLACKBOX] --- Zustand vor dem letzten Stillstand (aus dem Flash) ---");
  int start = 0;
  while (start < (int)stored.length()) {
    int nl = stored.indexOf('\n', start);
    if (nl < 0) nl = stored.length();
    String line = stored.substring(start, nl);
    line.trim();
    if (line.length() > 0) uartLogAdd("[BLACKBOX] " + line);
    start = nl + 1;
  }
  uartLogAdd("[BLACKBOX] --- Ende ---");
}

// ── Waechter ─────────────────────────────────────────────────────────────────
static void blackboxTaskFn(void *) {
  // Eigener Sicherheitsgurt: sollte der Waechter beim Schreiben der Blackbox
  // selbst an einem blockierten Mutex haengenbleiben, holt ihn der
  // Task-Watchdog. Dann gibt es zwar keine Blackbox, aber immer noch einen
  // Neustart — und ein Geraet, das zurueckkommt, ist mehr wert als eine
  // Diagnose, die niemand abholt.
  bool wdt = (esp_task_wdt_add(nullptr) == ESP_OK);

  // Stufe 2 erst hier scharf machen: ab jetzt gibt es auch jemanden, der
  // fuettert. Waere sie schon in setup() aktiv, liefe die Frist bereits,
  // waehrend noch niemand sie zuruecksetzen kann.
  bbRtcWdtStart();

  uint32_t startedAt = millis();

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(BLACKBOX_CHECK_MS));
    if (wdt) esp_task_wdt_reset();
    bbRtcWdtFeed();

    uint32_t now = millis();

    // Zugestellt? Dann darf die Spur im Flash weg. shipBatchesOk ist der
    // Zaehler erfolgreicher Uebertragungen aus logship.cpp — im Unity-Build
    // hier sichtbar. Erst ein bestaetigter Batch heisst, dass die Zeilen
    // wirklich auf dem Server liegen.
    if (bbPendingClear && shipBatchesOk > 0) {
      Preferences pc;
      if (pc.begin(BLACKBOX_NS, false)) {
        pc.remove(BLACKBOX_KEY);
        pc.end();
      }
      bbPendingClear = false;
    }

    // Waehrend eines OTA-Updates ist der Loop legitim ueber lange Strecken
    // im Upload-Handler. Ein Neustart mitten im Flashen waere fatal.
    if (Update.isRunning()) {
      startedAt = now;      // Anlaufzeit nach dem Update neu beginnen
      bbArmed   = false;
      continue;
    }

    if (!bbArmed) {
      if (now - startedAt < BLACKBOX_GRACE_MS) continue;
      if (blackboxHeartbeat == 0) continue;   // Loop hat noch nie gemeldet
      bbArmed = true;
    }

    uint32_t hb = blackboxHeartbeat;
    uint32_t age = now - hb;
    bbLastAgeMs = age;

    if (age < BLACKBOX_STALL_MS) continue;

    // Stillstand.
    char reason[96];
    snprintf(reason, sizeof(reason), "Hauptloop steht seit %lus in %s",
             (unsigned long)(age / 1000UL), phaseName(blackboxPhase));

    blackboxWrite(reason);
    bootDiagMarkPlannedRestart(reason);

    Serial.flush();
    delay(120);            // dem UART Zeit geben, den Text loszuwerden

    // Stufe 2 vor dem geplanten Neustart abschalten: ab hier fuettert
    // niemand mehr, und eine noch laufende Frist wuerde in den naechsten
    // Startvorgang hineinragen. Der Bootloader richtet sich seinen eigenen
    // RTC-Watchdog ohnehin neu ein.
#if BB_HAVE_RTC_WDT
    if (bbRtcWdtOn) {
      rtc_wdt_protect_off();
      rtc_wdt_disable();
      bbRtcWdtOn = false;
    }
#endif
    esp_restart();
  }
}

void blackboxStartTask() {
  if (bbTaskHandle) return;
  // Kern 0: der Hauptloop laeuft auf Kern 1. Ein Waechter, der sich denselben
  // Kern mit dem teilt, den er ueberwacht, kann von diesem verdraengt werden.
  // Prioritaet 3 liegt ueber dem Loop (1) und ueber dem Sende-Task (1).
  xTaskCreatePinnedToCore(blackboxTaskFn, "bbwatch", 4096, nullptr, 3, &bbTaskHandle, 0);
}

// ── Status ───────────────────────────────────────────────────────────────────
String blackboxStatusJson() {
  String j = "{";
  j += "\"armed\":"        + String(bbArmed ? "true" : "false");
  j += ",\"hb_age_ms\":"   + String(bbLastAgeMs);
  j += ",\"stall_ms\":"    + String((unsigned long)BLACKBOX_STALL_MS);
  j += ",\"phase\":\""     + String(phaseName(blackboxPhase)) + "\"";
  j += ",\"rtc_wdt\":"     + String(bbRtcWdtOn ? "true" : "false");
  j += ",\"rtc_wdt_ms\":"  + String((unsigned long)BLACKBOX_RTCWDT_MS);
  j += ",\"had_previous\":" + String(bbHadPrevious ? "true" : "false");
  j += ",\"write_failed\":" + String(bbWriteFailed ? "true" : "false");
  j += "}";
  return j;
}

#endif // VESC_BRIDGE_UNITY_BUILD
