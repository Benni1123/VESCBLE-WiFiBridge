#include "leds.h"
#include "debuglog.h"
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <stdarg.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ── Mehrkanal-LED-Steuerung (bis zu 4 Kanaele) ────────────────────────────────
#define LED_MAX_CHANNELS 4
#define qsub8(a, b) ((a)>(b)?(a)-(b):0)
#define qadd8(a, b) ((a)+(b)>255?255:(a)+(b))
// GLOBAL SYNC CLOCK
static volatile uint32_t ledsFrameNow = 0;

struct LedChannel {
  int  pin     = -1;     
  int  count   = 30;    
  bool synced  = false; 
  int  effect  = 0;     // 0=Aus, 1=Solid, 2=KR, 3=Pol(EU), 4=Pol(US Weiss), 5=Pol(US WigWag), 6=Rainbow, 7=Breath, 8=Sparkle, 9=Meteor, 10=Eigenes Muster
  int  r       = 0;
  int  g       = 0;
  int  b       = 255;
  int  bright  = 128;
  int  krSpeed = 50;    // Tempo 1..100: 1 = sehr langsam, 100 = sehr schnell
  int  krWidth = 3;     // Universal-Parameter (Breite, Menge, Dichte)
  int  polHz   = 4;     // Blaulicht-Sonderfall: echte Frequenz 1..10 Hz
  bool swapColors = false; // Tauscht bei US-Police Links/Rechts
  int  colorOrder = 0;     // Index in LED_COLOR_ORDERS (0=GRB Default, 1=RGB, ...)
  int  polRole    = 0;     // Police-Rolle: 0=Teilen (intern), 1=Links, 2=Rechts
  bool pinLow     = false; // true = Datenleitung als GPIO aktiv LOW (RMT abgekoppelt, Aus)
  Adafruit_NeoPixel *strip = nullptr;

  // Animationszustaende
  int  krPos      = 0;
  int  krDir      = 1;
  unsigned long krLastStep = 0;

  bool polOn      = false;
  bool polForce   = false; // erzwingt Neuzeichnen bei Farb-/Helligkeits-/Parameteraenderung waehrend Police laeuft
  int  polSig     = -1;    // Signatur des zuletzt gezeichneten Sichtzustands (An/Aus + Phase); -1 = "noch nie gezeichnet"
  uint16_t rbHue  = 0;     // zeitbasierter Hue-Akkumulator fuer Rainbow (laeuft sauber ueber, kein int-Overflow)
};

static LedChannel ch[LED_MAX_CHANNELS];
static int        channelCount = 1;
static unsigned long ledKeepaliveMs = 0;     // 0 = aus (Default); nur im Debug-Modus einstellbar, wird in NVS persistiert

static const int PIN_MIN = 0, PIN_MAX = 48;
static const int CNT_MIN = 1, CNT_MAX = 300;

// ── Eigene LED-Muster / Live-Pixelzustand ────────────────────────────────────
// Pro Kanal wird fuer jede moegliche LED ein logischer RGB-Wert gehalten.
// Das sind bei 4 x 300 LEDs nur 3600 Byte RAM.
static uint8_t customRgb[LED_MAX_CHANNELS][CNT_MAX][3] = {{{0}}};
static int8_t  customPreset[LED_MAX_CHANNELS]          = { -1, -1, -1, -1 };
static bool    customModified[LED_MAX_CHANNELS]        = { true, true, true, true };

// Animation des eigenen Musters. Das Live-Muster bleibt immer die Quelle und kann
// waehrend einer laufenden Animation weiter editiert werden.
enum CustomAnimMode : uint8_t {
  CUSTOM_ANIM_STATIC      = 0,
  CUSTOM_ANIM_MOVE        = 1,
  CUSTOM_ANIM_PINGPONG    = 2,
  CUSTOM_ANIM_BRIGHT_WAVE = 3,
  CUSTOM_ANIM_COLORWAVES  = 4,
  CUSTOM_ANIM_TWINKLE     = 5,
  CUSTOM_ANIM_MORPH       = 6
};

static uint8_t  customAnim[LED_MAX_CHANNELS]        = { 0, 0, 0, 0 };
static uint8_t  customAnimSpeed[LED_MAX_CHANNELS]   = { 50, 50, 50, 50 }; // 1..100, hoeher = schneller
static uint8_t  customAnimAmount[LED_MAX_CHANNELS]  = { 70, 70, 70, 70 }; // Wellentiefe/Dichte
static bool     customAnimReverse[LED_MAX_CHANNELS] = { false, false, false, false };
static int8_t   customMorphPreset[LED_MAX_CHANNELS] = { -1, -1, -1, -1 };
static int16_t  customMorphLen[LED_MAX_CHANNELS]    = { 0, 0, 0, 0 };
static uint8_t  customMorphRgb[LED_MAX_CHANNELS][CNT_MAX][3] = {{{0}}};

#define LED_PATTERN_MAX      12
#define LED_PATTERN_NAME_MAX 31

static String patKey(int slot, const char *suffix) {
  return String("p") + String(slot) + suffix;
}

static String jsonEscape(const String &in) {
  String out; out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '\\' || c == '"') { out += '\\'; out += c; }
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if ((uint8_t)c >= 0x20) out += c;
  }
  return out;
}

static bool patternExists(int slot) {
  if (slot < 0 || slot >= LED_PATTERN_MAX) return false;
  Preferences p; p.begin("ledpat", false);
  bool used = p.getBool(patKey(slot, "u").c_str(), false);
  p.end();
  return used;
}

static bool patternLoad(int slot, String &name, int &len, uint8_t *data, size_t dataBytes) {
  if (slot < 0 || slot >= LED_PATTERN_MAX || !data) return false;
  Preferences p; p.begin("ledpat", false);
  bool used = p.getBool(patKey(slot, "u").c_str(), false);
  if (!used) { p.end(); return false; }
  name = p.getString(patKey(slot, "n").c_str(), "Muster");
  len  = p.getInt(patKey(slot, "l").c_str(), 0);
  if (len < 1) len = 1;
  if (len > CNT_MAX) len = CNT_MAX;
  memset(data, 0, dataBytes);
  size_t want = (size_t)len * 3U;
  if (want > dataBytes) want = dataBytes;
  size_t have = 0;
  String dataKey = patKey(slot, "d");
  if (p.isKey(dataKey.c_str())) have = p.getBytesLength(dataKey.c_str());
  size_t rd = have < want ? have : want;
  if (rd) p.getBytes(patKey(slot, "d").c_str(), data, rd);
  p.end();
  return true;
}

static bool patternSave(int slot, const String &name, int len, const uint8_t *data) {
  if (slot < 0 || slot >= LED_PATTERN_MAX || !data) return false;
  if (len < 1) len = 1;
  if (len > CNT_MAX) len = CNT_MAX;
  String nm = name; nm.trim();
  if (nm.length() == 0) nm = "Farbe " + String(slot + 1);
  if (nm.length() > LED_PATTERN_NAME_MAX) nm.remove(LED_PATTERN_NAME_MAX);

  Preferences p; p.begin("ledpat", false);
  p.putBool  (patKey(slot, "u").c_str(), true);
  p.putString(patKey(slot, "n").c_str(), nm);
  p.putInt   (patKey(slot, "l").c_str(), len);
  size_t wr = p.putBytes(patKey(slot, "d").c_str(), data, (size_t)len * 3U);
  p.end();
  return wr == (size_t)len * 3U;
}

static void patternDelete(int slot) {
  if (slot < 0 || slot >= LED_PATTERN_MAX) return;
  Preferences p; p.begin("ledpat", false);
  p.remove(patKey(slot, "u").c_str());
  p.remove(patKey(slot, "n").c_str());
  p.remove(patKey(slot, "l").c_str());
  p.remove(patKey(slot, "d").c_str());
  p.end();
}

static int patternFindFree() {
  for (int i = 0; i < LED_PATTERN_MAX; i++) if (!patternExists(i)) return i;
  return -1;
}

// Laedt das zweite Preset fuer Preset-Morph in einen RAM-Cache. Ein kuerzeres
// Zielpreset wird hinten schwarz aufgefuellt, ein laengeres beim Rendern gekappt.
static void loadMorphCache(int i) {
  if (i < 0 || i >= LED_MAX_CHANNELS) return;
  memset(customMorphRgb[i], 0, sizeof(customMorphRgb[i]));
  customMorphLen[i] = 0;
  int slot = customMorphPreset[i];
  if (slot < 0 || slot >= LED_PATTERN_MAX) return;

  static uint8_t data[CNT_MAX * 3];
  String name; int len = 0;
  if (!patternLoad(slot, name, len, data, sizeof(data))) {
    customMorphPreset[i] = -1;
    return;
  }
  if (len < 0) len = 0;
  if (len > CNT_MAX) len = CNT_MAX;
  if (len > 0) memcpy(customMorphRgb[i], data, (size_t)len * 3U);
  customMorphLen[i] = (int16_t)len;
}

static void clampCustomAnim(int i) {
  if (i < 0 || i >= LED_MAX_CHANNELS) return;
  if (customAnim[i] > CUSTOM_ANIM_MORPH) customAnim[i] = CUSTOM_ANIM_STATIC;
  if (customAnimSpeed[i] < 1)   customAnimSpeed[i] = 1;
  if (customAnimSpeed[i] > 100) customAnimSpeed[i] = 100;
  if (customAnimAmount[i] < 1)   customAnimAmount[i] = 1;
  if (customAnimAmount[i] > 100) customAnimAmount[i] = 100;
  if (customMorphPreset[i] < -1 || customMorphPreset[i] >= LED_PATTERN_MAX) customMorphPreset[i] = -1;
}

// ── LED-Task auf Kern 1 ───────────────────────────────────────────────────────
static SemaphoreHandle_t ledsMutex      = nullptr;
static TaskHandle_t      ledsTaskHandle = nullptr;
static volatile bool     ledsEnabled    = false;
static bool              ledsStripsReady = false;   // Strips schon frueh initialisiert + geblankt?
static volatile int32_t  ledsLatestErpm = 0;

static inline void ledsLock()   { if (ledsMutex) xSemaphoreTake(ledsMutex, portMAX_DELAY); }
static inline void ledsUnlock() { if (ledsMutex) xSemaphoreGive(ledsMutex); }

static Preferences ledPrefs;
static WebServer  *ledServer = nullptr;

// ── Debug-Log aus dem LED-Task ───────────────────────────────────────────────
// dlog()/uartLogAdd() haengen an einem std::vector ohne eigenen Mutex. Der
// LED-Task laeuft auf Kern 1, die Web-Endpunkte auf Kern 0 - ein direkter
// dlog()-Aufruf von hier waere also ein Datenrennen auf dem Log-Vektor.
// Darum werden Meldungen nur unter ledsLock() gepuffert und spaeter aus dem
// Hauptloop (ledsUpdateState) ausgegeben.
#define LED_LOG_QUEUE 16
#define LED_LOG_LINE  72
static char    ledLogQueue[LED_LOG_QUEUE][LED_LOG_LINE];
static uint8_t ledLogHead = 0, ledLogTail = 0;

static void ledLogPush(const char *fmt, ...) {
  char buf[LED_LOG_LINE];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  uint8_t next = (uint8_t)((ledLogHead + 1) % LED_LOG_QUEUE);
  if (next == ledLogTail) ledLogTail = (uint8_t)((ledLogTail + 1) % LED_LOG_QUEUE);  // voll -> aeltesten verwerfen
  strncpy(ledLogQueue[ledLogHead], buf, LED_LOG_LINE);
  ledLogQueue[ledLogHead][LED_LOG_LINE - 1] = 0;
  ledLogHead = next;
}

// Aus dem Hauptloop aufgerufen. Nimmt den Mutex NICHT blockierend: haelt der
// LED-Task ihn gerade (z.B. waehrend der Blackout-Delays in ledsPullLow),
// wird einfach beim naechsten Durchlauf geleert - der Loop stockt nie.
static void ledsFlushLog() {
  if (!ledsMutex) return;
  if (xSemaphoreTake(ledsMutex, 0) != pdTRUE) return;
  char lines[LED_LOG_QUEUE][LED_LOG_LINE];
  int n = 0;
  while (ledLogTail != ledLogHead && n < LED_LOG_QUEUE) {
    strncpy(lines[n], ledLogQueue[ledLogTail], LED_LOG_LINE);
    lines[n][LED_LOG_LINE - 1] = 0;
    ledLogTail = (uint8_t)((ledLogTail + 1) % LED_LOG_QUEUE);
    n++;
  }
  ledsUnlock();
  for (int i = 0; i < n; i++) dlog("%s\n", lines[i]);
}

// ── FASTLED-Style Helper: Sanftes, organisches Ausfaden (Fade to Black) ────────
static void fadePixel(Adafruit_NeoPixel *strip, int p, uint8_t fadeBy) {
  uint32_t col = strip->getPixelColor(p);
  if (col == 0) return;
  uint8_t r = (col >> 16) & 0xff;
  uint8_t g = (col >> 8)  & 0xff;
  uint8_t b =  col        & 0xff;
  
  r = (r * (255 - fadeBy)) / 256;
  g = (g * (255 - fadeBy)) / 256;
  b = (b * (255 - fadeBy)) / 256;
  strip->setPixelColor(p, r, g, b);
}

// Farb-Reihenfolgen (Byte-Order) des LED-Chips. Index == LedChannel.colorOrder.
static const uint16_t LED_COLOR_ORDERS[] = { NEO_GRB, NEO_RGB, NEO_BRG, NEO_RBG, NEO_GBR, NEO_BGR };
#define LED_COLOR_ORDER_COUNT 6

static const unsigned long LED_FRAME_MS = 25;

// ── Einheitliche Tempo-Skala 1..100 ──────────────────────────────────────────
// Fuer alle normalen Tempo-Effekte (AUSGENOMMEN Blaulicht) gilt:
//   1   = sehr langsam
//   100 = sehr schnell
// Blaulicht EU/US bleibt absichtlich separat bei echten 1..10 Hz.
// Die Kurve ist logarithmisch. Dadurch bleibt der langsame Bereich fein dosierbar,
// waehrend 100 trotzdem wirklich schnell ist. slowMs/fastMs definieren nur den
// fuer den jeweiligen Effekttyp sinnvollen Zeitbereich; die Reglerkurve ist gleich.
static inline int clampTempo100(int v) {
  if (v < 1) return 1;
  if (v > 100) return 100;
  return v;
}

static uint32_t tempoPeriodMs(int tempo, uint32_t slowMs, uint32_t fastMs) {
  tempo = clampTempo100(tempo);
  if (slowMs < 1U) slowMs = 1U;
  if (fastMs < 1U) fastMs = 1U;
  if (slowMs < fastMs) { uint32_t t = slowMs; slowMs = fastMs; fastMs = t; }

  const float x = (float)(tempo - 1) / 99.0f;
  const float ratio = (float)fastMs / (float)slowMs;
  float ms = (float)slowMs * powf(ratio, x);
  if (ms < 1.0f) ms = 1.0f;
  return (uint32_t)(ms + 0.5f);
}

// Ein kompletter sichtbarer Bewegungs-/Wellenzyklus nutzt fuer die meisten
// Effekte denselben Bereich: bei 1 etwa 3 Minuten, bei 100 etwa 0,5 Sekunden.
static inline uint32_t tempoMainCycleMs(int tempo) {
  return tempoPeriodMs(tempo, 180000U, 500U);
}

static uint32_t tempoStepFromCycle(int tempo, int steps) {
  if (steps < 1) steps = 1;
  uint32_t ms = tempoMainCycleMs(tempo) / (uint32_t)steps;
  if (ms < LED_FRAME_MS) ms = LED_FRAME_MS;
  return ms;
}

// Migration der alten Standard-Tempo-Werte. Frueher war krSpeed eine
// Millisekunden-Angabe (kleiner = schneller) und polHz echte 1..10 Hz.
static int legacyStepMsToTempo100(int oldMs) {
  if (oldMs <= 1) return 100;
  if (oldMs >= 200) return 1;
  int v = 100 - (int)(((long)(oldMs - 1) * 99L + 99L) / 199L);
  return clampTempo100(v);
}

// Rueckmigration fuer Builds, in denen Blaulicht kurzzeitig ebenfalls auf
// die 1..100-Tempo-Skala umgestellt war. Danach bleibt polHz wieder echte Hz.
static int tempo100ToPoliceHz(int tempo) {
  tempo = clampTempo100(tempo);
  // Inverse der damaligen linearen 1..10-Hz -> 1..100-Abbildung.
  int hz = 1 + (int)(((long)(tempo - 1) * 9L + 49L) / 99L);
  if (hz < 1) hz = 1;
  if (hz > 10) hz = 10;
  return hz;
}

// ── Clamp ─────────────────────────────────────────────────────────────────────
static void clampChannel(int i) {
  LedChannel &c = ch[i];
  if (c.pin < 0) c.pin = -1;                  // < 0 = unbelegt (leeres GPIO-Feld)
  else if (c.pin > PIN_MAX) c.pin = PIN_MAX;  // gueltige Pins auf 0..PIN_MAX begrenzen
  if (c.count < CNT_MIN) c.count = CNT_MIN; if (c.count > CNT_MAX) c.count = CNT_MAX;
  if (c.colorOrder < 0 || c.colorOrder >= LED_COLOR_ORDER_COUNT) c.colorOrder = 0;
  if (c.polRole < 0 || c.polRole > 2) c.polRole = 0;
  if (c.effect < 0 || c.effect > 10) c.effect = 0;
  if (c.r < 0) c.r = 0; if (c.r > 255) c.r = 255;
  if (c.g < 0) c.g = 0; if (c.g > 255) c.g = 255;
  if (c.b < 0) c.b = 0; if (c.b > 255) c.b = 255;
  if (c.bright < 0) c.bright = 0; if (c.bright > 255) c.bright = 255;
  
  if (c.krSpeed < 1) c.krSpeed = 1; if (c.krSpeed > 100) c.krSpeed = 100;
  if (c.krWidth < 1) c.krWidth = 1; if (c.krWidth > 50) c.krWidth = 50;
  if (c.polHz < 1) c.polHz = 1;     if (c.polHz > 10) c.polHz = 10;
}
static void clampAll() { for (int i = 0; i < LED_MAX_CHANNELS; i++) clampChannel(i); }

// ── Config laden / speichern ──────────────────────────────────────────────────
static void ledsLoadConfig() {
  ledPrefs.begin("leds", false);
  channelCount = ledPrefs.getInt("chcnt", 1);
  ledKeepaliveMs = (unsigned long) ledPrefs.getInt("kams", 0);
  if (channelCount < 1) channelCount = 1;
  if (channelCount > LED_MAX_CHANNELS) channelCount = LED_MAX_CHANNELS;

  // Ab Tempo-Schema 2 nutzen alle normalen Animationen 1..100.
  // Blaulicht bleibt davon getrennt und verwendet echte 1..10 Hz.
  const bool tempoSchema2 = ledPrefs.getBool("tempo100", false);
  const bool policeHz10   = ledPrefs.getBool("polhz10", false);

  for (int i = 0; i < LED_MAX_CHANNELS; i++) {
    String p = "c" + String(i);
    ch[i].pin       = ledPrefs.getInt ((p + "pin").c_str(), -1);
    ch[i].count     = ledPrefs.getInt ((p + "cnt").c_str(), 30);
    ch[i].effect    = ledPrefs.getInt ((p + "eff").c_str(), 0);
    ch[i].r         = ledPrefs.getInt ((p + "r").c_str(),   0);
    ch[i].g         = ledPrefs.getInt ((p + "g").c_str(),   0);
    ch[i].b         = ledPrefs.getInt ((p + "b").c_str(),   255);
    ch[i].bright    = ledPrefs.getInt ((p + "br").c_str(),  128);
    const bool hadOldSpd = ledPrefs.isKey((p + "spd").c_str());
    const bool hadOldPol = ledPrefs.isKey((p + "phz").c_str());
    ch[i].krSpeed   = ledPrefs.getInt ((p + "spd").c_str(), 50);
    ch[i].krWidth   = ledPrefs.getInt ((p + "wid").c_str(), 3);
    ch[i].polHz     = ledPrefs.getInt ((p + "phz").c_str(), 4);
    if (!tempoSchema2) {
      // Alte Firmware: krSpeed war ms, polHz war bereits echte 1..10 Hz.
      ch[i].krSpeed = hadOldSpd ? legacyStepMsToTempo100(ch[i].krSpeed) : 50;
      if (!hadOldPol) ch[i].polHz = 4;
    } else if (!policeHz10) {
      // Nur die kurzzeitig veroeffentlichte Unified-Tempo-Version hatte
      // Blaulicht ebenfalls 1..100. Einmalig wieder auf echte Hz abbilden.
      ch[i].polHz = hadOldPol ? tempo100ToPoliceHz(ch[i].polHz) : 4;
    }
    ch[i].synced    = ledPrefs.getBool((p + "syn").c_str(), false);
    ch[i].swapColors = ledPrefs.getBool((p + "swp").c_str(), false);
    ch[i].colorOrder = ledPrefs.getInt ((p + "co").c_str(), 0);
    ch[i].polRole    = ledPrefs.getInt ((p + "prl").c_str(), 0);

    customPreset[i]      = (int8_t) ledPrefs.getInt ((p + "psi").c_str(), -1);
    customModified[i]    =          ledPrefs.getBool((p + "pmd").c_str(), true);
    customAnim[i]        = (uint8_t)ledPrefs.getInt ((p + "cam").c_str(), CUSTOM_ANIM_STATIC);
    customAnimSpeed[i]   = (uint8_t)ledPrefs.getInt ((p + "cas").c_str(), 50);
    customAnimAmount[i]  = (uint8_t)ledPrefs.getInt ((p + "caa").c_str(), 70);
    customAnimReverse[i] =          ledPrefs.getBool((p + "car").c_str(), false);
    customMorphPreset[i] = (int8_t) ledPrefs.getInt ((p + "cmp").c_str(), -1);
    clampCustomAnim(i);

    // Erst clampen, dann nur so viele Live-Pixel laden, wie der Kanal aktuell hat.
    // Wird ein 5er-Kanal spaeter zu 8 LEDs, sind die neuen LEDs dadurch schwarz.
    clampChannel(i);
    memset(customRgb[i], 0, sizeof(customRgb[i]));
    String pxk = p + "px";
    size_t have = 0;
    if (ledPrefs.isKey(pxk.c_str())) have = ledPrefs.getBytesLength(pxk.c_str());
    size_t want = (size_t)ch[i].count * 3U;
    if (want > sizeof(customRgb[i])) want = sizeof(customRgb[i]);
    size_t rd = have < want ? have : want;
    if (rd) ledPrefs.getBytes(pxk.c_str(), customRgb[i], rd);
    if (customPreset[i] < -1 || customPreset[i] >= LED_PATTERN_MAX) customPreset[i] = -1;

    if (!tempoSchema2) ledPrefs.putInt((p + "spd").c_str(), ch[i].krSpeed);
    if (!policeHz10 || !tempoSchema2) ledPrefs.putInt((p + "phz").c_str(), ch[i].polHz);
  }
  if (!tempoSchema2) ledPrefs.putBool("tempo100", true);
  if (!policeHz10)   ledPrefs.putBool("polhz10", true);
  ledPrefs.end();
  clampAll();
  for (int i = 0; i < LED_MAX_CHANNELS; i++) loadMorphCache(i);
}

static void ledsSaveConfig() {
  clampAll();
  ledPrefs.begin("leds", false);
  ledPrefs.putInt("chcnt", channelCount);
  ledPrefs.putInt("kams", (int) ledKeepaliveMs);
  for (int i = 0; i < LED_MAX_CHANNELS; i++) {
    String p = "c" + String(i);
    ledPrefs.putInt ((p + "pin").c_str(), ch[i].pin);
    ledPrefs.putInt ((p + "cnt").c_str(), ch[i].count);
    ledPrefs.putInt ((p + "eff").c_str(), ch[i].effect);
    ledPrefs.putInt ((p + "r").c_str(),   ch[i].r);
    ledPrefs.putInt ((p + "g").c_str(),   ch[i].g);
    ledPrefs.putInt ((p + "b").c_str(),   ch[i].b);
    ledPrefs.putInt ((p + "br").c_str(),  ch[i].bright);
    ledPrefs.putInt ((p + "spd").c_str(), ch[i].krSpeed);
    ledPrefs.putInt ((p + "wid").c_str(), ch[i].krWidth);
    ledPrefs.putInt ((p + "phz").c_str(), ch[i].polHz);
    ledPrefs.putBool((p + "syn").c_str(), ch[i].synced);
    ledPrefs.putBool((p + "swp").c_str(), ch[i].swapColors);
    ledPrefs.putInt ((p + "co").c_str(), ch[i].colorOrder);
    ledPrefs.putInt ((p + "prl").c_str(), ch[i].polRole);

    ledPrefs.putInt ((p + "psi").c_str(), customPreset[i]);
    ledPrefs.putBool((p + "pmd").c_str(), customModified[i]);
    ledPrefs.putInt ((p + "cam").c_str(), customAnim[i]);
    ledPrefs.putInt ((p + "cas").c_str(), customAnimSpeed[i]);
    ledPrefs.putInt ((p + "caa").c_str(), customAnimAmount[i]);
    ledPrefs.putBool((p + "car").c_str(), customAnimReverse[i]);
    ledPrefs.putInt ((p + "cmp").c_str(), customMorphPreset[i]);
    size_t pxBytes = (size_t)ch[i].count * 3U;
    if (pxBytes > sizeof(customRgb[i])) pxBytes = sizeof(customRgb[i]);
    ledPrefs.putBytes((p + "px").c_str(), customRgb[i], pxBytes);
  }
  ledPrefs.end();
}

// ── Debounced NVS Save ────────────────────────────────────────────────────────
static const unsigned long LED_SAVE_DEBOUNCE_MS = 1500;
static bool          ledSavePending = false;
static unsigned long ledSaveLastReq = 0;

static bool          ledDirty[LED_MAX_CHANNELS]     = { false };
static bool          ledForceShow[LED_MAX_CHANNELS] = { false }; // umgeht die 25ms-Drosselung (nur fuer event-getriebene Effekte wie Police)
static unsigned long ledLastShow = 0;
// Keepalive-Refresh: statische Frames (Aus / Feste Farbe / statisches Eigenmuster) werden
// nach dem einmaligen Zeichnen nicht mehr gesendet. Durch EMV/knappen Datenpegel
// verfaelschte Pixel bleiben dadurch stehen. Wir senden das aktuelle Frame darum alle
// ledKeepaliveMs ms erneut -> ein Stoerpixel wird spaetestens dann wieder ueberschrieben.
static unsigned long ledLastKeepalive = 0;

// Normales Markieren: wird von ledsShowDirty() auf LED_FRAME_MS gedrosselt gezeigt.
static void markDirty(int i)    { if (i >= 0 && i < LED_MAX_CHANNELS) ledDirty[i] = true; }
// Sofort-Markieren: wird beim naechsten ledsShowDirty() ungedrosselt gezeigt (fuer Sub-Frame-Timing, z.B. Blaulicht-Flanken).
static void markDirtyNow(int i) { if (i >= 0 && i < LED_MAX_CHANNELS) { ledDirty[i] = true; ledForceShow[i] = true; } }

static void ledsRequestSave() {
  ledSavePending = true;
  ledSaveLastReq = millis();
}

static void ledsFlushPendingSave() {
  if (ledSavePending && (millis() - ledSaveLastReq >= LED_SAVE_DEBOUNCE_MS)) {
    ledSavePending = false;
    ledsSaveConfig();
  }
}

// ── Strip initialisieren ─────────────────────────────────────────────────────
static void initStripFor(int i) {
  LedChannel &c = ch[i];
  if (c.strip) {
    c.strip->clear(); c.strip->show();
    delete c.strip; c.strip = nullptr;
  }
  if (c.pin < 0) return;   // unbelegter Kanal (leeres GPIO-Feld) -> kein Strip
  // Schutz vor RMT-Konflikt: nie zwei Kanaele auf denselben GPIO. Belegt ein
  // frueherer aktiver Kanal denselben Pin, erzeugen wir hier KEINEN Strip -
  // sonst klauen sich die NeoPixel-Objekte den RMT-Kanal ("not attached").
  for (int j = 0; j < i; j++) {
    if (ch[j].strip && ch[j].pin == c.pin) {
      ledLogPush("LEDs ch%d: GPIO %d already used by ch%d -> no strip", i, c.pin, j);
      return;
    }
  }
  uint16_t colOrder = LED_COLOR_ORDERS[(c.colorOrder >= 0 && c.colorOrder < LED_COLOR_ORDER_COUNT) ? c.colorOrder : 0];
  c.strip = new Adafruit_NeoPixel(c.count, c.pin, colOrder + NEO_KHZ800);
  c.strip->begin();
  c.strip->setBrightness(c.bright);
  c.strip->clear(); c.strip->show();
  c.krPos = 0; c.krDir = 1; c.krLastStep = 0;
  c.polOn = false; c.polForce = false; c.polSig = -1;
  c.rbHue = 0;
  ledDirty[i] = false; ledForceShow[i] = false;
  c.pinLow = false;   // frischer Strip -> Pin wieder an RMT gebunden
  ledLogPush("LEDs ch%d: GPIO %d UP (RMT attached, count=%d)", i, c.pin, c.count);
}

// ── Eigenes Muster: Renderer + Animationen ───────────────────────────────────
static inline int wrapIndex(int v, int n) {
  if (n <= 0) return 0;
  v %= n;
  if (v < 0) v += n;
  return v;
}

static inline uint32_t customHash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352dU;
  x ^= x >> 15; x *= 0x846ca68bU;
  x ^= x >> 16;
  return x;
}

static inline void setScaledCustomPixel(LedChannel &c, int pos, uint8_t r, uint8_t g, uint8_t b, float scale) {
  if (scale < 0.0f) scale = 0.0f;
  if (scale > 1.0f) scale = 1.0f;
  uint8_t rr = (uint8_t)(r * scale + 0.5f);
  uint8_t gg = (uint8_t)(g * scale + 0.5f);
  uint8_t bb = (uint8_t)(b * scale + 0.5f);
  c.strip->setPixelColor(pos, c.strip->gamma8(rr), c.strip->gamma8(gg), c.strip->gamma8(bb));
}

static void renderCustomChannel(int i, bool forceNow) {
  LedChannel &c = ch[i];
  if (!c.strip || c.count < 1) return;
  clampCustomAnim(i);

  const uint8_t mode = customAnim[i];
  uint32_t now = ledsFrameNow ? ledsFrameNow : millis();
  if (!forceNow && mode != CUSTOM_ANIM_STATIC && (now - c.krLastStep < LED_FRAME_MS)) return;
  c.krLastStep = now;
  c.strip->clear();

  // Auch die eigenen Animationen verwenden dieselbe 1..100-Tempo-Kurve.
  // Fuer laufende Muster wird aus der gemeinsamen Zykluszeit die Schrittzeit
  // passend zur aktuellen LED-Anzahl abgeleitet.
  uint32_t moveStepMs = tempoStepFromCycle(customAnimSpeed[i], c.count);
  int pingSteps = (c.count > 1) ? (2 * (c.count - 1)) : 1;
  uint32_t pingStepMs = tempoStepFromCycle(customAnimSpeed[i], pingSteps);

  if (mode == CUSTOM_ANIM_STATIC) {
    for (int p = 0; p < c.count; p++)
      setScaledCustomPixel(c, p, customRgb[i][p][0], customRgb[i][p][1], customRgb[i][p][2], 1.0f);
  }
  else if (mode == CUSTOM_ANIM_MOVE) {
    int shift = (int)(now / moveStepMs);
    if (customAnimReverse[i]) shift = -shift;
    for (int p = 0; p < c.count; p++) {
      int src = wrapIndex(p - shift, c.count);
      setScaledCustomPixel(c, p, customRgb[i][src][0], customRgb[i][src][1], customRgb[i][src][2], 1.0f);
    }
  }
  else if (mode == CUSTOM_ANIM_PINGPONG) {
    int span = (c.count > 1) ? (c.count - 1) : 1;
    int cycle = span * 2;
    int raw = cycle > 0 ? (int)((now / pingStepMs) % (uint32_t)cycle) : 0;
    int shift = (raw <= span) ? raw : (cycle - raw);
    if (customAnimReverse[i]) shift = -shift;
    for (int p = 0; p < c.count; p++) {
      int src = wrapIndex(p - shift, c.count);
      setScaledCustomPixel(c, p, customRgb[i][src][0], customRgb[i][src][1], customRgb[i][src][2], 1.0f);
    }
  }
  else if (mode == CUSTOM_ANIM_BRIGHT_WAVE) {
    // Farben/Positionen bleiben exakt erhalten; nur die Helligkeit wandert.
    uint32_t periodMs = tempoMainCycleMs(customAnimSpeed[i]);
    float phaseT = ((float)(now % periodMs) / (float)periodMs) * 6.28318530718f;
    if (customAnimReverse[i]) phaseT = -phaseT;
    // Wellentiefe 1..100 ist bewusst nicht linear 1..100 %.
    // 1 soll bereits sichtbar sein; 100 darf bis auf 0 Helligkeit absenken.
    // Daher: 1 -> 20 % Tiefe, 100 -> 100 % Tiefe.
    float depth = 0.20f + ((float)(customAnimAmount[i] - 1) / 99.0f) * 0.80f;
    for (int p = 0; p < c.count; p++) {
      float spatial = 6.28318530718f * ((float)p / (float)c.count);
      float wave = 0.5f + 0.5f * sinf(spatial - phaseT);
      float scale = (1.0f - depth) + depth * wave;
      setScaledCustomPixel(c, p, customRgb[i][p][0], customRgb[i][p][1], customRgb[i][p][2], scale);
    }
  }
  else if (mode == CUSTOM_ANIM_COLORWAVES) {
    // Das Live-Muster dient als zyklische Farbpalette. Zwischen benachbarten
    // Farben wird weich interpoliert, auch zu/von schwarzen "Aus"-Eintraegen.
    uint32_t periodMs = tempoMainCycleMs(customAnimSpeed[i]);
    float dir = customAnimReverse[i] ? -1.0f : 1.0f;
    float cyclePos = (float)(now % periodMs) / (float)periodMs;
    float travel = dir * cyclePos * c.count;
    float repeats = 0.75f + (customAnimAmount[i] / 100.0f) * 2.25f;
    for (int p = 0; p < c.count; p++) {
      float x = ((float)p / (float)c.count) * c.count * repeats - travel;
      int x0 = (int)floorf(x);
      float f = x - floorf(x);
      int a = wrapIndex(x0, c.count);
      int b = wrapIndex(x0 + 1, c.count);
      uint8_t r = (uint8_t)(customRgb[i][a][0] + (customRgb[i][b][0] - customRgb[i][a][0]) * f);
      uint8_t g = (uint8_t)(customRgb[i][a][1] + (customRgb[i][b][1] - customRgb[i][a][1]) * f);
      uint8_t bl= (uint8_t)(customRgb[i][a][2] + (customRgb[i][b][2] - customRgb[i][a][2]) * f);
      setScaledCustomPixel(c, p, r, g, bl, 1.0f);
    }
  }
  else if (mode == CUSTOM_ANIM_TWINKLE) {
    // Jeder Pixel bekommt pro Zeitfenster einen deterministischen Zufallsstart.
    // Dadurch braucht der Effekt keinen zusaetzlichen 300-Pixel-Zustand im RAM.
    uint32_t epochMs = tempoPeriodMs(customAnimSpeed[i], 8000U, 250U);
    uint32_t epoch = now / epochMs;
    float t = (float)(now % epochMs) / (float)epochMs;
    int density = customAnimAmount[i];
    for (int p = 0; p < c.count; p++) {
      uint32_t h = customHash32((uint32_t)p * 0x9e3779b9U ^ epoch * 0x85ebca6bU ^ (uint32_t)i * 0xc2b2ae35U);
      float scale = 0.0f;
      if ((int)(h % 100U) < density) {
        float start = ((h >> 8) & 1023U) / 1023.0f * 0.72f;
        float width = 0.18f + (((h >> 18) & 255U) / 255.0f) * 0.18f;
        float local = (t - start) / width;
        if (local >= 0.0f && local <= 1.0f) {
          float pulse = sinf(local * 3.14159265359f);
          scale = pulse * pulse * pulse * pulse;
        }
      }
      setScaledCustomPixel(c, p, customRgb[i][p][0], customRgb[i][p][1], customRgb[i][p][2], scale);
    }
  }
  else if (mode == CUSTOM_ANIM_MORPH) {
    // Das Live-Muster ist A, das ausgewaehlte gespeicherte Preset ist B.
    // A -> B -> A, weich geglaettet. Fehlende Zielpixel sind schwarz.
    if (customMorphPreset[i] < 0 || customMorphLen[i] <= 0) {
      for (int p = 0; p < c.count; p++)
        setScaledCustomPixel(c, p, customRgb[i][p][0], customRgb[i][p][1], customRgb[i][p][2], 1.0f);
    } else {
      uint32_t periodMs = tempoMainCycleMs(customAnimSpeed[i]);
      float x = (float)(now % periodMs) / (float)periodMs;
      float mix = (x < 0.5f) ? (x * 2.0f) : ((1.0f - x) * 2.0f);
      mix = mix * mix * (3.0f - 2.0f * mix); // smoothstep
      for (int p = 0; p < c.count; p++) {
        uint8_t ar = customRgb[i][p][0], ag = customRgb[i][p][1], ab = customRgb[i][p][2];
        uint8_t br = (p < customMorphLen[i]) ? customMorphRgb[i][p][0] : 0;
        uint8_t bg = (p < customMorphLen[i]) ? customMorphRgb[i][p][1] : 0;
        uint8_t bb = (p < customMorphLen[i]) ? customMorphRgb[i][p][2] : 0;
        uint8_t r = (uint8_t)(ar + (br - ar) * mix);
        uint8_t g = (uint8_t)(ag + (bg - ag) * mix);
        uint8_t b = (uint8_t)(ab + (bb - ab) * mix);
        setScaledCustomPixel(c, p, r, g, b, 1.0f);
      }
    }
  }

  if (forceNow) markDirtyNow(i); else markDirty(i);
}

// ── Effekt Reset & Anwenden ──────────────────────────────────────────────────
static void applyChannel(int i) {
  LedChannel &c = ch[i];
  if (!c.strip) return;   // unbelegt oder LOW (Strip freigegeben) -> nichts zeichnen; ledsLoop holt den Pin zurueck
  c.strip->setBrightness(c.bright);
  
  if (c.effect == 0) {
    c.strip->clear();
    markDirtyNow(i);
  } else if (c.effect == 1) {
    uint32_t col = c.strip->Color(c.strip->gamma8(c.r), c.strip->gamma8(c.g), c.strip->gamma8(c.b));
    for (int p = 0; p < c.count; p++) c.strip->setPixelColor(p, col);
    markDirtyNow(i);
  } else if (c.effect == 10) {
    c.krLastStep = 0;
    renderCustomChannel(i, true);
  } else if (c.effect == 2 || (c.effect >= 6 && c.effect <= 9)) {
    c.krPos = 0; c.krDir = 1; c.rbHue = 0; c.krLastStep = 0;
    c.strip->clear();
    markDirtyNow(i);
  } else if (c.effect >= 3 && c.effect <= 5) {
    c.polOn = false; c.polForce = true; c.polSig = -1;
    c.strip->clear();
    markDirtyNow(i);
  }
}

// ── 2: Knight Rider (High-End "KITT-Scanner") ──────────────────────────────────
static void knightRiderChannel(int i) {
  LedChannel &c = ch[i];
  if (!c.strip) return;
  uint32_t now = millis();
  int steps = (c.count > 1) ? (2 * (c.count - 1)) : 1;
  uint32_t interval = tempoStepFromCycle(c.krSpeed, steps);
  if (now - c.krLastStep < interval) return;
  c.krLastStep = now;

  c.strip->clear();
  int width = c.krWidth; 
  
  for (int p = 0; p < c.count; p++) {
    float dist = abs(p - c.krPos);
    if (dist < width) {
      float intensity = 1.0 - pow((float)dist / width, 3);
      uint8_t r = c.strip->gamma8(c.r * intensity);
      uint8_t g = c.strip->gamma8(c.g * intensity);
      uint8_t b = c.strip->gamma8(c.b * intensity);
      c.strip->setPixelColor(p, r, g, b);
    }
  }

  markDirty(i);

  c.krPos += c.krDir;
  if (c.krPos >= (c.count - 1)) { c.krPos = c.count - 1; c.krDir = -1; } 
  else if (c.krPos <= 0)        { c.krPos = 0;           c.krDir =  1; }
}

// ── 3,4,5: Police / Blaulicht ────────────────────────────────────────────────
static void policeChannel(int i) {
  LedChannel &c = ch[i];
  if (!c.strip) return;

  uint32_t now = ledsFrameNow;
  int hz = c.polHz;
  if (hz < 1) hz = 1; if (hz > 10) hz = 10;
  int flashes = c.krWidth; if (flashes < 1) flashes = 1;
  int phases = (c.effect == 4) ? 3 : 2;

  // Blaulicht-Sonderfall: der Reglerwert entspricht der echten Frequenz.
  // 1 = 1 Hz, 10 = 10 Hz. Gilt fuer EU und beide US-Varianten.
  uint32_t cycleTimeMs = 1000UL / (uint32_t)hz;
  uint32_t inCycle     = now % cycleTimeMs;                     // [0, cycleTimeMs)

  // Phasen EXAKT ueber den ganzen Zyklus kacheln -> kein Rest, currentPhase immer 0..phases-1.
  // (Vorher liess cycleTimeMs/phases einen 1ms-Rest, der am Zyklusende eine Phantom-Phase +
  //  Phantom-Blitz erzeugte. Zusammen mit "nur bei An/Aus neu zeichnen" blieb nach dem weissen
  //  Center das Weiss haengen, statt die farbige Phase 0 zu zeigen.)
  // currentPhase mit DERSELBEN Formel wie die Phasengrenzen bestimmen -> garantiert
  // konsistent, kein 1ms-Widerspruch/Blitz am Phasenuebergang. (phases <= 3, Schleife trivial.)
  int currentPhase = 0;
  while (currentPhase < phases - 1 &&
         inCycle >= ((uint32_t)(currentPhase + 1) * cycleTimeMs) / phases) {
    currentPhase++;
  }

  uint32_t phaseStart  = ((uint32_t) currentPhase      * cycleTimeMs) / phases;
  uint32_t phaseEnd    = ((uint32_t)(currentPhase + 1) * cycleTimeMs) / phases;
  uint32_t phaseLen    = phaseEnd - phaseStart;                 // Phasen sind ggf. 1ms unterschiedlich, kacheln aber luecken-/ueberlappungsfrei
  uint32_t timeInPhase = inCycle - phaseStart;

  uint32_t flashPeriod = phaseLen / flashes; if (flashPeriod < 1) flashPeriod = 1;
  uint32_t timeInFlash = timeInPhase % flashPeriod;

  uint32_t flashDuration = 15;
  if (flashDuration > flashPeriod / 2) flashDuration = flashPeriod / 2;
  bool lightOn = (timeInFlash < flashDuration);

  // Neu zeichnen bei jeder Aenderung des SICHTBAREN Zustands = (An/Aus UND Phase/Farbe),
  // nicht nur bei An/Aus. Sonst wird ein Phasenwechsel bei durchgehend anbleibendem Licht
  // verschluckt und die alte Farbe klebt fest.
  int sig = lightOn ? (currentPhase + 1) : 0;
  if (sig != c.polSig || c.polForce) {
    c.polForce = false;
    c.polSig = sig;
    c.polOn = lightOn;
    c.strip->clear();
    if (lightOn) {
      int half = c.count / 2;
      int third = c.count / 3;
      uint32_t cUser = c.strip->Color(c.strip->gamma8(c.r), c.strip->gamma8(c.g), c.strip->gamma8(c.b));
      uint32_t cRed  = c.strip->Color(255, 0, 0);
      uint32_t cBlue = c.strip->Color(0, 0, 255);
      uint32_t cWht  = c.strip->Color(255, 255, 255);
      uint32_t cLeft  = c.swapColors ? cBlue : cRed;
      uint32_t cRight = c.swapColors ? cRed  : cBlue;

      if (c.polRole == 1 || c.polRole == 2) {
        // Ganzer Streifen = eine physische Seite. Links leuchtet in Phase 0,
        // Rechts in Phase 1, Weiss (nur Effekt 4) in Phase 2 auf beiden Seiten.
        bool leftPhase  = (currentPhase == 0);
        bool rightPhase = (currentPhase == 1);
        bool whitePhase = (c.effect == 4 && currentPhase == 2);
        uint32_t col = 0; bool draw = false;
        if (c.polRole == 1 && leftPhase)  { col = (c.effect == 3) ? cUser : cLeft;  draw = true; }
        if (c.polRole == 2 && rightPhase) { col = (c.effect == 3) ? cUser : cRight; draw = true; }
        if (whitePhase)                   { col = cWht; draw = true; }
        if (draw) for (int p = 0; p < c.count; p++) c.strip->setPixelColor(p, col);
      }
      else if (c.effect == 3) {
        if (currentPhase == 0) for (int p = 0; p < half; p++) c.strip->setPixelColor(p, cUser);
        else                   for (int p = half; p < c.count; p++) c.strip->setPixelColor(p, cUser);
      }
      else if (c.effect == 4) {
        if (currentPhase == 0)      for (int p = 0; p < half; p++) c.strip->setPixelColor(p, cLeft);
        else if (currentPhase == 1) for (int p = half; p < c.count; p++) c.strip->setPixelColor(p, cRight);
        else                        for (int p = third; p < 2 * third; p++) c.strip->setPixelColor(p, cWht);
      }
      else if (c.effect == 5) {
        if (currentPhase == 0) for (int p = 0; p < half; p++) c.strip->setPixelColor(p, cLeft);
        else                   for (int p = half; p < c.count; p++) c.strip->setPixelColor(p, cRight);
      }
    }
    markDirtyNow(i);   // Blaulicht braucht sofortiges Zeigen auf jeder Flanke (Flash-Dauer < Frame-Zeit)
  }
}

// ── 6: Rainbow Wave ───────────────────────────────────────────────────────────
static void rainbowChannel(int i) {
  LedChannel &c = ch[i];
  if (!c.strip) return;
  uint32_t now = millis();
  if (now - c.krLastStep < LED_FRAME_MS) return;   // zeitbasiert -> Abtasten mit Framerate reicht (spart Rechenlast)
  uint32_t elapsed = now - c.krLastStep;
  c.krLastStep = now;

  // Hue-Vorschub aus verstrichener Zeit (nicht aus Frame-Anzahl!) in einen uint16_t-Akku:
  // laeuft bei 65535 automatisch sauber ueber -> kein Overflow, unabhaengig von Laufzeit/Framerate.
  uint32_t periodMs = tempoMainCycleMs(c.krSpeed);
  uint32_t adv = (uint32_t)(((uint64_t)elapsed * 65536ULL) / periodMs);
  if (adv > 65535U) adv = 65535U;
  c.rbHue += (uint16_t)adv;

  float density = c.krWidth / 10.0; 
  for (int p = 0; p < c.count; p++) {
     uint16_t pixelHue = c.rbHue + (uint16_t)((int32_t)p * 65536L / c.count * density);
     c.strip->setPixelColor(p, c.strip->gamma32(c.strip->ColorHSV(pixelHue)));
  }
  markDirty(i);
}

// ── 7: Breathing ──────────────────────────────────────────────────────────────
static void breathingChannel(int i) {
  LedChannel &c = ch[i];
  if (!c.strip) return;
  uint32_t now = millis();
  if (now - c.krLastStep < LED_FRAME_MS) return;   // war 15ms -> jetzt an die Framerate gekoppelt
  c.krLastStep = now;

  // Phase haengt an now (absolute Zeit) -> Tempo kommt aus krSpeed, nicht aus der Abtastrate.
  uint32_t periodMs = tempoMainCycleMs(c.krSpeed);
  float t = (float)(now % periodMs) / (float)periodMs;
  float phase = 0.5f - 0.5f * cosf(t * 6.28318530718f);
  phase = phase * phase * (3.0f - 2.0f * phase); 

  uint8_t r = c.strip->gamma8(c.r * phase);
  uint8_t g = c.strip->gamma8(c.g * phase);
  uint8_t b = c.strip->gamma8(c.b * phase);
  
  for (int p = 0; p < c.count; p++) c.strip->setPixelColor(p, r, g, b);
  markDirty(i);
}

// ── 8: Sparkle ────────────────────────────────────────────────────────────────
static void sparkleChannel(int i) {
  LedChannel &c = ch[i];
  if (!c.strip) return;
  uint32_t now = millis();
  uint32_t interval = tempoPeriodMs(c.krSpeed, 1500U, LED_FRAME_MS);
  if (now - c.krLastStep < interval) return;
  c.krLastStep = now;

  for (int p = 0; p < c.count; p++) fadePixel(c.strip, p, 40);
  
  if (random(100) < (c.krWidth * 4)) { 
      c.strip->setPixelColor(random(c.count), c.strip->gamma8(c.r), c.strip->gamma8(c.g), c.strip->gamma8(c.b));
  }
  markDirty(i);
}

// ── 9: Meteor Rain ────────────────────────────────────────────────────────────
static void meteorChannel(int i) {
  LedChannel &c = ch[i];
  if (!c.strip) return;
  uint32_t now = millis();
  int travelSteps = c.count + c.krWidth + 1;
  uint32_t interval = tempoStepFromCycle(c.krSpeed, travelSteps);
  if (now - c.krLastStep < interval) return;
  c.krLastStep = now;

  for (int p = 0; p < c.count; p++) fadePixel(c.strip, p, 45); 
  
  for (int w = 0; w < c.krWidth; w++) {
    int p = c.krPos - w;
    if (p >= 0 && p < c.count) {
      float intensity = 1.0 - ((float)w / c.krWidth); 
      intensity = intensity * intensity; 
      c.strip->setPixelColor(p, c.strip->gamma8(c.r * intensity), c.strip->gamma8(c.g * intensity), c.strip->gamma8(c.b * intensity));
    }
  }
  
  markDirty(i);

  c.krPos++;
  if (c.krPos > c.count + c.krWidth) c.krPos = 0; 
}

// ── Hauptschleife ─────────────────────────────────────────────────────────────
static void ledsShowDirty() {
  bool throttled = (millis() - ledLastShow < LED_FRAME_MS);
  bool didThrottledShow = false;
  for (int i = 0; i < channelCount; i++) {
    if (!ch[i].strip) continue;
    if (ledForceShow[i]) {
      // Sofort zeigen, Drosselung umgehen (Police-Flanken). Selbst-limitierend, da nur auf Zustandswechsel.
      ch[i].strip->show();
      ledForceShow[i] = false;
      ledDirty[i]     = false;
    } else if (ledDirty[i] && !throttled) {
      // Normale Effekte: hoechstens alle LED_FRAME_MS ein show().
      ch[i].strip->show();
      ledDirty[i] = false;
      didThrottledShow = true;
    }
  }
  // Nur der gedrosselte Pfad taktet die Drossel-Uhr; Sofort-Shows lassen sie unberuehrt,
  // damit ein Police-Kanal die anderen Kanaele nicht aus dem Frame-Budget draengt.
  if (didThrottledShow) ledLastShow = millis();
}

// Schaltet einen Kanal echt "aus": letztes Frame schwarz (noch ueber RMT), dann
// die Datenleitung als GPIO aktiv auf LOW. So kann der WS2815 im gestoerten/
// floatenden Zustand kein Geisterleuchten mehr zeigen. Der Pin ist danach NICHT
// mehr an RMT gebunden -> vor dem naechsten show() muss initStripFor() ihn zurueck-
// holen (passiert in ledsLoop / applyChannel).
static void ledsPullLow(int i) {
  LedChannel &c = ch[i];
  if (c.pin < 0 || c.pinLow) return;
  if (c.strip) { delete c.strip; c.strip = nullptr; }   // echten Strip freigeben (RMT frei)
  // Temporaerer Strip mit count+10 (gedeckelt auf CNT_MAX), um auch physisch ueberzaehlige
  // LEDs sicher zu loeschen. 3x schwarz senden (Redundanz gegen gestoerte Frames), jeweils
  // komplett rausschieben BEVOR wir den Pin uebernehmen.
  int n = c.count + 10; if (n > CNT_MAX) n = CNT_MAX; if (n < 1) n = 1;
  uint16_t ord = LED_COLOR_ORDERS[(c.colorOrder >= 0 && c.colorOrder < LED_COLOR_ORDER_COUNT) ? c.colorOrder : 0];
  Adafruit_NeoPixel *bs = new Adafruit_NeoPixel(n, c.pin, ord + NEO_KHZ800);
  bs->begin(); bs->clear();
  for (int r = 0; r < 3; r++) {
    bs->show();
    delay(2 + ((unsigned long)n * 30UL) / 1000UL);   // Frame KOMPLETT rausschieben
  }
  delete bs;                        // RMT nach vollstaendigem TX sauber freigeben
  pinMode(c.pin, OUTPUT);           // dann Datenleitung aktiv LOW
  digitalWrite(c.pin, LOW);
  c.pinLow = true;
  ledDirty[i] = false; ledForceShow[i] = false;
  ledLogPush("LEDs ch%d: GPIO %d DOWN (blanked, data LOW, RMT released)", i, c.pin);
}

void ledsLoop(int32_t erpm) {
  (void)erpm;
  ledsFrameNow = millis();

  for (int i = 0; i < channelCount; i++) {
    if (ch[i].effect == 0) { ledsPullLow(i); continue; }   // Aus -> Datenleitung aktiv LOW
    if (ch[i].pinLow) { initStripFor(i); applyChannel(i); }   // zurueck aus LOW: RMT zurueckholen + Basiszustand (nur hier, unter Task-Lock)
    if (ch[i].effect == 2)                        knightRiderChannel(i);
    else if (ch[i].effect >= 3 && ch[i].effect <= 5)   policeChannel(i);
    else if (ch[i].effect == 6)                   rainbowChannel(i);
    else if (ch[i].effect == 7)                   breathingChannel(i);
    else if (ch[i].effect == 8)                   sparkleChannel(i);
    else if (ch[i].effect == 9)                   meteorChannel(i);
    else if (ch[i].effect == 10 && customAnim[i] != CUSTOM_ANIM_STATIC) renderCustomChannel(i, false);
  }
  // Keepalive: statische Frames (Aus/Feste Farbe/statisches Eigenmuster) periodisch erneut
  // senden, damit durch Stoerungen verfaelschte Pixel geheilt werden. Animierte Effekte
  // senden ohnehin jeden Frame neu und brauchen das nicht.
  if (ledKeepaliveMs && ledsFrameNow - ledLastKeepalive >= ledKeepaliveMs) {
    ledLastKeepalive = ledsFrameNow;
    for (int i = 0; i < channelCount; i++) {
      if (!ch[i].strip || ch[i].pinLow) continue;
      bool staticFrame = (ch[i].effect == 1) ||
                         (ch[i].effect == 10 && customAnim[i] == CUSTOM_ANIM_STATIC);
      if (staticFrame) ledForceShow[i] = true;   // "Aus" ist ueber aktiv-LOW schon stoerungsimmun
    }
  }
  ledsShowDirty();          
  ledsFlushPendingSave();   
}

void ledsUpdateState(bool enabled, int32_t erpm) {
  ledsEnabled    = enabled;
  ledsLatestErpm = erpm;
  ledsFlushLog();   // laeuft im Hauptloop -> dlog() hier gefahrlos moeglich
}

static void ledsTaskFn(void *) {
  for (;;) {
    if (ledsEnabled) {
      ledsLock();
      ledsLoop(ledsLatestErpm);
      ledsUnlock();
    } else {
      // Auch bei deaktivierter LED-Steuerung muessen angeforderte NVS-Speicherungen
      // (Pixel-Edits, Slider) noch rausgeschrieben werden.
      ledsLock();
      ledsFlushPendingSave();
      ledsUnlock();
    }
    vTaskDelay(pdMS_TO_TICKS(1));   
  }
}

void ledsStartTask() {
  if (!ledsMutex) ledsMutex = xSemaphoreCreateMutex();
  if (ledsTaskHandle) return;   

  xTaskCreatePinnedToCore(ledsTaskFn, "ledsTask", 4096, nullptr, 1, &ledsTaskHandle, 1);
}

bool ledsAreOn() {
  // Ohne aktive Steuerung kann nichts leuchten — dann gar nicht erst sperren.
  if (!ledsEnabled) return false;
  bool on = false;
  ledsLock();
  for (int i = 0; i < LED_MAX_CHANNELS; i++) {
    // effect 0 heisst "Aus", pin < 0 heisst "Kanal nicht konfiguriert",
    // bright 0 heisst dunkel trotz laufendem Effekt.
    if (ch[i].pin >= 0 && ch[i].effect != 0 && ch[i].bright > 0) { on = true; break; }
  }
  ledsUnlock();
  return on;
}

void ledsOff() {
  // Belt-and-suspenders: den Task SOFORT stoppen, nicht erst beim naechsten
  // vescLoop()-Sync. So kann zwischen "aus" und dem Clear kein einzelner
  // Frame des alten Effekts mehr durchrutschen. vescLoop() korrigiert den
  // Flag ohnehin wieder auf cfg_leds_enabled, falls doch noch aktiv.
  ledsEnabled = false;
  if (ledSavePending) { ledSavePending = false; ledsSaveConfig(); }
  ledsLock();   
  for (int i = 0; i < LED_MAX_CHANNELS; i++) {
    ch[i].effect = 0;
    ledsPullLow(i);   // 3x schwarz (count+10) rausschieben + Datenleitung aktiv LOW
  }
  ledsUnlock();
}

// ── Ziel-Aufloesung (einzelner Kanal oder alle synchronisierten) ──────────────
static int resolveTargets(int *out) {
  int n = 0;
  if (ledServer->hasArg("sync") && ledServer->arg("sync") == "1") {
    for (int i = 0; i < channelCount; i++) if (ch[i].synced) out[n++] = i;
  } else if (ledServer->hasArg("ch")) {
    int i = ledServer->arg("ch").toInt();
    if (i >= 0 && i < channelCount) out[n++] = i;
  }
  return n;
}

static void okReply() { ledServer->send(200, "text/plain", "OK"); }

// True, wenn wirklich JEDE physische LED dieses Kanals 0/0/0 ist.
// In diesem Fall gibt es keinen Grund, den WS-Datenpin aktiv zu lassen:
// der Kanal wird auf Effekt AUS gesetzt, schwarz herausgeschoben und GPIO LOW gezogen.
static bool customChannelAllBlack(int i) {
  if (i < 0 || i >= LED_MAX_CHANNELS) return true;
  int n = ch[i].count;
  if (n < 0) n = 0;
  if (n > CNT_MAX) n = CNT_MAX;
  for (int p = 0; p < n; p++) {
    if (customRgb[i][p][0] != 0 || customRgb[i][p][1] != 0 || customRgb[i][p][2] != 0)
      return false;
  }
  return true;
}

// ── Eigenes LED-Muster: API ──────────────────────────────────────────────────
static void handlePatternsList() {
  String j = "{\"max\":" + String(LED_PATTERN_MAX) + ",\"patterns\":[";
  bool first = true;
  Preferences p; p.begin("ledpat", false);
  for (int i = 0; i < LED_PATTERN_MAX; i++) {
    if (!p.getBool(patKey(i, "u").c_str(), false)) continue;
    String name = p.getString(patKey(i, "n").c_str(), "Muster");
    int len = p.getInt(patKey(i, "l").c_str(), 1);
    if (len < 1) len = 1; if (len > CNT_MAX) len = CNT_MAX;
    if (!first) j += ","; first = false;
    j += "{\"id\":" + String(i) + ",\"name\":\"" + jsonEscape(name) + "\",\"count\":" + String(len) + "}";
  }
  p.end();
  j += "]}";
  ledServer->send(200, "application/json", j);
}

static void handlePixelsGet() {
  int t[LED_MAX_CHANNELS]; int n = resolveTargets(t);
  if (n < 1) { ledServer->send(400, "application/json", "{\"error\":\"target\"}"); return; }

  ledsLock();
  int maxCount = 0;
  int commonPreset = customPreset[t[0]];
  bool modified = customModified[t[0]];
  for (int k = 0; k < n; k++) {
    if (ch[t[k]].count > maxCount) maxCount = ch[t[k]].count;
    if (customPreset[t[k]] != commonPreset) commonPreset = -1;
    if (customModified[t[k]]) modified = true;
  }

  String j; j.reserve(64 + maxCount * 10);
  int ci = t[0];
  j = "{\"count\":" + String(maxCount) + ",\"preset\":" + String(commonPreset) + ",\"modified\":" + String(modified ? "true" : "false");
  j += ",\"anim\":" + String(customAnim[ci]);
  j += ",\"speed\":" + String(customAnimSpeed[ci]);
  j += ",\"amount\":" + String(customAnimAmount[ci]);
  j += ",\"reverse\":" + String(customAnimReverse[ci] ? "true" : "false");
  j += ",\"morph\":" + String(customMorphPreset[ci]);
  j += ",\"pixels\":[";
  for (int p = 0; p < maxCount; p++) {
    if (p) j += ",";
    uint8_t r = 0, g = 0, b = 0;
    for (int k = 0; k < n; k++) {
      int i = t[k];
      if (p < ch[i].count) { r = customRgb[i][p][0]; g = customRgb[i][p][1]; b = customRgb[i][p][2]; break; }
    }
    uint32_t packed = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
    j += String(packed);
  }
  j += "]}";
  ledsUnlock();
  ledServer->send(200, "application/json", j);
}

static void handlePixelSet() {
  int t[LED_MAX_CHANNELS]; int n = resolveTargets(t);
  int pos = ledServer->arg("p").toInt();
  int r = ledServer->arg("r").toInt();
  int g = ledServer->arg("g").toInt();
  int b = ledServer->arg("b").toInt();
  if (n < 1 || pos < 0 || pos >= CNT_MAX) { ledServer->send(400, "text/plain", "target/pixel"); return; }
  if (r < 0) r = 0; if (r > 255) r = 255;
  if (g < 0) g = 0; if (g > 255) g = 255;
  if (b < 0) b = 0; if (b > 255) b = 255;

  ledsLock();
  for (int k = 0; k < n; k++) {
    int i = t[k];
    if (pos >= ch[i].count) continue; // kuerzere Kanaele ignorieren diesen Pixel
    customRgb[i][pos][0] = (uint8_t)r;
    customRgb[i][pos][1] = (uint8_t)g;
    customRgb[i][pos][2] = (uint8_t)b;
    customModified[i] = true;
    if (customChannelAllBlack(i)) {
      // Letztes leuchtendes Pixel wurde auf 0/0/0 gesetzt -> wirklich AUS.
      ch[i].effect = 0;
      ledsPullLow(i); // schwarzes Frame, TX abwarten, danach Daten-GPIO LOW
    } else {
      ch[i].effect = 10;
      if (ch[i].pinLow || !ch[i].strip) initStripFor(i);
      if (ch[i].strip) {
        ch[i].strip->setBrightness(ch[i].bright);
        renderCustomChannel(i, true); // Live-Edit sofort in der laufenden Animation sichtbar
      }
    }
  }
  ledsUnlock();
  ledsRequestSave();
  okReply();
}

static void handlePixelsBatchSet() {
  int t[LED_MAX_CHANNELS]; int n = resolveTargets(t);
  String list = ledServer->arg("p");
  int r = ledServer->arg("r").toInt();
  int g = ledServer->arg("g").toInt();
  int b = ledServer->arg("b").toInt();
  if (n < 1 || list.length() == 0) { ledServer->send(400, "text/plain", "target/pixels"); return; }
  if (r < 0) r = 0; if (r > 255) r = 255;
  if (g < 0) g = 0; if (g > 255) g = 255;
  if (b < 0) b = 0; if (b > 255) b = 255;

  bool touched[LED_MAX_CHANNELS] = { false, false, false, false };
  bool any = false;

  ledsLock();
  // Kommagetrennte Positionsliste ohne String-Teilobjekte parsen. Beispiel: 0,2,4,7
  int pos = 0;
  bool haveDigits = false;
  bool validToken = true;
  int listLen = (int)list.length();
  for (int q = 0; q <= listLen; q++) {
    char c = (q < listLen) ? list[q] : ','; // kuenstliches Komma verarbeitet das letzte Token
    if (c >= '0' && c <= '9') {
      haveDigits = true;
      if (pos < 10000) pos = pos * 10 + (c - '0');
      else validToken = false;
      continue;
    }
    if (c != ',') {
      validToken = false;
      continue;
    }

    if (haveDigits && validToken && pos >= 0 && pos < CNT_MAX) {
      for (int k = 0; k < n; k++) {
        int i = t[k];
        if (pos >= ch[i].count) continue; // kuerzere Sync-Kanaele ignorieren diese Position
        customRgb[i][pos][0] = (uint8_t)r;
        customRgb[i][pos][1] = (uint8_t)g;
        customRgb[i][pos][2] = (uint8_t)b;
        touched[i] = true;
        any = true;
      }
    }
    pos = 0;
    haveDigits = false;
    validToken = true;
  }

  // Jeden betroffenen Kanal nur einmal neu rendern, egal wie viele LEDs gewaehlt sind.
  for (int k = 0; k < n; k++) {
    int i = t[k];
    if (!touched[i]) continue;
    customModified[i] = true;
    if (customChannelAllBlack(i)) {
      // Auch bei Mehrfachauswahl: sobald ALLE Pixel 0/0/0 sind -> GPIO LOW.
      ch[i].effect = 0;
      ledsPullLow(i);
    } else {
      ch[i].effect = 10;
      if (ch[i].pinLow || !ch[i].strip) initStripFor(i);
      if (ch[i].strip) {
        ch[i].strip->setBrightness(ch[i].bright);
        renderCustomChannel(i, true);
      }
    }
  }
  ledsUnlock();

  if (!any) { ledServer->send(400, "text/plain", "pixels"); return; }
  ledsRequestSave();
  okReply();
}

static void handleCustomFx() {
  int t[LED_MAX_CHANNELS]; int n = resolveTargets(t);
  if (n < 1) { ledServer->send(400, "text/plain", "target"); return; }

  int mode   = ledServer->hasArg("mode")   ? ledServer->arg("mode").toInt()   : -1;
  int speed  = ledServer->hasArg("speed")  ? ledServer->arg("speed").toInt()  : -1;
  int amount = ledServer->hasArg("amount") ? ledServer->arg("amount").toInt() : -1;
  int morph  = ledServer->hasArg("morph")  ? ledServer->arg("morph").toInt()  : -2;
  bool hasReverse = ledServer->hasArg("reverse");
  bool reverse = hasReverse && ledServer->arg("reverse") == "1";

  ledsLock();
  for (int k = 0; k < n; k++) {
    int i = t[k];
    if (mode >= 0)   customAnim[i]        = (uint8_t)mode;
    if (speed >= 0)  customAnimSpeed[i]   = (uint8_t)speed;
    if (amount >= 0) customAnimAmount[i]  = (uint8_t)amount;
    if (hasReverse)  customAnimReverse[i] = reverse;
    if (morph != -2) {
      customMorphPreset[i] = (int8_t)morph;
      clampCustomAnim(i);
      loadMorphCache(i);
    }
    clampCustomAnim(i);
    if (customChannelAllBlack(i)) {
      ch[i].effect = 0;
      ledsPullLow(i);
    } else {
      ch[i].effect = 10;
      if (ch[i].pinLow || !ch[i].strip) initStripFor(i);
      if (ch[i].strip) renderCustomChannel(i, true);
    }
  }
  ledsUnlock();
  ledsRequestSave();
  okReply();
}

static void handlePatternApply() {
  int t[LED_MAX_CHANNELS]; int n = resolveTargets(t);
  int slot = ledServer->arg("id").toInt();
  if (n < 1 || slot < 0 || slot >= LED_PATTERN_MAX) { ledServer->send(400, "text/plain", "target/id"); return; }

  static uint8_t data[CNT_MAX * 3];
  String name; int len = 0;
  if (!patternLoad(slot, name, len, data, sizeof(data))) { ledServer->send(404, "text/plain", "preset"); return; }

  ledsLock();
  for (int k = 0; k < n; k++) {
    int i = t[k];
    memset(customRgb[i], 0, sizeof(customRgb[i])); // zu lange Zielstreifen hinten AUS
    int copyN = len < ch[i].count ? len : ch[i].count; // zu kurze Zielstreifen: Rest des Presets faellt weg
    if (copyN > 0) memcpy(customRgb[i], data, (size_t)copyN * 3U);
    customPreset[i] = (int8_t)slot;
    customModified[i] = false;
    if (customChannelAllBlack(i)) {
      // Ein komplett schwarzes Preset ist elektrisch ebenfalls AUS.
      ch[i].effect = 0;
      ledsPullLow(i);
    } else {
      ch[i].effect = 10;
      if (ch[i].pinLow || !ch[i].strip) initStripFor(i);
      applyChannel(i);
    }
  }
  ledsUnlock();
  ledsRequestSave();
  ledServer->send(200, "application/json", "{\"id\":" + String(slot) + ",\"name\":\"" + jsonEscape(name) + "\",\"count\":" + String(len) + "}");
}

static void handlePatternSave() {
  int t[LED_MAX_CHANNELS]; int n = resolveTargets(t);
  if (n < 1) { ledServer->send(400, "text/plain", "target"); return; }

  int slot = ledServer->hasArg("id") ? ledServer->arg("id").toInt() : -1;
  if (slot < 0) slot = patternFindFree();
  if (slot < 0 || slot >= LED_PATTERN_MAX) { ledServer->send(507, "text/plain", "preset slots full"); return; }

  String name = ledServer->arg("name"); name.trim();
  if (name.length() == 0 && patternExists(slot)) {
    static uint8_t dummy[CNT_MAX * 3]; String oldName; int oldLen;
    patternLoad(slot, oldName, oldLen, dummy, sizeof(dummy));
    name = oldName;
  }
  if (name.length() == 0) name = "Farbe " + String(slot + 1);

  static uint8_t data[CNT_MAX * 3];
  memset(data, 0, sizeof(data));
  int len = 0;

  ledsLock();
  for (int k = 0; k < n; k++) if (ch[t[k]].count > len) len = ch[t[k]].count;
  if (len > CNT_MAX) len = CNT_MAX;
  // Bei Sync: fuer jede Position den ersten Kanal nehmen, der diese LED besitzt.
  // Dadurch bleiben LEDs 6..8 z.B. von einer 8er-Lightbar erhalten, auch wenn Seiten nur 5 LEDs haben.
  for (int pos = 0; pos < len; pos++) {
    for (int k = 0; k < n; k++) {
      int i = t[k];
      if (pos < ch[i].count) {
        data[pos * 3 + 0] = customRgb[i][pos][0];
        data[pos * 3 + 1] = customRgb[i][pos][1];
        data[pos * 3 + 2] = customRgb[i][pos][2];
        break;
      }
    }
  }
  ledsUnlock();

  if (!patternSave(slot, name, len, data)) { ledServer->send(500, "text/plain", "save"); return; }

  ledsLock();
  for (int k = 0; k < n; k++) { customPreset[t[k]] = (int8_t)slot; customModified[t[k]] = false; }
  // Falls dieses Preset gerade als Morph-Ziel benutzt wird, Cache sofort erneuern.
  for (int i = 0; i < LED_MAX_CHANNELS; i++) if (customMorphPreset[i] == slot) loadMorphCache(i);
  ledsUnlock();
  ledsRequestSave();
  ledServer->send(200, "application/json", "{\"id\":" + String(slot) + ",\"name\":\"" + jsonEscape(name) + "\",\"count\":" + String(len) + "}");
}

static void handlePatternDelete() {
  int slot = ledServer->arg("id").toInt();
  if (slot < 0 || slot >= LED_PATTERN_MAX || !patternExists(slot)) { ledServer->send(404, "text/plain", "preset"); return; }
  patternDelete(slot);
  ledsLock();
  for (int i = 0; i < LED_MAX_CHANNELS; i++) {
    if (customPreset[i] == slot) { customPreset[i] = -1; customModified[i] = true; }
    if (customMorphPreset[i] == slot) {
      customMorphPreset[i] = -1;
      customMorphLen[i] = 0;
      memset(customMorphRgb[i], 0, sizeof(customMorphRgb[i]));
    }
  }
  ledsUnlock();
  ledsRequestSave();
  okReply();
}

static void handlePatternAllOff() {
  // Der Endpunkt heisst bewusst "alloff": also wirklich AUS.
  // Erst das Custom-Muster auf Schwarz setzen, dann den Kanal komplett abschalten.
  // ledsPullLow() sendet ein schwarzes Frame und zieht anschliessend den Daten-GPIO LOW.
  int t[LED_MAX_CHANNELS]; int n = resolveTargets(t);
  if (n < 1) { ledServer->send(400, "text/plain", "target"); return; }
  ledsLock();
  for (int k = 0; k < n; k++) {
    int i = t[k];
    memset(customRgb[i], 0, sizeof(customRgb[i]));
    customModified[i] = true;
    ch[i].effect = 0;
    ledsPullLow(i);
  }
  ledsUnlock();
  ledsRequestSave();
  okReply();
}

// ── HTML Weboberflaeche ───────────────────────────────────────────────────────
// ── Seite ────────────────────────────────────────────────────────────────────
//
// Der Quelltext der Seite steht als eigene Datei unter web/leds.html und wird
// beim Bauen gepackt (tools/gzip_pages.py erzeugt leds_page_gz.h). Eingebunden
// wird nur das Ergebnis.
//
// Der ESP packt dabei NICHTS aus - er liefert die gepackten Bytes unveraendert
// mit dem Kopf "Content-Encoding: gzip", der Browser macht den Rest.
#include "leds_page_gz.h"

// ── Setup API-Endpoints ───────────────────────────────────────────────────────
// Blankt die Strips so frueh wie moeglich (in setup(), noch VOR WiFi/BLE), damit
// die WS2812 nach dem Power-On nicht bis zur spaeten ledsSetup()-Initialisierung
// zufaelligen Muell zeigen. initStripFor() macht clear()+show() -> Strip physisch
// dunkel. Registriert KEINE HTTP-Routes (der Webserver existiert hier noch nicht).
void ledsInitStripsEarly() {
  if (ledsStripsReady) return;                       // idempotent: nur einmal wirksam
  if (!ledsMutex) ledsMutex = xSemaphoreCreateMutex();
  ledsLoadConfig();
  for (int i = 0; i < LED_MAX_CHANNELS; i++) ch[i].effect = 0;
  // 1) Datenleitungen SOFORT definiert LOW (noch vor jedem Strip), damit zwischen
  //    Power-On und Init kein floatender Pin Stoerungen als Daten latcht.
  for (int i = 0; i < channelCount; i++)
    if (ch[i].pin >= 0) { pinMode(ch[i].pin, OUTPUT); digitalWrite(ch[i].pin, LOW); }
  // 2) Boot-Zustand = aus: 3x schwarz (count+10) senden + Pin wieder aktiv LOW.
  //    ledsPullLow legt dafuer selbst einen temporaeren Strip an.
  for (int i = 0; i < channelCount; i++) ledsPullLow(i);
  ledsStripsReady = true;
}

void ledsSetup(WebServer *server) {
  ledServer = server;
  if (!ledServer) return;

  // Strips initialisieren + blanken (idempotent; i.d.R. schon frueh in setup() erledigt).
  ledsInitStripsEarly();

  ledServer->on("/leds", HTTP_GET, [](){
    // Laenge MUSS mitgegeben werden: die Fassung ohne sie bestimmt sie per
    // strlen(), und gzip-Daten enthalten Nullbytes. Die Seite kaeme sonst als
    // Fragment an. Der Kopf muss VOR dem Senden gesetzt sein.
    ledServer->sendHeader("Content-Encoding", "gzip");
    ledServer->send_P(200, "text/html", (PGM_P)LEDS_PAGE_GZ, LEDS_PAGE_GZ_LEN);
  });

  ledServer->on("/api/led/config", HTTP_GET, [](){
    String j = "{\"count\":" + String(channelCount) + ",\"keepalive\":" + String(ledKeepaliveMs) + ",\"channels\":[";
    for (int i = 0; i < LED_MAX_CHANNELS; i++) {
      if (i) j += ",";
      j += "{\"pin\":"       + String(ch[i].pin);
      j += ",\"count\":"     + String(ch[i].count);
      j += ",\"colororder\":"+ String(ch[i].colorOrder);
      j += ",\"polrole\":"   + String(ch[i].polRole);
      j += ",\"synced\":"    + String(ch[i].synced ? "true" : "false");
      j += ",\"effect\":"    + String(ch[i].effect);
      j += ",\"r\":"         + String(ch[i].r);
      j += ",\"g\":"         + String(ch[i].g);
      j += ",\"b\":"         + String(ch[i].b);
      j += ",\"bright\":"    + String(ch[i].bright);
      j += ",\"krspeed\":"   + String(ch[i].krSpeed);
      j += ",\"krwidth\":"   + String(ch[i].krWidth);
      j += ",\"polhz\":"     + String(ch[i].polHz);
      j += ",\"swapcolors\":"+ String(ch[i].swapColors ? "true" : "false");
      j += ",\"customanim\":"    + String(customAnim[i]);
      j += ",\"customspeed\":"   + String(customAnimSpeed[i]);
      j += ",\"customamount\":"  + String(customAnimAmount[i]);
      j += ",\"customreverse\":" + String(customAnimReverse[i] ? "true" : "false");
      j += ",\"custommorph\":"   + String(customMorphPreset[i]);
      j += "}";
    }
    j += "]}";
    ledServer->send(200, "application/json", j);
  });

  ledServer->on("/api/led/channels", HTTP_POST, [](){
    if (ledServer->hasArg("n")) {
      int n = ledServer->arg("n").toInt();
      if (n < 1) n = 1; if (n > LED_MAX_CHANNELS) n = LED_MAX_CHANNELS;
      ledsLock();   
      if (n < channelCount) {
        for (int i = n; i < channelCount; i++) {
          if (ch[i].strip) { ch[i].strip->clear(); ch[i].strip->show();
                             delete ch[i].strip; ch[i].strip = nullptr; }
        }
      }
      int old = channelCount;
      channelCount = n;
      if (n > old) for (int i = old; i < n; i++) {
        ch[i].pin = -1;   // neuer Kanal startet ohne GPIO (leeres Feld) - bewusst vergeben
        initStripFor(i); applyChannel(i);
      }
      ledsSaveConfig();
      ledsUnlock();
    }
    ledServer->send(200, "text/plain", "OK");
  });

  ledServer->on("/api/led/sync", HTTP_POST, [](){
    if (ledServer->hasArg("ch")) {
      int i = ledServer->arg("ch").toInt();
      if (i >= 0 && i < channelCount) {
        ch[i].synced = (ledServer->arg("on") == "1");
        ledsSaveConfig();
      }
    }
    ledServer->send(200, "text/plain", "OK");
  });

  ledServer->on("/api/led/polrole", HTTP_POST, [](){
    if (ledServer->hasArg("ch") && ledServer->hasArg("role")) {
      int i = ledServer->arg("ch").toInt();
      if (i >= 0 && i < channelCount) {
        int r = ledServer->arg("role").toInt();
        if (r < 0 || r > 2) r = 0;
        ch[i].polRole  = r;
        ch[i].polForce = true;   // Police sofort mit neuer Rolle neu zeichnen
        ledsSaveConfig();
      }
    }
    ledServer->send(200, "text/plain", "OK");
  });

  // Not-Aus: alle Kanaele auf Effekt 0 + Strips blanken (grosser AUS-Button).
  ledServer->on("/api/led/alloff", HTTP_POST, [](){
    ledsLock();
    for (int i = 0; i < channelCount; i++) {
      ch[i].effect = 0;
      if (ch[i].strip) { ch[i].strip->clear(); ch[i].strip->show(); }
      ledDirty[i] = false; ledForceShow[i] = false;
    }
    ledsUnlock();
    ledsSaveConfig();
    ledServer->send(200, "text/plain", "OK");
  });

  // Alles AN = feste Farbe (Effekt 1). Gegenstueck zum AUS-Toggle.
  ledServer->on("/api/led/allon", HTTP_POST, [](){
    ledsLock();
    for (int i = 0; i < channelCount; i++) {
      ch[i].effect = 1;    // feste Farbe
      clampChannel(i);
      applyChannel(i);     // zeichnet die Farbe (No-Op wenn Pin LOW -> ledsLoop holt zurueck + zeichnet)
    }
    ledsUnlock();
    ledsSaveConfig();
    ledServer->send(200, "text/plain", "OK");
  });

  ledServer->on("/api/led/color", HTTP_POST, [](){
    int tg[LED_MAX_CHANNELS]; int n = resolveTargets(tg);
    ledsLock();
    for (int k = 0; k < n; k++) {
      int i = tg[k];
      if (ledServer->hasArg("r")) ch[i].r = ledServer->arg("r").toInt();
      if (ledServer->hasArg("g")) ch[i].g = ledServer->arg("g").toInt();
      if (ledServer->hasArg("b")) ch[i].b = ledServer->arg("b").toInt();
      clampChannel(i);
      if (ch[i].effect == 1) applyChannel(i); 
      else if (ch[i].effect >= 3 && ch[i].effect <= 5) ch[i].polForce = true;
    }
    ledsUnlock();
    ledsRequestSave();   
    ledServer->send(200, "text/plain", "OK");
  });

  ledServer->on("/api/led/bright", HTTP_POST, [](){
    int tg[LED_MAX_CHANNELS]; int n = resolveTargets(tg);
    ledsLock();
    for (int k = 0; k < n; k++) {
      int i = tg[k];
      if (ledServer->hasArg("v")) ch[i].bright = ledServer->arg("v").toInt();
      clampChannel(i);
      if (ch[i].strip) ch[i].strip->setBrightness(ch[i].bright);
      if (ch[i].effect == 1 || ch[i].effect == 10) applyChannel(i);
      else if (ch[i].effect >= 3 && ch[i].effect <= 5) ch[i].polForce = true;
    }
    ledsUnlock();
    ledsRequestSave();   
    ledServer->send(200, "text/plain", "OK");
  });

  ledServer->on("/api/led/krspeed", HTTP_POST, [](){
    int tg[LED_MAX_CHANNELS]; int n = resolveTargets(tg);
    for (int k = 0; k < n; k++) {
      int i = tg[k];
      if (ledServer->hasArg("v")) ch[i].krSpeed = ledServer->arg("v").toInt();
      clampChannel(i);
    }
    ledsRequestSave();   
    ledServer->send(200, "text/plain", "OK");
  });

  ledServer->on("/api/led/krwidth", HTTP_POST, [](){
    int tg[LED_MAX_CHANNELS]; int n = resolveTargets(tg);
    for (int k = 0; k < n; k++) {
      int i = tg[k];
      if (ledServer->hasArg("v")) ch[i].krWidth = ledServer->arg("v").toInt();
      clampChannel(i);
      if (ch[i].effect >= 3 && ch[i].effect <= 5) ch[i].polForce = true;
    }
    ledsRequestSave();   
    ledServer->send(200, "text/plain", "OK");
  });

  ledServer->on("/api/led/polhz", HTTP_POST, [](){
    int tg[LED_MAX_CHANNELS]; int n = resolveTargets(tg);
    for (int k = 0; k < n; k++) {
      int i = tg[k];
      if (ledServer->hasArg("v")) ch[i].polHz = ledServer->arg("v").toInt();
      clampChannel(i);
      if (ch[i].effect >= 3 && ch[i].effect <= 5) ch[i].polForce = true;
    }
    ledsRequestSave();   
    ledServer->send(200, "text/plain", "OK");
  });

  ledServer->on("/api/led/swapcol", HTTP_POST, [](){
    int tg[LED_MAX_CHANNELS]; int n = resolveTargets(tg);
    for (int k = 0; k < n; k++) {
      int i = tg[k];
      if (ledServer->hasArg("v")) ch[i].swapColors = (ledServer->arg("v") == "1");
      if (ch[i].effect >= 3 && ch[i].effect <= 5) ch[i].polForce = true;
    }
    ledsRequestSave();   
    ledServer->send(200, "text/plain", "OK");
  });

  ledServer->on("/api/led/effect", HTTP_POST, [](){
    int tg[LED_MAX_CHANNELS]; int n = resolveTargets(tg);
    ledsLock();
    for (int k = 0; k < n; k++) {
      int i = tg[k];
      if (ledServer->hasArg("e")) ch[i].effect = ledServer->arg("e").toInt();
      clampChannel(i);
      if (ch[i].effect == 0) {
        ledsPullLow(i);                                   // Aus -> schwarz + Daten-GPIO LOW
      } else if (ch[i].effect == 10 && customChannelAllBlack(i)) {
        // Auch beim Auswaehlen von "Eigenes LED-Muster": ist wirklich jedes
        // Pixel 0/0/0, bleibt der physische Ausgang AUS und der GPIO LOW.
        ch[i].effect = 0;
        ledsPullLow(i);
      } else {
        if (ch[i].pinLow || !ch[i].strip) initStripFor(i);
        applyChannel(i);
      }
    }
    ledsUnlock();
    ledsSaveConfig();
    ledServer->send(200, "text/plain", "OK");
  });

  ledServer->on("/api/led/hw", HTTP_POST, [](){
    for (int i = 0; i < channelCount; i++) {
      String pk = "p" + String(i), nk = "n" + String(i), ok = "o" + String(i);
      if (ledServer->hasArg(pk.c_str())) ch[i].pin        = ledServer->arg(pk.c_str()).toInt();
      if (ledServer->hasArg(nk.c_str())) ch[i].count      = ledServer->arg(nk.c_str()).toInt();
      if (ledServer->hasArg(ok.c_str())) ch[i].colorOrder = ledServer->arg(ok.c_str()).toInt();
    }
    if (ledServer->hasArg("ka")) {
      long v = ledServer->arg("ka").toInt();
      if (v < 0) v = 0; if (v > 5000) v = 5000;
      ledKeepaliveMs = (unsigned long) v;
    }
    clampAll();
    ledsSaveConfig();
    ledsLock();   
    for (int i = 0; i < channelCount; i++) { initStripFor(i); applyChannel(i); }
    ledsUnlock();
    ledServer->send(200, "text/plain", "OK");
  });

  // ── Eigenes LED-Muster ──────────────────────────────────────────────────────
  ledServer->on("/api/led/patterns",       HTTP_GET,  handlePatternsList);
  ledServer->on("/api/led/pixels",         HTTP_GET,  handlePixelsGet);
  ledServer->on("/api/led/pixel",          HTTP_POST, handlePixelSet);
  ledServer->on("/api/led/pixels/batch",   HTTP_POST, handlePixelsBatchSet);
  ledServer->on("/api/led/customfx",       HTTP_POST, handleCustomFx);
  ledServer->on("/api/led/pattern/apply",  HTTP_POST, handlePatternApply);
  ledServer->on("/api/led/pattern/save",   HTTP_POST, handlePatternSave);
  ledServer->on("/api/led/pattern/delete", HTTP_POST, handlePatternDelete);
  ledServer->on("/api/led/pattern/alloff", HTTP_POST, handlePatternAllOff);

  dlog("LEDs: multi-channel /leds page + API registered\n");
}