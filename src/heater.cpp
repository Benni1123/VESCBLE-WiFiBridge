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
// Deshalb drei Poll-Abstaende, mindestens aber 10 s: ein einzelner verlorener
// Poll wirft die Heizung nicht aus der Spur, zwei hintereinander schon.
#define HEAT_STALE_MIN_MS  10000UL

static inline uint32_t heatStaleMs() {
  uint32_t iv = (uint32_t)cfg_autopoll_interval * 1000UL;
  uint32_t w  = iv * 3UL;
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
  int duty = heatDutyFor(pct);
  if (duty != hLastDuty) {
    ledcWrite((uint8_t)hAttachedPin, duty);
    hLastDuty = duty;
  }
  hOutPct = pct;
}

static int heatHoldPct();   // weiter unten definiert, hier schon gebraucht

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
              String((unsigned long)(heatStaleMs() / 1000UL)) + "s (3x Poll " +
              String(cfg_autopoll_interval) + "s)";
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
    heatWritePct(hLevel);
    hReason = "on";
    return;
  }

  // ── Auto: nur beim Fahren ─────────────────────────────────────────────────
  if (!fresh) {
    // Keine verlaesslichen ERPM-Werte -> nicht heizen. Das ist der ganze Zweck
    // der Funktion: der Akku soll nicht leer werden, waehrend das Geraet
    // herumsteht. Ohne Daten ist "steht herum" die wahrscheinlichere Lage.
    heatWritePct(0);
    hReason = "nodata";
    return;
  }

  int32_t a = (erpm < 0) ? -erpm : erpm;
  if (a > hErpmOn) {
    hLastMoveMs = now;
    hEverMoved  = true;
    heatWritePct(hLevel);
    hReason = "riding";
    return;
  }

  // ── Kurz angehalten: noch nichts aendern ────────────────────────────────
  // Zwei Sekunden vor einer Kreuzung sind kein Halt. Ohne diese Stufe wuerde
  // bei jedem Abbremsen abgesenkt und sofort wieder hochgefahren.
  uint32_t stopped = (uint32_t)(now - hLastMoveMs);
  if (hEverMoved && stopped < (uint32_t)hStopDly * 1000UL) {
    heatWritePct(hLevel);
    hReason = "settle";
    return;
  }

  // ── Ampel: abgesenkt ────────────────────────────────────────────────────
  // Die Griffe bleiben warm, der Verbrauch sinkt. An einer Ampel merkt man den
  // Unterschied in der Temperatur nicht — in der Restreichweite schon.
  if (hEverMoved && hLagSec > 0 && stopped < (uint32_t)hLagSec * 1000UL) {
    heatWritePct(heatHoldPct());
    hReason = "hold";
    return;
  }

  // Nachlauf abgelaufen -> komplett aus. Kein Dauerwert im Stand: der wuerde
  // den Akku leer heizen, waehrend der Scooter herumsteht.
  heatWritePct(0);
  hReason = "idle";
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
static const char HEAT_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Griffheizung</title>
  <link rel="stylesheet" href="/style.css">
  <style>
    input[type=range]{width:100%;accent-color:var(--accent);margin-top:6px}
    .rng-row{display:grid;grid-template-columns:1fr 54px;gap:10px;align-items:center;margin-top:6px}
    .rng-val{font-size:13px;color:var(--accent);text-align:right}
    select{width:100%;padding:8px 10px;background:var(--bg3);border:1px solid var(--border);border-radius:4px;color:var(--text);font-family:inherit;font-size:13px}
    .grid2{display:grid;grid-template-columns:1fr 1fr;gap:8px}
    .big{font-size:30px;font-weight:700;line-height:1.1}
    .state{padding:12px;border-radius:8px;background:var(--bg3);border:1px solid var(--border2);display:flex;justify-content:space-between;align-items:center;gap:12px}
    .warn{margin-top:8px;padding:8px 10px;border-left:2px solid #e0a030;border-radius:4px;background:rgba(224,160,48,.08);color:var(--text2);font-size:11px;line-height:1.45}
    .info-note.warn{border-left-color:#e0a030;background:rgba(224,160,48,.08)}
    .err{margin-top:8px;padding:8px 10px;border-left:2px solid var(--err);border-radius:4px;background:rgba(229,115,115,.10);color:#e57373;font-size:12px}
    .info-note{display:none;margin-top:7px;padding:7px 9px;border-left:2px solid var(--accent);border-radius:4px;background:rgba(77,163,255,.07);color:var(--text2);font-size:11px;line-height:1.45}
  body.show-info .info-note:not([data-relevant="0"]){display:block}
    .info-btn{position:fixed;top:12px;right:100px;padding:4px 10px;background:var(--bg2);border:1px solid var(--border);border-radius:4px;color:var(--text2);font-family:'Ndot47',system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;font-size:12px;cursor:pointer}
    .info-btn:hover{border-color:var(--accent);color:var(--accent)}
    .info-btn.on{border-color:var(--accent);color:var(--accent)}
    .btnrow{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}
  </style>
</head>
<body>
<button class="info-btn" onclick="toggleInfo()" id="btn-info" title="Info">i</button>
<button class="theme-btn" onclick="toggleTheme()" id="themeBtn">&#9728;&#65039;</button>
<button class="lang-btn" onclick="toggleLang()" id="langBtn">DE</button>
<div class="wrap">
  <h1 id="appTitle" style="cursor:default;user-select:none;-webkit-tap-highlight-color:transparent">&#x1F6F4; VESC BLE/WiFi</h1>
  <div class="sub" id="statusBar">Loading...</div>
  <div class="tabs">
    <div class="tab" onclick="location.href='/?tab=info'">Info</div>
    <div class="tab" onclick="location.href='/?tab=config'">Config</div>
    <div class="tab" onclick="location.href='/?tab=ota'">OTA Flash</div>
    <div class="tab" id="tab-api-link" style="display:none" onclick="location.href='/?tab=api'">API</div>
    <div class="tab" id="tab-leds-link" style="display:none" onclick="location.href='/leds'">LED</div>
    <div class="tab active" onclick="location.href='/heat'" id="tab-heat">Heizung</div>
  </div>

  <div class="section">
    <h3 id="lbl-state">Status</h3>
    <div class="state">
      <div>
        <div class="big" id="outVal">--</div>
        <div style="font-size:11px;color:var(--text3)" id="outWhy">&nbsp;</div>
      </div>
      <div style="text-align:right;font-size:12px;color:var(--text2)">
        <div id="stErpm">ERPM --</div>
        <div id="stVolt">-- V</div>
      </div>
    </div>
    <div class="err" id="pinErr" style="display:none"></div>
    <div class="warn" id="heatDisabled" style="display:none"></div>
  </div>

  <div class="section">
    <h3 id="lbl-mode-t">Betrieb</h3>
    <label id="lbl-mode">Modus</label>
    <select id="mode" onchange="onMode()">
      <option value="0" id="opt-m0">Aus</option>
      <option value="2" id="opt-m2">Auto (nur beim Fahren)</option>
      <option value="1" id="opt-m1">An (immer)</option>
    </select>
    <div class="info-note" id="lbl-mode-hint"></div>

    <label style="margin-top:12px" id="lbl-level">Leistung beim Fahren</label>
    <div class="rng-row">
      <input type="range" id="level" min="0" max="100" step="1" oninput="lv('level')" onchange="save(0)">
      <span class="rng-val" id="level_v">0%</span>
    </div>

    <div id="autoBox">
      <label style="margin-top:12px" id="lbl-red">Im Halt absenken um</label>
      <div class="rng-row">
        <input type="range" id="reduce" min="0" max="100" step="5" oninput="lv('reduce')" onchange="save(0)">
        <span class="rng-val" id="reduce_v">0%</span>
      </div>
      <div style="font-size:12px;color:var(--text2);margin-top:4px" id="holdCalc">&nbsp;</div>
      <div class="info-note" id="lbl-red-hint"></div>

      <div class="grid2" style="margin-top:12px">
        <div><label id="lbl-sdly">Absenken nach (s)</label><input type="text" id="stop_dly" maxlength="4" placeholder="10"></div>
        <div><label id="lbl-lag">Ganz aus nach (s)</label><input type="text" id="lag_s" maxlength="4" placeholder="30"></div>
      </div>
      <div style="margin-top:12px"><label id="lbl-erpm">ERPM-Schwelle</label><input type="text" id="erpm_on" maxlength="6" placeholder="200"></div>
      <div class="info-note" id="lbl-erpm-hint"></div>
    </div>
  </div>

  <div class="section">
    <h3 id="lbl-hw-t">Hardware</h3>
    <div class="info-note" id="lbl-hw-hint"></div>
    <label class="checkbox-row" style="margin-top:12px">
      <input type="checkbox" id="invert">
      <span id="lbl-invert">Ausgang invertiert (Treiber schaltet bei LOW ein)</span>
    </label>
    <label style="margin-top:12px" id="lbl-minv">Unterspannungsabschaltung (V, 0 = aus)</label>
    <input type="text" id="min_volt" maxlength="6" placeholder="0">
    <div class="info-note" id="lbl-minv-hint"></div>
    <div class="info-note warn" id="lbl-gate"></div>
    <div class="btnrow">
      <button class="btn" onclick="save(1)" id="btn-save">Speichern</button>
    </div>
    <div class="msg" id="msg" style="min-height:16px;margin-top:8px;font-size:12px;color:var(--accent)"></div>
  </div>

  <div class="section">
    <h3 id="lbl-test-t">Testlauf</h3>
    <div class="info-note" id="lbl-test-hint"></div>
    <div class="grid2" style="margin-top:10px">
      <div><label id="lbl-test-pct">Leistung (%)</label><input type="text" id="test_pct" maxlength="3" placeholder="100"></div>
      <div><label id="lbl-test-sec">Dauer (s)</label><input type="text" id="test_sec" maxlength="3" placeholder="60"></div>
    </div>
    <div class="btnrow">
      <button class="btn" onclick="test(1)" id="btn-test">Test starten</button>
      <button class="btn" onclick="test(0)" id="btn-stop">Stopp</button>
    </div>
  </div>
</div>

<script>
var lang=(document.cookie.match(/lang=([a-z]+)/)||[])[1]||(navigator.language.startsWith('de')?'de':'en');
function de(){return lang==='de';}
function L(d,e){return de()?d:e;}
function gid(id){return document.getElementById(id);}

var theme=(document.cookie.match(/theme=([a-z]+)/)||[])[1]||(window.matchMedia('(prefers-color-scheme:dark)').matches?'dark':'light');
function applyTheme(){document.documentElement.setAttribute('data-theme',theme);var b=gid('themeBtn');if(b)b.textContent=theme==='dark'?'☀️':'🌙';document.cookie='theme='+theme+';path=/;max-age=31536000';}
function toggleTheme(){theme=theme==='dark'?'light':'dark';applyTheme();}
applyTheme();
function toggleLang(){lang=lang==='de'?'en':'de';document.cookie='lang='+lang+';path=/;max-age=31536000';location.reload();}
gid('langBtn').textContent=de()?'EN':'DE';

// ── Hinweistexte ein-/ausblenden ────────────────────────────────────────────
// Der Zustand steht in einem Cookie und nicht in localStorage: so gilt er
// seitenuebergreifend (Startseite, /leds, /heat) mit demselben Mechanismus,
// den auch Thema und Sprache benutzen. Standard ist AUS — die Oberflaeche
// soll aufgeraeumt aussehen, bis man die Erklaerungen anfordert.
var hintsOn = (document.cookie.match(/hints=(\d)/)||[])[1] === '1';
function applyHints(){
  if (document.body) document.body.classList.toggle('show-info', hintsOn);
  var b = gid('btn-info');
  if (b){
    if (hintsOn) b.classList.add('on'); else b.classList.remove('on');
    b.title = hintsOn ? (de()?'Hinweise ausblenden':'Hide notes')
                      : (de()?'Hinweise einblenden':'Show notes');
  }
}
function toggleInfo(){
  hintsOn = !hintsOn;
  document.cookie = 'hints=' + (hintsOn?'1':'0') + ';path=/;max-age=31536000';
  applyHints();
}
applyHints();

// Modulreiter ohne Springen: der Zustand der optionalen Reiter kommt aus
// /api/info und damit erst nach einer Netzwerkantwort. Bis dahin fehlen sie,
// und wenn sie auftauchen, ruecken die uebrigen Reiter zur Seite.
//
// Deshalb wird der zuletzt bekannte Zustand in einem Cookie gemerkt und beim
// Laden SOFORT angewandt. Die Antwort korrigiert ihn dann nur noch.
function modsSave(l,h){ document.cookie='mods='+(l?'1':'0')+(h?'1':'0')+';path=/;max-age=31536000'; }
function modsApply(l,h){
  var e1=document.getElementById('tab-leds-link'); if(e1) e1.style.display=l?'':'none';
  var e2=document.getElementById('tab-heat-link'); if(e2) e2.style.display=h?'':'none';
}
(function(){
  var m=(document.cookie.match(/mods=(\d\d)/)||[])[1];
  if(m) modsApply(m[0]==='1', m[1]==='1');
})();

var FMIN=100, FMAX=20000;

function tr(){
  var s=function(id,d,e){var el=gid(id);if(el)el.textContent=L(d,e);};
  s('tab-heat','Heizung','Heater');
  s('lbl-state','Status','Status');
  s('lbl-mode-t','Betrieb','Operation');
  s('lbl-mode','Modus','Mode');
  s('opt-m0','Aus','Off');
  s('opt-m1','An (immer)','On (always)');
  s('opt-m2','Auto (nur beim Fahren)','Auto (only while riding)');
  s('lbl-mode-hint',
    'Auto heizt nur, wenn der VESC Bewegung meldet. Steht der Scooter, geht die Heizung nach dem Nachlauf aus — damit der Akku nicht leer geheizt wird, waehrend das Geraet herumsteht.',
    'Auto heats only while the VESC reports movement. When the scooter stands still the heater turns off after the follow-up time, so the battery is not drained while the device just sits there.');
  s('lbl-level','Leistung beim Fahren','Power while riding');
  s('lbl-red','Im Halt absenken um','Reduce when stopped by');
  s('lbl-red-hint','Prozent der Fahrleistung, nicht ein fester Wert \u2014 wer die Fahrleistung aendert, muss die Ampel nicht nachstellen. Bei 50% Fahrt und 5% Absenkung bleiben 47%. 0% = im Halt genauso warm wie beim Fahren, 100% = im Halt aus.',
                   'Percent of the riding power, not a fixed value \u2014 change the riding power and the stop level follows. 50% riding with 5% reduction leaves 47%. 0% = as warm as while riding, 100% = off while stopped.');
  s('lbl-sdly','Absenken nach (s)','Reduce after (s)');
  s('lbl-erpm','ERPM-Schwelle','ERPM threshold');
  s('lbl-lag','Ganz aus nach (s)','Off completely after (s)');
  s('lbl-erpm-hint','Ueber dieser ERPM gilt "faehrt". Der Nachlauf haelt die Heizung nach dem Anhalten noch so lange an — fuer die Ampel.',
                    'Above this ERPM counts as "riding". The follow-up keeps the heater on for that long after stopping — for traffic lights.');
  s('lbl-hw-t','Hardware','Hardware');
  s('lbl-invert','Ausgang invertiert (Treiber schaltet bei LOW ein)','Output inverted (driver switches on at LOW)');
  s('lbl-minv','Unterspannungsabschaltung (V, 0 = aus)','Low-voltage cutoff (V, 0 = off)');
  s('lbl-minv-hint','Unter dieser Akkuspannung wird nicht geheizt. Greift nur bei frischen VESC-Daten.',
                    'Below this pack voltage the heater stays off. Only applies with fresh VESC data.');
  s('btn-save','Speichern','Save');
  s('lbl-test-t','Testlauf','Test run');
  s('lbl-test-hint','Heizt unabhaengig von ERPM, damit sich Frequenz und Leistung ohne Fahrt ausprobieren lassen. Endet automatisch — die Unterspannungsabschaltung bleibt aktiv.',
                    'Heats regardless of ERPM so frequency and power can be tried without riding. Ends automatically — the low-voltage cutoff stays active.');
  s('lbl-test-pct','Leistung (%)','Power (%)');
  s('lbl-test-sec','Dauer (s)','Duration (s)');
  s('btn-test','Test starten','Start test');
  s('btn-stop','Stopp','Stop');
  var hd=gid('heatDisabled');
  if(hd) hd.innerHTML=L('<b>In der Konfiguration abgeschaltet.</b> Diese Seite laesst sich einstellen, es wird aber nichts geschaltet. Haken setzen unter Config \u2192 Griffheizung.',
                        '<b>Disabled in configuration.</b> This page can be set up, but nothing is switched. Tick the box under Config \u2192 Grip heater.');
  var g=gid('lbl-gate');
  if(g) g.innerHTML=L('<b>Pflicht in der Schaltung:</b> 10 kΩ Pulldown vom Gate des MOSFET nach Masse. Beim Neustart, bei einem Absturz und in den ersten Millisekunden nach dem Einschalten ist der GPIO Eingang — ohne Pulldown entscheidet dann Zufall, ob die Heizung laeuft. Der Code kann das nicht absichern, der Widerstand schon.',
                      '<b>Required in your wiring:</b> a 10 kΩ pulldown from the MOSFET gate to ground. During a restart, a crash and the first milliseconds after power-on the GPIO is an input — without the pulldown, chance decides whether the heater runs. Code cannot guarantee this, the resistor can.');
}
tr();

function why(r){
  var m={
    off:L('Modus aus','Mode off'),
    on:L('An (immer)','On (always)'),
    riding:L('faehrt','riding'),
    settle:L('gerade angehalten','just stopped'),
    hold:L('Halt \u2014 abgesenkt','stopped \u2014 reduced'),
    idle:L('steht \u2014 aus','standing \u2014 off'),
    nodata:L('keine frischen VESC-Daten','no fresh VESC data'),
    undervolt:L('Unterspannung — Akku schonen','low voltage — protecting the pack'),
    test:L('Testlauf','test run'),
    nopin:L('kein gueltiger GPIO','no valid GPIO'),
    disabled:L('in der Konfiguration abgeschaltet','disabled in configuration')
  };
  return m[r]||r;
}
function lv(id){gid(id+'_v').textContent=gid(id).value+'%';}
function onMode(){
  var m=gid('mode').value;
  gid('autoBox').style.display=(m==='2')?'':'none';
  save(0);
}

var dragging=false;
['level','reduce'].forEach(function(id){
  gid(id).addEventListener('pointerdown',function(){dragging=true;});
});
// Loslassen am FENSTER abfangen, nicht am Regler: wer mit dem Finger
// herauswischt, loest sonst nie ein pointerup auf dem Regler aus, und die
// Sperre bliebe fuer immer stehen.
window.addEventListener('pointerup',function(){dragging=false;});
window.addEventListener('pointercancel',function(){dragging=false;});

function fill(d){
  FMIN=d.freq_min||100; FMAX=d.freq_max||20000;
  gid('mode').value=d.mode;
  if(!dragging){
    gid('level').value=d.level; lv('level');
    gid('reduce').value=d.reduce; lv('reduce');
  }
  if(document.activeElement!==gid('erpm_on'))  gid('erpm_on').value=d.erpm_on;
  if(document.activeElement!==gid('lag_s'))    gid('lag_s').value=d.lag_s;
  if(document.activeElement!==gid('stop_dly')) gid('stop_dly').value=d.stop_dly;
  if(document.activeElement!==gid('min_volt')) gid('min_volt').value=d.min_volt;
  gid('invert').checked=d.invert===true;
  gid('autoBox').style.display=(String(d.mode)==='2')?'':'none';
  var hc=gid('holdCalc');
  if(hc) hc.textContent=L('Im Halt also '+d.hold_pct+'% ('+d.reduce+'% unter '+d.level+'% Fahrleistung)',
                          'So '+d.hold_pct+'% while stopped ('+d.reduce+'% below '+d.level+'% riding power)');
  gid('outVal').textContent=d.out+'%';
  gid('outVal').style.color=d.active?'var(--ok)':'var(--text3)';
  var w=why(d.reason);
  if(d.reason==='test'&&d.test_left>0) w+=' ('+d.test_left+'s)';
  gid('outWhy').textContent=w;
  var pe=gid('pinErr');
  if(d.pin_error){pe.style.display='';pe.textContent=d.pin_error;}
  else pe.style.display='none';
  var mh=gid('lbl-mode-hint');
  if(mh&&String(d.mode)==='2') mh.textContent=L(
    'Auto heizt nur, wenn der VESC Bewegung meldet. Steht der Scooter, geht die Heizung nach dem Nachlauf aus \u2014 damit der Akku nicht leer geheizt wird. Ein ERPM-Wert gilt '+Math.round((d.stale_ms||10000)/1000)+' s als frisch (3x das Poll-Intervall von '+(d.poll_s||5)+' s, mindestens 10 s); danach wird vorsichtshalber nicht geheizt.',
    'Auto heats only while the VESC reports movement. When the scooter stands still the heater turns off after the follow-up time, so the battery is not drained. An ERPM reading counts as fresh for '+Math.round((d.stale_ms||10000)/1000)+' s (3x the poll interval of '+(d.poll_s||5)+' s, at least 10 s); after that the heater stays off to be safe.');
  var hh=gid('lbl-hw-hint');
  if(hh) hh.textContent=L(
    'Aktuell: '+(d.pin>=0?('GPIO'+d.pin):'kein GPIO')+', '+d.freq+' Hz. Gesperrt: '+d.vesc_rx+'/'+d.vesc_tx+' (VESC-UART), 19/20 (USB), 43/44 (Konsole), 26-32 (Flash/PSRAM), 0/3/45/46 (Strapping). Frequenz '+FMIN+'-'+FMAX+' Hz — unter '+FMIN+' Hz reicht die PWM-Aufloesung nicht fuer 1%-Schritte.',
    'Current: '+(d.pin>=0?('GPIO'+d.pin):'no GPIO')+', '+d.freq+' Hz. Blocked: '+d.vesc_rx+'/'+d.vesc_tx+' (VESC UART), 19/20 (USB), 43/44 (console), 26-32 (flash/PSRAM), 0/3/45/46 (strapping). Frequency '+FMIN+'-'+FMAX+' Hz — below '+FMIN+' Hz the PWM resolution is not enough for 1% steps.');
}

function status(){
  fetch('/api/info',{cache:'no-store'}).then(function(r){return r.json();}).then(function(d){
    gid('statusBar').textContent=(d.mode==='ap'&&!d.ssid)?'AP: '+d.ip:'WiFi: '+d.ssid+' ('+d.ip+')';
    gid('stErpm').textContent='ERPM '+(d.vesc_connected?d.vesc_erpm:'--');
    gid('stVolt').textContent=(d.vesc_connected?d.vesc_voltage:'--')+' V';
    // Reiter des anderen Moduls einblenden, wenn es aktiv ist. Beide Schalter
    // stehen in /api/info, das hier ohnehin geholt wird.
    modsApply(d.leds_enabled===true, true);   // Heizungsreiter ist hier aktiv
    modsSave (d.leds_enabled===true, d.heat_enabled===true);
    var hw=gid('heatDisabled');
    if(hw) hw.style.display=(d.heat_enabled===false)?'':'none';
    if(d.heat) fill(d.heat);
  }).catch(function(){});
}

function body(){
  return JSON.stringify({
    mode:     parseInt(gid('mode').value)||0,
    level:    parseInt(gid('level').value)||0,
    reduce:   parseInt(gid('reduce').value)||0,
    stop_dly: parseInt(gid('stop_dly').value),
    erpm_on:  parseInt(gid('erpm_on').value)||200,
    lag_s:    parseInt(gid('lag_s').value),
    invert:   gid('invert').checked,
    min_volt: parseFloat(gid('min_volt').value)||0
  });
}

function save(loud){
  fetch('/api/heat',{method:'POST',headers:{'Content-Type':'application/json'},body:body()})
    .then(function(r){return r.json();}).then(function(d){
      if(d.heat) fill(d.heat);
      if(loud) gid('msg').textContent=d.ok?L('Gespeichert','Saved'):(d.err||L('Fehler','Error'));
      if(!d.ok&&!loud) gid('msg').textContent=d.err||L('Fehler','Error');
      if(d.ok&&loud) setTimeout(function(){gid('msg').textContent='';},2500);
    }).catch(function(){gid('msg').textContent=L('Keine Verbindung','No connection');});
}

function test(on){
  var p=on?{pct:parseInt(gid('test_pct').value)||100,sec:parseInt(gid('test_sec').value)||60}:{pct:0,sec:0};
  fetch('/api/heat/test',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(p)})
    .then(function(r){return r.json();}).then(function(d){
      if(d.heat) fill(d.heat);
      gid('msg').textContent=d.ok?(on?L('Test laeuft','Test running'):L('Test gestoppt','Test stopped')):(d.err||L('Fehler','Error'));
      setTimeout(function(){gid('msg').textContent='';},2500);
    }).catch(function(){gid('msg').textContent=L('Keine Verbindung','No connection');});
}

// ── Debug-Freischaltung ──────────────────────────────────────────────────
//
// Der API-Tab haengt an einem serverseitigen RAM-Flag, das 8x Tippen auf den
// Titel setzt. Ohne diesen Block fehlte der Tab hier nicht nur — man kam auch
// nicht an ihn heran: die Startseite schaltet frei, aber auf dieser Seite gab
// es weder den Tab noch das Tippen, und wer hier gelandet war, musste erst
// wieder zurueck. Deshalb steht die Freischaltung auf jeder Seite, die eine
// Tableiste hat.
var dbgUnlocked=false;
function applyDebugState(){
  var el=gid('tab-api-link'); if(el) el.style.display=dbgUnlocked?'':'none';
}
function heatToast(m){
  var t=gid('heatToast');
  if(!t){
    t=document.createElement('div'); t.id='heatToast';
    t.style.cssText='position:fixed;top:12px;left:50%;transform:translateX(-50%);padding:10px 18px;border-radius:6px;font-family:inherit;font-size:13px;z-index:9999;transition:opacity .3s;pointer-events:none;background:var(--bg3);border:1px solid var(--accent);color:var(--text)';
    document.body.appendChild(t);
  }
  t.textContent=m; t.style.opacity='1';
  clearTimeout(t._h); t._h=setTimeout(function(){t.style.opacity='0';},3000);
}
function checkApiUnlock(){
  fetch('/api/debug/unlock',{cache:'no-store'}).then(function(r){return r.ok?r.json():null;}).then(function(j){
    dbgUnlocked=!!(j&&j.unlocked); applyDebugState();
  }).catch(function(){});
}
function unlockDebug(){
  fetch('/api/debug/unlock',{method:'POST'}).then(function(){
    dbgUnlocked=true; applyDebugState();
    heatToast(L('\uD83D\uDD13 Debug aktiv \u2013 bis Neustart','\uD83D\uDD13 Debug active \u2013 until reboot'));
  }).catch(function(){});
}
(function(){
  var title=gid('appTitle'); if(!title) return;
  var taps=0, timer=null;
  title.addEventListener('click',function(){
    if(dbgUnlocked) return;
    taps++;
    if(timer) clearTimeout(timer);
    timer=setTimeout(function(){ taps=0; },1500);
    if(taps>=8){
      taps=0;
      if(timer){ clearTimeout(timer); timer=null; }
      unlockDebug();
    }
  });
})();

checkApiUnlock();
status();
setInterval(status,2000);
</script>
</body>
</html>
)rawliteral";

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
    server->send_P(200, "text/html", HEAT_HTML);
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
    hTestUntil = 0;              // Einstellungen aendern beendet den Testlauf

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
