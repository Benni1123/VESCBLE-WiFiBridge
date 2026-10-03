// Griffheizung, ein Kanal (PWM auf einem frei waehlbaren GPIO)
// Diese Datei wird ueber main.cpp als Unity-Build eingebunden.
// Dadurch bleiben die bisherigen static-Sichtbarkeiten und Abhaengigkeiten exakt erhalten,
// waehrend der Quellcode logisch in einzelne Dateien aufgeteilt ist.
#if defined(VESC_BRIDGE_UNITY_BUILD)
#include "globals.h"
#include "debuglog.h"
#include "logship.h"
#include "heater.h"

#include <Preferences.h>

// ── Grenzen ──────────────────────────────────────────────────────────────────
//
// Die PWM-Frequenz ist NICHT beliebig waehlbar, und das hat einen harten
// technischen Grund: der LEDC-Zaehler des ESP32-S3 teilt den 80-MHz-Takt und
// braucht fuer eine Stufe pro Prozent mindestens 10 Bit Aufloesung. Bei 10 Bit
// gilt Teiler = 80 MHz / (Frequenz * 1024), und der Teiler kann nicht groesser
// als 1024 werden. Daraus folgt ein Minimum von rund 77 Hz.
//
// 100 Hz als untere Grenze liegt sicher darueber. Nach unten ist das keine
// Einschraenkung, die weh tut: eine Heizwendel ist ein Widerstand ohne
// Induktivitaet, sie "hoert" die Frequenz nicht. Relevant ist sie nur fuer die
// Treiberstufe — ein langsamer MOSFET-Treiber mag 200 Hz, ein schneller
// vertraegt auch 20 kHz. Genau deshalb ist der Wert einstellbar und nicht
// festgenagelt.
#define HEAT_FREQ_MIN      100U
#define HEAT_FREQ_MAX    20000U
#define HEAT_FREQ_DEFAULT  200U
#define HEAT_PWM_BITS         10
#define HEAT_PWM_MAX        1023   // (1 << HEAT_PWM_BITS) - 1

// Wie oft neu entschieden wird. Der Aufrufer sitzt im Hauptloop und kommt rund
// 1000-mal pro Sekunde vorbei; eine Heizung braucht das nicht.
#define HEAT_TICK_MS         200UL

// Obergrenze fuer den Testlauf. Ein Test ohne Zeitgrenze waere genau die Falle,
// die man sich damit baut: Seite zuklappen, Scooter abstellen, Heizung laeuft
// weiter. Nach dieser Zeit faellt sie von selbst in den normalen Betrieb
// zurueck.
#define HEAT_TEST_MAX_S       300
#define HEAT_TEST_DEFAULT_S    60

// Ab wann gelten die VESC-Werte als veraltet? Ohne frische Daten ist "faehrt
// gerade" nicht entscheidbar, und im Zweifel wird nicht geheizt.
//
// Der Wert darf NICHT fest sein, und das ist keine Feinheit: das Poll-Intervall
// ist bis 60 s einstellbar. Bei einem festen Fenster von 10 s waere der Wert
// dann die meiste Zeit "veraltet" — die Heizung fiele beim Fahren im
// Sekundentakt auf "keine frischen VESC-Daten" und ginge aus, obwohl das
// Polling einwandfrei laeuft. Das saehe nach einem Fehler in der Heizung aus
// und waere doch nur eine Zahl gegen die andere.
//
// Deshalb fuenf Poll-Abstaende, mindestens aber 30 s.
//
// Vorher waren es drei Abstaende und mindestens 10 s — zu knapp. Im Betrieb
// traten auch ohne angeschlossenen Client Luecken von 10 bis 12 s auf, und bei
// einem Poll-Intervall von 1 s lag die Untergrenze genau dort. Die Heizung
// schaltete dann mitten in der Fahrt ab, obwohl die Verbindung stand.
//
// Teuer ist die Grosszuegigkeit nicht: dass im Stand nicht geheizt wird, haengt
// NICHT an diesem Fenster, sondern an den Haltezeiten weiter unten. Die laufen
// auch bei fehlenden Daten weiter.
#define HEAT_STALE_MIN_MS  30000UL

static inline uint32_t heatStaleMs() {
  uint32_t iv = (uint32_t)cfg_autopoll_interval * 1000UL;
  uint32_t w  = iv * 5UL;
  if (w < HEAT_STALE_MIN_MS) w = HEAT_STALE_MIN_MS;
  return w;
}

static const char HEAT_NS[] = "heat";

// ── Einstellungen ────────────────────────────────────────────────────────────
// Eigener NVS-Namensraum, bewusst getrennt von "vesccfg". Der Grund ist das
// Speicherverhalten der Hauptkonfiguration: dort loest jedes Speichern einen
// Neustart aus, weil BLE-Name, UART-Pins und WLAN-Liste nicht im Betrieb
// uebernommen werden koennen. Fuer einen Leistungsschieber waere ein Neustart
// bei jedem Zug absurd — hier greift alles sofort.
static int   hMode    = 0;      // 0=Aus, 1=An (immer), 2=Auto (nur beim Fahren)
static int   hPin     = -1;     // -1 = kein GPIO gewaehlt
static int   hLevel   = 60;     // Leistung beim Fahren, 0..100 %
// Leistung waehrend des Nachlaufs — also an der Ampel. Danach geht die
// Heizung ganz aus.
//
// Vorher hiess das "Leistung im Stand" und galt DAUERHAFT, der Nachlauf lief
// mit voller Fahrleistung. Das war zweimal falsch herum: an der Ampel braucht
// es keine volle Leistung, und ein Dauerwert im Stand heizt den Akku leer,
// genau das, was die ERPM-Kopplung verhindern soll.
// Absenkung an der Ampel, in Prozent der Fahrleistung. Bewusst RELATIV und
// nicht als fester Wert: wer die Fahrleistung aendert, will die Ampel nicht
// jedes Mal nachstellen. Bei 60 % Fahrt und 40 % Absenkung bleiben 36 %.
static int   hReduce  = 40;     // 0..100 % weniger als beim Fahren

// Wie lange nach dem Anhalten noch VOLL geheizt wird, bevor abgesenkt wird.
// Zwei Sekunden Stillstand vor einer Kreuzung sind kein Halt — ohne diese
// Verzoegerung wuerde die Heizung bei jedem Abbremsen kurz herunterregeln und
// gleich wieder hoch.
static int   hStopDly = 10;     // Sekunden

// ── Boost ───────────────────────────────────────────────────────────────────
// War die Heizung laenger als hBoostAfter aus, laeuft sie beim naechsten
// Einschalten erst einmal hBoostSec lang mit hBoostPct.
//
// Der Sinn: kalte Griffe und kalte Haende brauchen anfangs deutlich mehr
// Leistung als zum Warmhalten. Ohne Boost stellt man die Fahrleistung nach dem
// ersten Kilometer hoch und nach dem dritten wieder runter — und vergisst das
// Runterstellen.
//
// Die Sperrzeit ist das Entscheidende: nach einer kurzen Pause an der Tankstelle
// sind die Griffe noch warm, da waere ein Boost nur Stromverschwendung.
static int   hBoostPct   = 0;     // 0 = Boost aus
static int   hBoostAfter = 30;    // Minuten aus, bevor der Boost wieder greift
static int   hBoostSec   = 120;   // Dauer des Boosts
static int   hFreq    = HEAT_FREQ_DEFAULT;
static int   hErpmOn  = 200;    // |ERPM| darueber = faehrt
static int   hLagSec  = 30;     // Nachlauf nach dem Anhalten (Ampel)
static bool  hInvert  = false;  // Treiberstufe mit invertiertem Eingang
static float hMinVolt = 0.0f;   // Unterspannungsabschaltung, 0 = aus
// Darf eine eigene Statusabfrage eingestreut werden, wenn ein Client an der
// Bruecke haengt? Standard an. Abschaltbar im API-Tab, falls VESC Tool oder
// die App mit dem unverlangten Antwortpaket nicht zurechtkommen.
static bool  hInject  = true;

// ── Laufzeitzustand ──────────────────────────────────────────────────────────
static bool     hAttached   = false;   // PWM laeuft auf hAttachedPin
static int      hAttachedPin = -1;
static int      hAttachedFreq = 0;
static int      hOutPct     = 0;       // was aktuell wirklich rausgeht
static int      hLastDuty   = -1;      // zuletzt geschriebener Rohwert
static uint32_t hLastTick   = 0;
static uint32_t hLastMoveMs = 0;       // wann zuletzt Fahrt erkannt wurde
static bool     hEverMoved  = false;
static uint32_t hLastOnMs   = 0;       // wann zuletzt Leistung anlag (0 = nie)
static uint32_t hBoostUntil = 0;       // 0 = kein Boost aktiv
static uint32_t hTestUntil  = 0;       // 0 = kein Test aktiv
static int      hTestPct    = 0;
static String   hReason     = "off";   // warum die aktuelle Leistung anliegt
static String   hPinError;             // leer = GPIO in Ordnung

// ── Fuer das Protokoll ──────────────────────────────────────────────────────
// Geschrieben wird nur bei einem WECHSEL — Leistung oder Grund. Eine Zeile pro
// Tick waere alle 200 ms eine, das haette den Sendepuffer in Minuten gefuellt
// und das Log unlesbar gemacht. So sind es pro Fahrt eine Handvoll Zeilen.
#define HEAT_LOG_BIT 16                // Bit im Debug-Filter (API-Tab)
static int      hLogPct     = -1;      // zuletzt protokollierte Leistung
static String   hLogReason;            // zuletzt protokollierter Grund
static int32_t  hLastErpm   = 0;       // letzter gesehener ERPM-Wert
static float    hLastVolt   = 0.0f;    // letzte gesehene Spannung
static bool     hLastConn   = false;   // war der VESC beim Entscheiden verbunden?
static uint32_t hLastDataMs = 0;       // Alter der VESC-Daten beim Entscheiden

static WebServer *hSrv = nullptr;

// ── GPIO-Pruefung ────────────────────────────────────────────────────────────
//
// Ein falscher Pin ist hier nicht nur "geht nicht", sondern kann das Geraet
// unerreichbar machen: auf den Flash-/PSRAM-Leitungen zu wackeln endet im
// Absturz, auf den USB-Leitungen verliert man den seriellen Zugang, und auf
// einem Strapping-Pin bootet der Chip unter Umstaenden nicht mehr. Deshalb
// wird der Wunsch geprueft und im Zweifel abgelehnt, statt ihn zu versuchen.
//
// Die Pruefung liefert den Grund als Text zurueck, damit auf der Seite steht
// WARUM der Pin nicht geht — "ungueltig" allein laedt zum Weiterprobieren ein.
static String heatPinProblem(int pin) {
  if (pin < 0)               return "";          // bewusst nicht gesetzt
  if (pin > 48)              return "GPIO gibt es auf dem ESP32-S3 nicht";
  if (pin >= 22 && pin <= 25) return "GPIO 22-25 existiert auf dem ESP32-S3 nicht";
  if (pin >= 26 && pin <= 32) return "GPIO 26-32 gehoert zum SPI-Flash/PSRAM";
  if (pin == 19 || pin == 20) return "GPIO 19/20 ist der USB-Anschluss";
  if (pin == 43 || pin == 44) return "GPIO 43/44 ist die serielle Konsole";
  if (pin == 0 || pin == 3 || pin == 45)
                             return "Strapping-Pin, kann den Start verhindern";
  if (pin == 46)             return "GPIO 46 ist Strapping-Pin und nur Eingang";
  if (pin == cfg_rx_pin || pin == cfg_tx_pin)
                             return "belegt durch die VESC-Schnittstelle (UART)";
  return "";
}

// ── NVS ──────────────────────────────────────────────────────────────────────
static void heatClamp() {
  if (hMode  < 0 || hMode  > 2)   hMode  = 0;
  if (hLevel < 0)                 hLevel = 0;
  if (hLevel > 100)               hLevel = 100;
  if (hReduce < 0)                hReduce = 0;
  if (hReduce > 100)              hReduce = 100;
  if (hStopDly < 0)               hStopDly = 0;
  if (hStopDly > 3600)            hStopDly = 3600;
  if (hBoostPct < 0)              hBoostPct = 0;
  if (hBoostPct > 100)            hBoostPct = 100;
  if (hBoostAfter < 1)            hBoostAfter = 1;
  if (hBoostAfter > 1440)         hBoostAfter = 1440;   // max 24 h
  if (hBoostSec < 10)             hBoostSec = 10;
  if (hBoostSec > 1800)           hBoostSec = 1800;     // max 30 min
  if (hFreq  < (int)HEAT_FREQ_MIN) hFreq = (int)HEAT_FREQ_MIN;
  if (hFreq  > (int)HEAT_FREQ_MAX) hFreq = (int)HEAT_FREQ_MAX;
  if (hErpmOn < 10)               hErpmOn = 10;
  if (hErpmOn > 50000)            hErpmOn = 50000;
  if (hLagSec < 0)                hLagSec = 0;
  if (hLagSec > 3600)             hLagSec = 3600;
  if (hMinVolt < 0.0f)            hMinVolt = 0.0f;
  if (hMinVolt > 200.0f)          hMinVolt = 200.0f;
  if (hPin < -1 || hPin > 48)     hPin = -1;
}

static void heatLoadNvs() {
  Preferences p;
  p.begin(HEAT_NS, true);            // nur lesen
  hMode    = p.getInt  ("mode",    0);
  hPin     = p.getInt  ("pin",     -1);
  hLevel   = p.getInt  ("level",   60);
  // Eigene Schluessel. Vorgaenger waren "idle" (Dauerleistung im Stand) und
  // "hold" (fester Wert an der Ampel) — beide mit anderer Bedeutung, deshalb
  // nicht uebernommen.
  hReduce  = p.getInt  ("red",     40);
  hStopDly = p.getInt  ("sdly",    10);
  hBoostPct   = p.getInt("bpct",   0);
  hBoostAfter = p.getInt("baft",  30);
  hBoostSec   = p.getInt("bsec", 120);
  hFreq    = p.getInt  ("freq",    HEAT_FREQ_DEFAULT);
  hErpmOn  = p.getInt  ("erpm",    200);
  hLagSec  = p.getInt  ("lag",     30);
  hInvert  = p.getBool ("inv",     false);
  hMinVolt = p.getFloat("minv",    0.0f);
  hInject  = p.getBool ("inject",  true);
  p.end();
  heatClamp();
}

static void heatSaveNvs() {
  Preferences p;
  p.begin(HEAT_NS, false);
  p.putInt  ("mode",  hMode);
  p.putInt  ("pin",   hPin);
  p.putInt  ("level", hLevel);
  p.putInt  ("red",   hReduce);
  p.putInt  ("sdly",  hStopDly);
  p.putInt  ("bpct",  hBoostPct);
  p.putInt  ("baft",  hBoostAfter);
  p.putInt  ("bsec",  hBoostSec);
  p.putInt  ("freq",  hFreq);
  p.putInt  ("erpm",  hErpmOn);
  p.putInt  ("lag",   hLagSec);
  p.putBool ("inv",   hInvert);
  p.putFloat("minv",  hMinVolt);
  p.putBool ("inject",hInject);
  p.end();
}

// ── PWM ──────────────────────────────────────────────────────────────────────
//
// Rohwert aus Prozent. Die Invertierung sitzt bewusst HIER und nicht in der
// Entscheidungslogik: so rechnet alles darueber mit "Prozent Heizleistung",
// und ob die Treiberstufe aktiv-high oder aktiv-low ist, bleibt eine Frage der
// Ausgabe. Haette man es oben eingebaut, muesste jede Stelle daran denken.
static inline int heatDutyFor(int pct) {
  if (pct < 0)   pct = 0;
  if (pct > 100) pct = 100;
  int duty = (pct * HEAT_PWM_MAX) / 100;
  return hInvert ? (HEAT_PWM_MAX - duty) : duty;
}

static void heatDetach() {
  if (!hAttached) return;
  // Erst auf "aus" fahren, dann abkoppeln. Umgekehrt bliebe der letzte
  // Pegel am Pin stehen, bis ihn jemand anders anfasst — bei invertierter
  // Treiberstufe waere das volle Leistung.
  ledcWrite(hAttachedPin, heatDutyFor(0));
  ledcDetach(hAttachedPin);
  pinMode(hAttachedPin, INPUT);
  hAttached     = false;
  hAttachedPin  = -1;
  hAttachedFreq = 0;
  hLastDuty     = -1;
}

// Richtet die PWM auf dem gewuenschten Pin ein. Bei unveraenderten Werten ein
// No-Op, damit ein Aufruf im Tick nichts kostet.
static bool heatAttach() {
  hPinError = heatPinProblem(hPin);
  if (hPin < 0 || hPinError.length() > 0) {
    heatDetach();
    return false;
  }
  if (hAttached && hAttachedPin == hPin && hAttachedFreq == hFreq) {
    return true;                      // steht schon richtig
  }

  heatDetach();

  // Vor dem ersten PWM-Takt den Pin definiert auf "aus" legen. Ohne das liegt
  // zwischen pinMode und dem ersten ledcWrite ein undefinierter Pegel an —
  // kurz, aber bei invertierter Stufe eben kurz volle Leistung.
  pinMode(hPin, OUTPUT);
  digitalWrite(hPin, hInvert ? HIGH : LOW);

  if (!ledcAttach((uint8_t)hPin, (uint32_t)hFreq, HEAT_PWM_BITS)) {
    hPinError = "PWM laesst sich auf diesem GPIO nicht einrichten";
    pinMode(hPin, INPUT);
    return false;
  }
  ledcWrite((uint8_t)hPin, heatDutyFor(0));
  hAttached     = true;
  hAttachedPin  = hPin;
  hAttachedFreq = hFreq;
  hLastDuty     = heatDutyFor(0);
  logShipAdd("[HEAT] PWM aktiv: GPIO" + String(hPin) + " " + String(hFreq) + " Hz" +
             (hInvert ? " (invertiert)" : ""));
  return true;
}

static void heatWritePct(int pct) {
  if (!hAttached) { hOutPct = 0; return; }
  // Zeitpunkt der letzten tatsaechlichen Heizleistung festhalten. Daran haengt
  // der Boost: nur nach einer langen Pause soll er greifen.
  if (pct > 0) {
    hLastOnMs = millis();
    if (hLastOnMs == 0) hLastOnMs = 1;   // 0 bedeutet "noch nie"
  }
  int duty = heatDutyFor(pct);
  if (duty != hLastDuty) {
    ledcWrite((uint8_t)hAttachedPin, duty);
    hLastDuty = duty;
  }
  hOutPct = pct;
}

static int  heatHoldPct();            // weiter unten definiert, hier schon gebraucht
static bool heatBoosting(uint32_t now);

// ── Protokoll ────────────────────────────────────────────────────────────────
//
// Was hier steht, ist die Frage, die man sich nach einer Fahrt stellt: ging die
// Heizung an, und wenn sie aus war, warum. Deshalb steht in jeder Zeile der
// ERPM-Wert (bzw. die Spannung), auf dem die Entscheidung beruhte — eine Zeile
// "aus" ohne die Zahl dahinter laesst genau die Frage offen, fuer die man ins
// Log schaut.
//
// Das Praefix [HEAT] ist das, worauf der Knopf "Heizung" im Logbetrachter
// filtert.
static void heatLog(const char *what) {
  if (!cfg_debug || !(cfg_debug_filter & HEAT_LOG_BIT)) return;

  String line = "[HEAT] ";
  line += what;

  if (hReason == "riding") {
    line += " - faehrt, ERPM " + String(hLastErpm) +
            " (Schwelle " + String(hErpmOn) + ")";
  } else if (hReason == "boost") {
    uint32_t left = 0;
    if (heatBoosting(millis())) left = (uint32_t)(hBoostUntil - millis()) / 1000UL;
    line += " - Boost " + String(hBoostPct) + "% (statt " + String(hLevel) +
            "%), noch " + String((unsigned long)left) + "s, ERPM " + String(hLastErpm);
  } else if (hReason == "settle") {
    line += " - gerade angehalten, noch volle Leistung fuer " +
            String((unsigned long)((uint32_t)hStopDly - (millis() - hLastMoveMs) / 1000UL)) + "s";
  } else if (hReason == "hold") {
    uint32_t left = 0;
    uint32_t span = (uint32_t)hLagSec * 1000UL;
    uint32_t gone = millis() - hLastMoveMs;
    if (gone < span) left = (span - gone) / 1000UL;
    line += " - Halt (Ampel), " + String(hReduce) + "% unter " + String(hLevel) +
            "% = " + String(heatHoldPct()) + "%, noch " +
            String((unsigned long)left) + "s bis aus, ERPM " + String(hLastErpm);
  } else if (hReason == "idle") {
    line += " - steht, Nachlauf abgelaufen, ERPM " + String(hLastErpm) +
            " (Schwelle " + String(hErpmOn) + ")";
  } else if (hReason == "nodata") {
    // Zwei verschiedene Ursachen, die nicht in einen Topf gehoeren: entweder
    // der VESC gilt als getrennt, oder er ist verbunden und der letzte Wert
    // ist zu alt. Im Log stand vorher beides als "letzter Wert vor 0s" —
    // eine Zeile, die zur Fehlersuche nichts beitraegt.
    if (!hLastConn) {
      line += " - VESC nicht verbunden, kein ERPM";
    } else {
      line += " - ERPM zu alt: letzter Wert vor " +
              String((unsigned long)(hLastDataMs / 1000UL)) + "s, Fenster " +
              String((unsigned long)(heatStaleMs() / 1000UL)) + "s (5x Poll " +
              String(cfg_autopoll_interval) + "s, min 30s), Haltezeiten laufen weiter";
    }
  } else if (hReason == "undervolt") {
    line += " - Unterspannung " + String(hLastVolt, 1) + " V unter Grenze " +
            String(hMinVolt, 1) + " V";
  } else if (hReason == "test") {
    line += " - Testlauf";
  } else if (hReason == "on") {
    line += " - Modus An (immer)";
  } else if (hReason == "off") {
    line += " - Modus Aus";
  } else if (hReason == "disabled") {
    line += " - in der Konfiguration abgeschaltet";
  } else if (hReason == "nopin") {
    line += " - GPIO " + String(hPin) + " nicht nutzbar: " + hPinError;
  }

  logShipAdd(line);
  uartLogAddRaw(line);
}

// Nach jeder Entscheidung aufrufen. Schreibt nur, wenn sich Leistung ODER
// Grund geaendert hat.
static void heatLogIfChanged() {
  // Beim abgelehnten GPIO gehoert der Grund mit in den Vergleich: wechselt man
  // von einem unbrauchbaren Pin auf einen anderen unbrauchbaren, bleiben
  // Leistung (0) und Grund ("nopin") gleich — die neue Begruendung waere nie
  // im Log erschienen.
  String key = hReason;
  if (hReason == "nopin") key += "|" + hPinError;
  if (hOutPct == hLogPct && key == hLogReason) return;

  if (hOutPct > 0 && hLogPct <= 0) {
    heatLog((String("ein ") + String(hOutPct) + "%").c_str());
  } else if (hOutPct == 0 && hLogPct != 0) {
    heatLog("aus");
  } else if (hOutPct != hLogPct) {
    heatLog((String(hLogPct) + "% -> " + String(hOutPct) + "%").c_str());
  } else if (hOutPct == 0) {
    // Gleiche Leistung (null), anderer Grund: z.B. von einem unbrauchbaren
    // GPIO auf den naechsten. "0%" waere hier eine seltsame Auskunft.
    heatLog("aus");
  } else {
    // Gleiche Leistung, anderer Grund: z.B. von "faehrt" in den Nachlauf.
    // Lesenswert, weil die Zeile erklaert, warum es gleich ausgehen wird.
    heatLog((String(hOutPct) + "%").c_str());
  }

  hLogPct    = hOutPct;
  hLogReason = key;
}

bool heatNeedsErpm() { return cfg_heat_enabled && hMode == 2; }

bool heatInjectWanted() { return cfg_heat_enabled && hInject && hMode == 2; }

// Laeuft gerade ein Boost?
static bool heatBoosting(uint32_t now) {
  return (hBoostUntil != 0 && (int32_t)(now - hBoostUntil) < 0);
}

// Boost scharf machen, wenn lange genug nicht geheizt wurde.
//
// Geprueft wird der Abstand zur letzten tatsaechlichen Heizleistung, nicht zum
// letzten Fahren: wer eine Stunde steht und dabei nicht heizt, soll den Boost
// bekommen, auch wenn der Scooter zwischendurch kurz bewegt wurde.
//
// Beim ersten Einschalten nach einem Neustart ist hLastOnMs noch 0 — dann gilt
// "war lange aus", und der Boost greift. Das ist gewollt: nach einem Neustart
// sind die Griffe mit Sicherheit kalt.
static void heatArmBoost(uint32_t now) {
  if (hBoostPct <= 0) return;              // Boost abgeschaltet
  if (heatBoosting(now)) return;           // laeuft schon
  if (hLastOnMs != 0 &&
      (uint32_t)(now - hLastOnMs) < (uint32_t)hBoostAfter * 60000UL) {
    return;                                // Pause war zu kurz
  }
  hBoostUntil = now + (uint32_t)hBoostSec * 1000UL;
  if (hBoostUntil == 0) hBoostUntil = 1;
}

// Leistung an der Ampel: Fahrleistung minus Absenkung, PROZENTUAL. Einmal
// zentral, damit Anzeige, Protokoll und Ausgabe nie auseinanderlaufen.
//
// 50 % Fahrt und 5 % Absenkung ergeben 47 % (50 x 0,95 = 47,5, abgerundet).
// Der Vorteil gegenueber Prozentpunkten: die Absenkung behaelt ihr Verhaeltnis,
// wenn die Fahrleistung sich aendert.
static int heatHoldPct() {
  int v = (hLevel * (100 - hReduce)) / 100;
  if (v < 0)   v = 0;
  if (v > 100) v = 100;
  return v;
}

// ── Entscheidung ─────────────────────────────────────────────────────────────
// Die Entscheidung selbst. Steckt in einer eigenen Funktion, damit das
// Protokollieren an EINER Stelle sitzt: die Funktion hat neun Ausgaenge, und an
// jeden einzeln eine Protokollzeile zu haengen waere genau die Sorte Arbeit,
// bei der man einen davon vergisst — und dann fehlt im Log ausgerechnet der
// Fall, den man sucht.
static void heatDecide(bool vescConnected, int32_t erpm, float voltage, uint32_t now);

void heatUpdateState(bool vescConnected, int32_t erpm, float voltage) {
  uint32_t now = millis();

  // Selbst gedrosselt: der Aufrufer sitzt im Hauptloop. Signed gerechnet,
  // damit der Ueberlauf von millis() nach 49 Tagen nicht zu einer Pause von
  // 49 Tagen fuehrt.
  if (hLastTick != 0 && (int32_t)(now - hLastTick) < (int32_t)HEAT_TICK_MS) return;
  hLastTick = now;

  // Fuer die Protokollzeilen festhalten, worauf die Entscheidung beruht.
  hLastErpm   = erpm;
  hLastVolt   = voltage;
  hLastConn   = vescConnected;
  hLastDataMs = (vescStatus.lastUpdate != 0) ? (uint32_t)(now - vescStatus.lastUpdate) : 0;

  heatDecide(vescConnected, erpm, voltage, now);
  heatLogIfChanged();
}

static void heatDecide(bool vescConnected, int32_t erpm, float voltage, uint32_t now) {
  // Ohne den Haken in der Konfiguration verhaelt sich das Modul wie "Aus":
  // kein GPIO, keine PWM, keine Protokollzeilen. Der gespeicherte Modus bleibt
  // aber stehen — Haken wieder rein und es laeuft weiter wie vorher.
  if (!cfg_heat_enabled || hMode == 0) {
    if (hAttached) heatWritePct(0);
    // Auch ohne laufende PWM auf 0 setzen: sonst meldet der Status weiter die
    // letzte Leistung, obwohl am Pin nichts mehr liegt. Eine Anzeige, die
    // "40%" sagt, waehrend der Modus auf Aus steht, ist schlimmer als keine.
    hOutPct = 0;
    hReason = cfg_heat_enabled ? "off" : "disabled";
    return;
  }

  if (!heatAttach()) {
    hReason = "nopin";
    hOutPct = 0;
    return;
  }

  // ── Testlauf hat Vorrang ──────────────────────────────────────────────────
  // Beim Aufbau will man die Frequenz und die Leistung durchprobieren, ohne
  // den Scooter dafuer anzuschieben. Der Test umgeht deshalb die
  // Fahrterkennung — aber nicht die Unterspannungsabschaltung.
  bool testing = (hTestUntil != 0 && (int32_t)(now - hTestUntil) < 0);
  if (hTestUntil != 0 && !testing) {
    hTestUntil = 0;
  }

  // ── Unterspannung ─────────────────────────────────────────────────────────
  // Nur bei frischen Daten. Eine Abschaltung auf Grund eines Werts, der von
  // vor zehn Minuten stammt, waere schlimmer als keine Abschaltung: sie
  // wuerde beim Fahren zuschlagen, obwohl der Akku voll ist.
  bool fresh = vescConnected && vescStatus.lastUpdate != 0 &&
               (int32_t)(now - vescStatus.lastUpdate) < (int32_t)heatStaleMs();
  if (hMinVolt > 0.0f && fresh && voltage > 1.0f && voltage < hMinVolt) {
    heatWritePct(0);
    hReason = "undervolt";
    return;
  }

  if (testing) {
    heatWritePct(hTestPct);
    hReason = "test";
    return;
  }

  if (hMode == 1) {                   // An, unabhaengig von der Fahrt
    heatArmBoost(now);
    if (heatBoosting(now)) {
      heatWritePct(hBoostPct);
      hReason = "boost";
      return;
    }
    heatWritePct(hLevel);
    hReason = "on";
    return;
  }

  // ── Auto: nur beim Fahren ─────────────────────────────────────────────────
  //
  // Fehlende Daten werden behandelt wie "faehrt gerade nicht" — NICHT wie ein
  // Not-Aus. Vorher ging die Heizung bei einer Datenluecke sofort auf 0 und
  // beim naechsten Wert wieder hoch; mitten in der Fahrt ist das ein
  // Kaltschlag an den Griffen, und schuld war nur ein verlorener Poll.
  //
  // Jetzt laeuft stattdessen dieselbe Haltesequenz wie nach einem echten
  // Anhalten: erst volle Leistung, dann abgesenkt, dann aus. Der Schutz vor dem
  // Leerheizen bleibt also vollstaendig erhalten — er dauert nur genauso lange
  // wie beim Abstellen, statt sofort zuzuschlagen.
  int32_t a = (erpm < 0) ? -erpm : erpm;
  if (fresh && a > hErpmOn) {
    hLastMoveMs = now;
    hEverMoved  = true;
    heatArmBoost(now);
    if (heatBoosting(now)) {
      heatWritePct(hBoostPct);
      hReason = "boost";
      return;
    }
    heatWritePct(hLevel);
    hReason = "riding";
    return;
  }

  // Noch nie Bewegung gesehen: es gibt keinen Zeitpunkt, ab dem eine
  // Haltesequenz zaehlen koennte. Also aus.
  if (!hEverMoved) {
    heatWritePct(0);
    hReason = fresh ? "idle" : "nodata";
    return;
  }

  // ── Kurz angehalten: noch nichts aendern ────────────────────────────────
  // Zwei Sekunden vor einer Kreuzung sind kein Halt. Ohne diese Stufe wuerde
  // bei jedem Abbremsen abgesenkt und sofort wieder hochgefahren.
  uint32_t stopped = (uint32_t)(now - hLastMoveMs);
  if (stopped < (uint32_t)hStopDly * 1000UL) {
    heatWritePct(hLevel);
    hReason = "settle";
    return;
  }

  // ── Ampel: abgesenkt ────────────────────────────────────────────────────
  // Die Griffe bleiben warm, der Verbrauch sinkt. An einer Ampel merkt man den
  // Unterschied in der Temperatur nicht — in der Restreichweite schon.
  if (hLagSec > 0 && stopped < (uint32_t)hLagSec * 1000UL) {
    heatWritePct(heatHoldPct());
    hReason = "hold";
    return;
  }

  // Haltezeit abgelaufen -> komplett aus. Kein Dauerwert im Stand: der wuerde
  // den Akku leer heizen, waehrend der Scooter herumsteht.
  heatWritePct(0);
  hReason = fresh ? "idle" : "nodata";
}

bool heatIsOn() { return hAttached && hOutPct > 0; }

void heatOff() {
  hTestUntil = 0;
  if (hAttached) heatWritePct(0);
  hOutPct = 0;
  hReason = "off";
}

// ── Status ───────────────────────────────────────────────────────────────────
String heatStatusJson() {
  uint32_t now = millis();
  long testLeft = 0;
  if (hTestUntil != 0 && (int32_t)(now - hTestUntil) < 0) {
    testLeft = (long)(((int32_t)(hTestUntil - now)) / 1000);
  }
  String j = "{";
  j += "\"mode\":"      + String(hMode);
  j += ",\"pin\":"      + String(hPin);
  j += ",\"level\":"    + String(hLevel);
  j += ",\"reduce\":"   + String(hReduce);
  j += ",\"stop_dly\":" + String(hStopDly);
  j += ",\"hold_pct\":" + String(heatHoldPct());
  j += ",\"boost_pct\":"   + String(hBoostPct);
  j += ",\"boost_after\":" + String(hBoostAfter);
  j += ",\"boost_sec\":"   + String(hBoostSec);
  {
    uint32_t nw = millis();
    long bl = heatBoosting(nw) ? (long)((uint32_t)(hBoostUntil - nw) / 1000UL) : 0;
    j += ",\"boost_left\":" + String(bl);
    // Wie lange ist die Heizung schon aus? Daran sieht man auf der Seite, ob
    // der Boost beim naechsten Losfahren greifen wird.
    long offS = (hLastOnMs == 0) ? -1 : (long)((uint32_t)(nw - hLastOnMs) / 1000UL);
    j += ",\"off_for_s\":" + String(offS);
  }
  j += ",\"freq\":"     + String(hFreq);
  j += ",\"erpm_on\":"  + String(hErpmOn);
  j += ",\"lag_s\":"    + String(hLagSec);
  j += ",\"invert\":"   + String(hInvert ? "true" : "false");
  j += ",\"min_volt\":" + String(hMinVolt, 1);
  j += ",\"inject\":"   + String(hInject ? "true" : "false");
  j += ",\"out\":"      + String(hOutPct);
  j += ",\"reason\":\"" + hReason + "\"";
  j += ",\"active\":"   + String(heatIsOn() ? "true" : "false");
  j += ",\"test_left\":" + String(testLeft);
  // Der Grund fuer einen abgelehnten Pin gehoert in die Antwort, sonst steht
  // auf der Seite nur, dass nichts passiert.
  j += ",\"pin_error\":\"" + hPinError + "\"";
  j += ",\"vesc_rx\":"  + String(cfg_rx_pin);
  j += ",\"vesc_tx\":"  + String(cfg_tx_pin);
  j += ",\"freq_min\":" + String((unsigned)HEAT_FREQ_MIN);
  j += ",\"freq_max\":" + String((unsigned)HEAT_FREQ_MAX);
  // Frischefenster mitschicken: auf der Seite soll stehen, WIE LANGE ein
  // ERPM-Wert gilt - sonst wirkt "keine frischen VESC-Daten" willkuerlich.
  j += ",\"stale_ms\":" + String((unsigned long)heatStaleMs());
  j += ",\"poll_s\":"   + String(cfg_autopoll_interval);
  j += "}";
  return j;
}

// ── Seite ────────────────────────────────────────────────────────────────────
// ── Seite ────────────────────────────────────────────────────────────────────
//
// Der Quelltext der Seite steht als eigene Datei unter web/heat.html und wird
// beim Bauen gepackt (tools/gzip_pages.py erzeugt heat_page_gz.h). Eingebunden
// wird nur das Ergebnis.
//
// Warum ueberhaupt: HTML und JavaScript komprimieren auf rund ein Drittel. Der
// ESP packt dabei NICHTS aus — er liefert die gepackten Bytes unveraendert mit
// dem Kopf "Content-Encoding: gzip", und der Browser macht den Rest. Es ist
// also nicht nur kleiner, sondern auch schneller.
#include "heat_page_gz.h"

// ── Routen ───────────────────────────────────────────────────────────────────
//
// Winziger Zahlenleser statt einer JSON-Bibliothek: der Rest des Projekts
// macht es genauso, und fuer neun Felder lohnt sich keine zusaetzliche
// Abhaengigkeit im Flash. Fehlt ein Feld, bleibt der bisherige Wert stehen —
// die Seite schickt beim Schieberegler nicht jedes Mal alles mit.
static bool heatJsonInt(const String &s, const char *key, long &out) {
  int k = s.indexOf(String("\"") + key + "\"");
  if (k < 0) return false;
  int c = s.indexOf(':', k);
  if (c < 0) return false;
  int i = c + 1;
  while (i < (int)s.length() && (s[i] == ' ' || s[i] == '"')) i++;
  int start = i;
  if (i < (int)s.length() && (s[i] == '-' || s[i] == '+')) i++;
  bool any = false;
  while (i < (int)s.length() && isDigit(s[i])) { i++; any = true; }
  if (!any) return false;
  out = s.substring(start, i).toInt();
  return true;
}

static bool heatJsonFloat(const String &s, const char *key, float &out) {
  int k = s.indexOf(String("\"") + key + "\"");
  if (k < 0) return false;
  int c = s.indexOf(':', k);
  if (c < 0) return false;
  int i = c + 1;
  while (i < (int)s.length() && (s[i] == ' ' || s[i] == '"')) i++;
  int start = i;
  if (i < (int)s.length() && (s[i] == '-' || s[i] == '+')) i++;
  bool any = false;
  while (i < (int)s.length() && (isDigit(s[i]) || s[i] == '.' || s[i] == ',')) { i++; any = true; }
  if (!any) return false;
  String v = s.substring(start, i);
  v.replace(',', '.');                 // Komma aus deutschen Tastaturen
  out = v.toFloat();
  return true;
}

static bool heatJsonBool(const String &s, const char *key, bool &out) {
  int k = s.indexOf(String("\"") + key + "\"");
  if (k < 0) return false;
  int c = s.indexOf(':', k);
  if (c < 0) return false;
  String rest = s.substring(c + 1, min((int)s.length(), c + 8));
  rest.toLowerCase();
  if (rest.indexOf("true")  >= 0) { out = true;  return true; }
  if (rest.indexOf("false") >= 0) { out = false; return true; }
  return false;
}

// Nach einer Aenderung SOFORT neu entscheiden, statt auf den naechsten Tick zu
// warten.
//
// Der Grund: die Antwort auf das Speichern enthaelt den Status, und der wurde
// bisher gebaut, BEVOR die neue Einstellung ueberhaupt gewirkt hat. Auf der
// Seite stand dann weiter die alte Leistung — beim naechsten Verschieben des
// Reglers erschien der Wert von davor. Es sah aus, als hinke die Anzeige
// dauerhaft einen Schritt hinterher.
static void heatRecalcNow() {
  hLastTick = 0;   // Drosselung ueberspringen
  heatUpdateState(vescStatus.connected, vescStatus.erpm, vescStatus.voltage);
}

void heatSetup(WebServer *server) {
  heatLoadNvs();
  hPinError = heatPinProblem(hPin);

  // Beim Start NICHT heizen, egal was gespeichert ist. Die PWM wird erst im
  // ersten Tick eingerichtet, und der entscheidet nach den aktuellen
  // VESC-Daten. Ein Geraet, das nach einem Neustart erst einmal mit voller
  // Leistung heizt, weil der letzte Zustand so war, waere die falsche
  // Reihenfolge.
  hOutPct = 0;
  hReason = (hMode == 0) ? "off" : "nodata";

  Serial.printf("[HEAT] Modus %d, GPIO %d, %d Hz, %d%%/%d%%, ERPM>%d, Nachlauf %ds%s%s\n",
                hMode, hPin, hFreq, hLevel, heatHoldPct(), hErpmOn, hLagSec,
                hInvert ? ", invertiert" : "",
                hPinError.length() ? " -- GPIO abgelehnt" : "");
  if (hPinError.length()) {
    logShipAdd("[HEAT] GPIO " + String(hPin) + " abgelehnt: " + hPinError);
  }

  if (!server) return;
  hSrv = server;

  server->on("/heat", HTTP_GET, [server]() {
    // Gepackt ausliefern. Zwei Dinge sind hier Pflicht:
    //
    //   1. Der Kopf MUSS vor send_P() gesetzt werden.
    //   2. Die Laenge MUSS ausdruecklich mitgegeben werden. Die Fassung ohne
    //      Laengenangabe bestimmt sie per strlen() — und gzip-Daten enthalten
    //      Nullbytes. Die Seite kaeme als Fragment an, und zwar je nach Inhalt
    //      mal laenger, mal kuerzer. Genau die Sorte Fehler, die man fuer ein
    //      Netzwerkproblem haelt.
    server->sendHeader("Content-Encoding", "gzip");
    server->send_P(200, "text/html", (PGM_P)HEAT_PAGE_GZ, HEAT_PAGE_GZ_LEN);
  });

  server->on("/api/heat", HTTP_GET, [server]() {
    server->send(200, "application/json", heatStatusJson());
  });

  server->on("/api/heat", HTTP_POST, [server]() {
    String b = server->arg("plain");
    long  v;
    float f;
    bool  bo;

    int    oldPin = hPin, oldFreq = hFreq, oldMode = hMode;
    bool   oldInv = hInvert;

    if (heatJsonInt  (b, "mode",     v))  hMode    = (int)v;
    if (heatJsonInt  (b, "pin",      v))  hPin     = (int)v;
    if (heatJsonInt  (b, "level",    v))  hLevel   = (int)v;
    if (heatJsonInt  (b, "reduce",   v))  hReduce  = (int)v;
    if (heatJsonInt  (b, "stop_dly", v))  hStopDly = (int)v;
    if (heatJsonInt  (b, "boost_pct",   v)) hBoostPct   = (int)v;
    if (heatJsonInt  (b, "boost_after", v)) hBoostAfter = (int)v;
    if (heatJsonInt  (b, "boost_sec",   v)) hBoostSec   = (int)v;
    if (heatJsonInt  (b, "freq",     v))  hFreq    = (int)v;
    if (heatJsonInt  (b, "erpm_on",  v))  hErpmOn  = (int)v;
    if (heatJsonInt  (b, "lag_s",    v))  hLagSec  = (int)v;
    if (heatJsonBool (b, "invert",   bo)) hInvert  = bo;
    if (heatJsonFloat(b, "min_volt", f))  hMinVolt = f;
    if (heatJsonBool (b, "inject",   bo)) hInject  = bo;
    heatClamp();

    // Alles, was den Pegel am Pin aendern kann, zuerst auf "aus" fahren.
    // Wuerde man bei laufender PWM den Pin wechseln oder invertieren, stuende
    // am alten Pin der letzte Pegel und am neuen fuer einen Moment der
    // falsche.
    if (hPin != oldPin || hFreq != oldFreq || hInvert != oldInv || hMode != oldMode) {
      heatDetach();
    }
    hPinError = heatPinProblem(hPin);
    hTestUntil  = 0;             // Einstellungen aendern beendet den Testlauf
    hBoostUntil = 0;             // und einen laufenden Boost

    heatSaveNvs();
    heatRecalcNow();             // damit die Antwort schon den neuen Stand traegt

    bool ok = (hPinError.length() == 0);
    String j = "{\"ok\":" + String(ok ? "true" : "false");
    if (!ok) j += ",\"err\":\"GPIO " + String(hPin) + ": " + hPinError + "\"";
    j += ",\"heat\":" + heatStatusJson() + "}";
    server->send(200, "application/json", j);
  });

  server->on("/api/heat/test", HTTP_POST, [server]() {
    String b = server->arg("plain");
    long pct = 0, sec = 0;
    heatJsonInt(b, "pct", pct);
    heatJsonInt(b, "sec", sec);

    if (sec <= 0 || pct <= 0) {
      hTestUntil = 0;
      heatOff();
      heatRecalcNow();
      server->send(200, "application/json",
                   String("{\"ok\":true,\"heat\":") + heatStatusJson() + "}");
      return;
    }
    if (pct > 100) pct = 100;
    if (sec > HEAT_TEST_MAX_S) sec = HEAT_TEST_MAX_S;

    if (hMode == 0) {
      server->send(200, "application/json",
                   String("{\"ok\":false,\"err\":\"Modus steht auf Aus\",\"heat\":")
                   + heatStatusJson() + "}");
      return;
    }
    hPinError = heatPinProblem(hPin);
    if (hPin < 0 || hPinError.length()) {
      server->send(200, "application/json",
                   String("{\"ok\":false,\"err\":\"Kein gueltiger GPIO\",\"heat\":")
                   + heatStatusJson() + "}");
      return;
    }

    hTestPct   = (int)pct;
    hTestUntil = millis() + (uint32_t)sec * 1000UL;
    if (hTestUntil == 0) hTestUntil = 1;    // 0 bedeutet "kein Test"
    heatRecalcNow();
    logShipAdd("[HEAT] Testlauf " + String((int)pct) + "% fuer " + String((int)sec) + "s");
    server->send(200, "application/json",
                 String("{\"ok\":true,\"heat\":") + heatStatusJson() + "}");
  });
}

#endif // VESC_BRIDGE_UNITY_BUILD
