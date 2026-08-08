#include "leds.h"
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
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
static unsigned long ledKeepaliveMs = 150;   // 0 = aus; Keepalive-Intervall (in HW-Config einstellbar)

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
  ledKeepaliveMs = (unsigned long) ledPrefs.getInt("kams", 150);
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
      Serial.printf("LEDs ch%d: GPIO %d already used by ch%d -> no strip\n", i, c.pin, j);
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
  Serial.printf("LEDs ch%d: init pin=%d count=%d\n", i, c.pin, c.count);
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
static const char LEDS_PAGE_HTML[] PROGMEM = R"ledslit(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>LEDs</title>
  <link rel="stylesheet" href="/style.css">
  <style>
    input[type=range]{width:100%;accent-color:var(--accent);margin-top:6px}
    .rng-row{display:grid;grid-template-columns:24px 1fr 44px;gap:8px;align-items:center;margin-top:8px}
    .rng-row span{font-size:12px;color:var(--text2)}
    .rng-val{font-size:12px;color:var(--accent);text-align:right}
    select{width:100%;padding:8px 10px;background:var(--bg3);border:1px solid var(--border);border-radius:4px;color:var(--text);font-family:inherit;font-size:13px}
    .chrow{background:var(--bg3);border:1px solid var(--border2);border-radius:6px;padding:12px;margin-bottom:8px}
    .cnt-ctrl{display:grid;grid-template-columns:1fr auto auto;gap:8px;align-items:center;margin-bottom:10px}
    .custombox{margin-top:10px;padding:10px;background:var(--bg3);border:1px solid var(--border);border-radius:6px}
    .customrow{display:grid;grid-template-columns:1fr auto;gap:8px;align-items:end;margin-top:8px}
    .customrow.three{grid-template-columns:1fr auto auto}
    .pixel-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(118px,1fr));gap:10px;margin-top:12px}
    .pixel{padding:10px;background:var(--bg2);border:1px solid var(--border);border-radius:8px;text-align:center;transition:border-color .12s,box-shadow .12s,background .12s}
    .pixel.selected{border-color:var(--accent);box-shadow:0 0 0 2px var(--accent) inset;background:rgba(77,163,255,.10)}
    .pixel-select{width:100%;min-height:44px;padding:9px 8px;margin:0 0 9px;background:var(--bg3);border:1px solid var(--border2);border-radius:6px;color:var(--text);font-family:inherit;font-size:13px;font-weight:650;cursor:pointer;user-select:none;touch-action:manipulation}
    .pixel-select:hover{border-color:var(--accent)}
    .pixel.selected .pixel-select{border-color:var(--accent);color:var(--accent);background:rgba(77,163,255,.18)}
    .pixel.selected .pixel-select:before{content:'\2713  ';}
    .pixel input[type=color]{width:100%;height:46px;border:1px solid var(--border2);border-radius:6px;background:var(--bg3);padding:3px;cursor:pointer}
    .pixel .off{width:100%;padding:7px 6px;margin-top:7px;background:var(--bg3);border:1px solid var(--border2);border-radius:5px;color:var(--text2);font-family:inherit;font-size:12px;cursor:pointer}
    .selbox{margin-top:10px;padding:9px;background:var(--bg2);border:1px solid var(--border);border-radius:6px}
    .selhead{display:flex;justify-content:space-between;gap:8px;align-items:center;font-size:11px;color:var(--text2);margin-bottom:7px}
    .selhead b{color:var(--accent);font-size:12px}
    .selbuttons{display:flex;gap:5px;flex-wrap:wrap}
    .selcolor{display:grid;grid-template-columns:1fr auto;gap:7px;align-items:end;margin-top:8px}
    .selcolor input[type=color]{width:100%;height:38px;border:1px solid var(--border2);border-radius:5px;background:var(--bg3);padding:2px;cursor:pointer}
    .selcolor input[type=color]:disabled,.selcolor .btn:disabled{opacity:.4;cursor:default}
    .patmsg{min-height:16px;margin-top:8px;font-size:11px;color:var(--accent)}
    .modified{color:#ffbf69;font-weight:600}
    .info-note{display:none;margin-top:7px;padding:7px 9px;border-left:2px solid var(--accent);border-radius:4px;background:rgba(77,163,255,.07);color:var(--text2);font-size:11px;line-height:1.4}
    body.show-info .info-note:not([data-relevant="0"]){display:block}
    .hwbtns{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
  </style>
</head>
<body>
<button class="theme-btn" onclick="toggleTheme()" id="themeBtn">&#9728;&#65039;</button>
<button class="lang-btn" onclick="toggleLang()" id="langBtn">DE</button>
<div class="wrap">
  <h1>&#x1F6F4; VESC BLE/WiFi</h1>
  <div class="sub" id="statusBar">Loading...</div>
  <div class="tabs">
    <div class="tab" onclick="location.href='/?tab=info'">Info</div>
    <div class="tab" onclick="location.href='/?tab=config'">Config</div>
    <div class="tab" onclick="location.href='/?tab=ota'">OTA Flash</div>
    <div class="tab" id="tab-api-link" style="display:none" onclick="location.href='/?tab=api'">API</div>
    <div class="tab active" onclick="location.href='/leds'">LED</div>
  </div>

  <div class="section">
    <h3 id="lbl-hw">Channels</h3>
    <div id="hwrows"></div>
    <div class="hwbtns">
      <button class="btn" onclick="applyHw()" id="btn-hw">Apply hardware</button>
      <button class="btn sm" onclick="toggleInfo()" id="btn-info">i</button>
    </div>
    <div class="msg" id="hwmsg"></div>
    <button id="bigOff" onclick="ledsBigToggle()" style="display:none;width:100%;padding:22px 12px;margin:14px 0 2px;font-size:30px;font-weight:800;letter-spacing:6px;color:#fff;background:linear-gradient(#d32f2f,#8e0000);border:2px solid #ff5a5a;border-radius:12px;box-shadow:0 4px 16px rgba(0,0,0,.3);cursor:pointer">AUS</button>
  </div>

  <div id="controls"></div>
</div>

<script>
var lang=(document.cookie.match(/lang=([a-z]+)/)||[])[1]||(navigator.language.startsWith('de')?'de':'en');
function de(){return lang==='de';}
function L(d,e){return de()?d:e;}
function gid(id){return document.getElementById(id);}

var theme=(document.cookie.match(/theme=([a-z]+)/)||[])[1]||(window.matchMedia('(prefers-color-scheme:dark)').matches?'dark':'light');
function applyTheme(){document.documentElement.setAttribute('data-theme',theme);var b=gid('themeBtn');if(b)b.textContent=theme==='dark'?'\u2600\uFE0F':'\uD83C\uDF19';document.cookie='theme='+theme+';path=/;max-age=31536000';}
function toggleTheme(){theme=theme==='dark'?'light':'dark';applyTheme();}
applyTheme();
function toggleLang(){lang=lang==='de'?'en':'de';document.cookie='lang='+lang+';path=/;max-age=31536000';location.reload();}
gid('langBtn').textContent=de()?'EN':'DE';

var infoVisible=false;
try{infoVisible=localStorage.getItem('ledInfoVisible')==='1';}catch(e){}
function applyInfoState(){
  if(document.body) document.body.classList.toggle('show-info',infoVisible);
  var b=gid('btn-info');
  if(b){b.textContent=infoVisible?'i \u2713':'i';b.title=infoVisible?L('Hinweise ausblenden','Hide notes'):L('Hinweise einblenden','Show notes');}
}
function toggleInfo(){
  infoVisible=!infoVisible;
  try{localStorage.setItem('ledInfoVisible',infoVisible?'1':'0');}catch(e){}
  applyInfoState();
}

(function(){
  var s=function(id,en,d){var el=gid(id);if(el)el.textContent=de()?d:en;};
  s('lbl-hw','Channels','Kanäle');
  s('btn-hw','Apply hardware','Hardware übernehmen');
  var sb=gid('statusBar'); if(sb && sb.textContent==='Loading...') sb.textContent=de()?'Lädt...':'Loading...';
})();

var vescRx=-1, vescTx=-1;
function loadStatus(){
  fetch('/api/info').then(function(r){return r.json();}).then(function(d){
    gid('statusBar').textContent=d.mode==='ap'&&!d.ssid?'AP: '+d.ip:'WiFi: '+d.ssid+' ('+d.ip+')';
    if(d.rx_pin!==undefined) vescRx=parseInt(d.rx_pin);
    if(d.tx_pin!==undefined) vescTx=parseInt(d.tx_pin);
    var bo=gid('bigOff'); if(bo){ bo.style.display=(d.ble_name==='Headcrash366')?'':'none'; updateBigOff(); }
  }).catch(function(){});
}
loadStatus();
setInterval(loadStatus,5000);
function gv(id){var e=gid(id);return e?e.value:0;}
function setText(id,v){var e=gid(id);if(e)e.textContent=v;}
function toHex(n){var h=(+n).toString(16);return h.length<2?'0'+h:h;}
function rgbToHex(r,g,b){return '#'+toHex(r)+toHex(g)+toHex(b);}
function pfx(idx){return idx<0?'sync':'ch'+idx;}
function tgt(idx){return idx<0?'sync=1':'ch='+idx;}
function targetQuery(idx){return tgt(idx);}
function packedHex(v){var h=(+v>>>0).toString(16);while(h.length<6)h='0'+h;return '#'+h.slice(-6);}

var cfg={count:1,channels:[]};
var patterns=[];
var t=null, pixelTimers={}, pixelGroupTimers={};
var pixelSelections={}, pixelLastSel={}, pixelCounts={};
function send(url){return fetch(url,{method:'POST'});}
function fire(url){send(url).catch(function(){});}
function debSend(url){clearTimeout(t);t=setTimeout(function(){fire(url);},60);}

function loadPatterns(done){
  fetch('/api/led/patterns').then(function(r){return r.json();}).then(function(d){
    patterns=d.patterns||[]; if(done)done();
  }).catch(function(){ patterns=[]; if(done)done(); });
}

function load(){
  loadPatterns(function(){
    fetch('/api/led/config').then(function(r){return r.json();}).then(function(d){
      cfg=d; render();
    }).catch(function(){});
  });
}

function render(){ renderHw(); renderControls(); updateBigOff(); }

function coOpts(sel){
  var names=['GRB','RGB','BRG','RBG','GBR','BGR'], o='';
  for(var k=0;k<names.length;k++) o+='<option value="'+k+'"'+((k===sel)?' selected':'')+'>'+names[k]+'</option>';
  return o;
}
function renderHw(){
  var h='';
  h+='<div class="cnt-ctrl">';
  h+='<span style="font-size:13px;color:var(--text2)">'+L('Aktive Kanäle','Active channels')+': '+cfg.count+'</span>';
  h+='<button class="btn red sm" onclick="chCountDelta(-1)" '+(cfg.count<=1?'disabled':'')+'>&#8722;</button>';
  h+='<button class="btn green sm" onclick="chCountDelta(1)" '+(cfg.count>=4?'disabled':'')+'>+</button>';
  h+='</div>';
  h+='<div class="chrow"><label>'+L('LED-Refresh (Keepalive)','LED refresh (keepalive)')+'</label><div style="display:flex;gap:8px;align-items:center;margin-top:4px"><input type="text" id="hwka" maxlength="4" value="'+(cfg.keepalive!==undefined?cfg.keepalive:150)+'" style="flex:1"><span style="color:var(--text2);font-size:12px">ms (0='+L('aus','off')+')</span></div><div style="color:var(--text2);font-size:11px;margin-top:4px">'+L('Sendet statische Frames (Aus/Feste Farbe) periodisch neu.','Periodically re-sends static frames (off/solid).')+'</div></div>';
  for(var i=0;i<cfg.count;i++){
    var c=cfg.channels[i];
    h+='<div class="chrow">';
    h+='<div style="font-size:12px;color:var(--text2);margin-bottom:6px">'+L('Kanal','Channel')+' '+(i+1)+'</div>';
    h+='<div style="display:grid;grid-template-columns:1fr 1fr;gap:8px">';
    h+='<div><label>GPIO</label><input type="text" id="hwpin'+i+'" maxlength="2" placeholder="'+L('Pin?','Pin?')+'" value="'+(c.pin>=0?c.pin:'')+'"></div>';
    h+='<div><label>'+L('Anzahl','Count')+'</label><input type="text" id="hwcnt'+i+'" maxlength="3" value="'+c.count+'"></div>';
    h+='</div>';
    h+='<div style="margin-top:8px"><label>'+L('Farb-Reihenfolge','Color order')+'</label><select id="hwco'+i+'">'+coOpts(c.colororder)+'</select></div>';
    h+='<label class="checkbox-row" style="margin-top:8px"><input type="checkbox" '+(c.synced?'checked':'')+' onchange="toggleSync('+i+',this.checked)">'+L('Synchronisiert','Synced')+'</label>';
    h+='<div style="margin-top:8px;display:flex;gap:6px;align-items:center;flex-wrap:wrap"><label style="margin:0">'+L('Police-Seite','Police side')+'</label>'+
       '<button class="btn sm'+(c.polrole==0?' green':'')+'" onclick="onPolRole('+i+',0)">'+L('Teilen','Split')+'</button>'+
       '<button class="btn sm'+(c.polrole==1?' green':'')+'" onclick="onPolRole('+i+',1)">L</button>'+
       '<button class="btn sm'+(c.polrole==2?' green':'')+'" onclick="onPolRole('+i+',2)">R</button></div>';
    h+='</div>';
  }
  gid('hwrows').innerHTML=h;
}

function updateVis(idx) {
  var p = pfx(idx);
  var el = gid(p+'_eff');
  if(!el) return;
  var e = +el.value;
  var wc = gid(p+'_wrap_col'), wb = gid(p+'_wrap_bri');
  var wkr = gid(p+'_wrap_kr'), wpol = gid(p+'_wrap_pol');
  var wswp = gid(p+'_wrap_swp'), wcustom = gid(p+'_wrap_custom');
  
  if(wc) wc.style.display = (e==1 || e==2 || e==3 || e==7 || e==8 || e==9) ? 'block' : 'none';
  if(wb) wb.style.display = (e>=1) ? 'block' : 'none';
  if(wkr) wkr.style.display = (e==2 || e==6 || e==7 || e==8 || e==9) ? 'block' : 'none';
  if(wpol) wpol.style.display = (e>=3 && e<=5) ? 'block' : 'none';
  
  if(wswp) wswp.style.display = (e==4 || e==5) ? 'block' : 'none';
  if(wcustom) wcustom.style.display = (e==10) ? 'block' : 'none';
  if(e==10){ loadPixels(idx); updateCustomFxVis(idx); }
  
  var lblSpd = gid(p+'_lbl_spd'), lblWid = gid(p+'_lbl_wid');
  var rowWid = gid(p+'_row_wid');
  
  if(lblSpd && lblWid && rowWid) {
      rowWid.style.display = 'grid'; 
      lblWid.style.display = 'block'; 
      
      var hint = gid(p+'_hint');
      if(hint) hint.setAttribute('data-relevant',(e==2)?'1':'0');

      if(e==2)      { lblSpd.innerText = L('Tempo (Knight Rider)','Speed (Knight Rider)'); lblWid.innerText = L('Breite (Knight Rider)','Width (Knight Rider)'); }
      else if(e==6) { lblSpd.innerText = L('Regenbogen-Tempo','Rainbow speed');            lblWid.innerText = L('Farbdichte','Color density'); }
      else if(e==7) { lblSpd.innerText = L('Atem-Tempo','Breathing speed');                rowWid.style.display='none'; lblWid.style.display='none'; }
      else if(e==8) { lblSpd.innerText = L('Glitzer-Tempo','Sparkle speed');               lblWid.innerText = L('Sterne (Menge)','Stars (amount)'); }
      else if(e==9) { lblSpd.innerText = L('Meteor-Tempo','Meteor speed');                 lblWid.innerText = L('Schweif-Länge','Trail length'); }
  }
}

function renderControls(){
  var out='';
  var synced=[];
  for(var i=0;i<cfg.count;i++) if(cfg.channels[i].synced) synced.push(i);
  if(synced.length>0){
    out+=buildBlock(-1,L('Synchronisierte Kanäle','Synced channels')+' ('+synced.length+')',cfg.channels[synced[0]]);
  }
  for(var i=0;i<cfg.count;i++){
    if(cfg.channels[i].synced) continue;
    out+=buildBlock(i,L('Kanal','Channel')+' '+(i+1),cfg.channels[i]);
  }
  gid('controls').innerHTML=out;

  if(synced.length>0) updateVis(-1);
  for(var i=0;i<cfg.count;i++) if(!cfg.channels[i].synced) updateVis(i);
}

function buildBlock(idx,title,s){
  var p=pfx(idx);
  var h='<div class="section"><h3>'+title+'</h3>';
  h+='<label>'+L('Effekt','Effect')+'</label>';
  h+='<select id="'+p+'_eff" onchange="onEff('+idx+')">';
  h+='<option value="0"'+(s.effect==0?' selected':'')+'>'+L('Aus','Off')+'</option>';
  h+='<option value="1"'+(s.effect==1?' selected':'')+'>'+L('Feste Farbe','Solid')+'</option>';
  h+='<option value="2"'+(s.effect==2?' selected':'')+'>Knight Rider</option>';
  h+='<option value="3"'+(s.effect==3?' selected':'')+'>'+L('Blaulicht (Police EU)','Police light (EU)')+'</option>';
  h+='<option value="4"'+(s.effect==4?' selected':'')+'>'+L('US-Police (mit Weiß)','US police (with white)')+'</option>';
  h+='<option value="5"'+(s.effect==5?' selected':'')+'>'+L('US-Police (nur Rot/Blau)','US police (red/blue only)')+'</option>';
  h+='<option value="6"'+(s.effect==6?' selected':'')+'>'+L('Regenbogen-Welle','Rainbow Wave')+'</option>';
  h+='<option value="7"'+(s.effect==7?' selected':'')+'>'+L('Atmen / Pulsieren','Breathing')+'</option>';
  h+='<option value="8"'+(s.effect==8?' selected':'')+'>'+L('Glitzern (Sparkle)','Sparkle')+'</option>';
  h+='<option value="9"'+(s.effect==9?' selected':'')+'>'+L('Meteor Schauer','Meteor Rain')+'</option>';
  h+='<option value="10"'+(s.effect==10?' selected':'')+'>'+L('Eigenes LED-Muster','Custom LED pattern')+'</option>';
  h+='</select>';
  
  h+='<div id="'+p+'_wrap_col">';
  h+='<label style="margin-top:10px">'+L('Farbe','Color')+'</label>';
  h+='<input type="color" id="'+p+'_pick" value="'+rgbToHex(s.r,s.g,s.b)+'" oninput="onPick('+idx+')" style="width:100%;height:44px;border:1px solid var(--border2);border-radius:8px;background:var(--bg3);cursor:pointer;padding:2px">';
  h+='<div class="rng-row" style="margin-top:8px"><span>R</span><input type="range" id="'+p+'_r" min="0" max="255" value="'+s.r+'" oninput="onCol('+idx+')"><div class="rng-val" id="'+p+'_rv">'+s.r+'</div></div>';
  h+='<div class="rng-row"><span>G</span><input type="range" id="'+p+'_g" min="0" max="255" value="'+s.g+'" oninput="onCol('+idx+')"><div class="rng-val" id="'+p+'_gv">'+s.g+'</div></div>';
  h+='<div class="rng-row"><span>B</span><input type="range" id="'+p+'_b" min="0" max="255" value="'+s.b+'" oninput="onCol('+idx+')"><div class="rng-val" id="'+p+'_bv">'+s.b+'</div></div>';
  h+='</div>';

  h+='<div id="'+p+'_wrap_bri">';
  h+='<label style="margin-top:10px">'+L('Helligkeit','Brightness')+'</label>';
  h+='<div class="rng-row"><span>&#9788;</span><input type="range" id="'+p+'_br" min="0" max="255" value="'+s.bright+'" oninput="onBri('+idx+')"><div class="rng-val" id="'+p+'_brv">'+s.bright+'</div></div>';
  h+='</div>';

  h+='<div id="'+p+'_wrap_kr">';
  h+='<label id="'+p+'_lbl_spd" style="margin-top:10px">'+L('Tempo (Animation)','Animation Speed')+'</label>';
  h+='<div class="rng-row"><span>&#9201;</span><input type="range" id="'+p+'_spd" min="1" max="100" value="'+s.krspeed+'" oninput="onSpd('+idx+')"><div class="rng-val" id="'+p+'_spdv">'+s.krspeed+'</div></div>';
  h+='<div class="info-note">'+L('1 = langsam &middot; 100 = schnell.','1 = slow &middot; 100 = fast.')+'</div>';
  h+='<label id="'+p+'_lbl_wid" style="margin-top:10px">'+L('Breite / Menge','Width / Amount')+'</label>';
  h+='<div class="rng-row" id="'+p+'_row_wid"><span>&#9646;</span><input type="range" id="'+p+'_wid" min="1" max="50" value="'+s.krwidth+'" oninput="onWid('+idx+')"><div class="rng-val" id="'+p+'_widv">'+s.krwidth+'</div></div>';
  h+='<div class="info-note" id="'+p+'_hint" data-relevant="0">'+L('Tipp: Für den typischen KITT-Effekt Breite auf etwa 1/3 der LED-Anzahl stellen.','Tip: For the typical KITT effect, set the width to about 1/3 of the LED count.')+'</div>';
  h+='</div>';

  h+='<div id="'+p+'_wrap_pol">';
  h+='<label style="margin-top:10px">'+L('Blaulicht-Frequenz (Hz)','Police frequency (Hz)')+'</label>';
  h+='<div class="rng-row"><span>Hz</span><input type="range" id="'+p+'_phz" min="1" max="10" value="'+(s.polhz||4)+'" oninput="onPolHz('+idx+')"><div class="rng-val" id="'+p+'_phzv">'+(s.polhz||4)+'</div></div>';
  h+='<label style="margin-top:10px">'+L('Blitze pro Burst','Flashes per burst')+'</label>';
  h+='<div class="rng-row"><span>&#9646;</span><input type="range" id="'+p+'_pwid" min="1" max="10" value="'+s.krwidth+'" oninput="onPWid('+idx+')"><div class="rng-val" id="'+p+'_pwidv">'+s.krwidth+'</div></div>';
  h+='<div class="info-note">'+L('Frequenz in Hz bestimmt die Geschwindigkeit. Blitze pro Burst bestimmt die Anzahl der kurzen Lichtimpulse pro Seite.','Frequency in Hz controls the speed. Flashes per burst controls the number of short light pulses per side.')+'</div>';
  h+='<div id="'+p+'_wrap_swp" style="margin-top:10px;">';
  h+='<label class="checkbox-row"><input type="checkbox" id="'+p+'_swp" '+(s.swapcolors?'checked':'')+' onchange="onSwp('+idx+',this.checked)">'+L('Seiten tauschen (Rot/Blau)','Swap sides (Red/Blue)')+'</label>';
  h+='</div>';
  h+='</div>';

  h+='<div id="'+p+'_wrap_custom" class="custombox">';
  h+='<label>'+L('Gespeichertes Muster','Saved pattern')+'</label>';
  h+='<select id="'+p+'_pat" onchange="applyPattern('+idx+')">'+patternOptions(-1)+'</select>';
  h+='<div class="info-note">'+L('Ist ein Preset länger als der Kanal, werden überzählige LEDs ignoriert. Ist es kürzer, bleiben zusätzliche LEDs schwarz.','If a preset is longer than the channel, extra LEDs are ignored. If it is shorter, additional LEDs stay black.')+'</div>';
  h+='<div class="customrow"><div><label style="margin-top:8px">'+L('Preset-Name','Preset name')+'</label><input type="text" maxlength="31" id="'+p+'_patname" placeholder="'+L('z.B. Farbe 1','e.g. Color 1')+'"></div>'+
     '<button class="btn green" onclick="savePatternNew('+idx+')">'+L('Speichern','Save')+'</button></div>';
  h+='<div class="customrow three"><div class="patmsg" id="'+p+'_patmsg"></div>'+
     '<button class="btn sm" id="'+p+'_patupdate" onclick="updatePattern('+idx+')" disabled>'+L('Preset aktualisieren','Update preset')+'</button>'+
     '<button class="btn sm" id="'+p+'_patdelete" onclick="deletePattern('+idx+')" disabled>'+L('Löschen','Delete')+'</button></div>';

  h+='<div style="margin-top:12px;padding-top:10px;border-top:1px solid var(--border)">';
  h+='<label>'+L('Muster-Animation','Pattern animation')+'</label>';
  h+='<select id="'+p+'_anim" onchange="onCustomFx('+idx+')">'+
     '<option value="0"'+((s.customanim||0)==0?' selected':'')+'>'+L('Statisch','Static')+'</option>'+
     '<option value="1"'+(s.customanim==1?' selected':'')+'>'+L('Muster bewegen','Move pattern')+'</option>'+
     '<option value="2"'+(s.customanim==2?' selected':'')+'>Ping-Pong</option>'+
     '<option value="3"'+(s.customanim==3?' selected':'')+'>'+L('Helligkeitswelle','Brightness wave')+'</option>'+
     '<option value="4"'+(s.customanim==4?' selected':'')+'>Colorwaves</option>'+
     '<option value="5"'+(s.customanim==5?' selected':'')+'>Twinkle</option>'+
     '<option value="6"'+(s.customanim==6?' selected':'')+'>Preset-Morph</option></select>';
  h+='<div class="info-note" id="'+p+'_animhint" data-relevant="0"></div>';
  h+='<div id="'+p+'_animopts">';
  h+='<label style="margin-top:10px">'+L('Tempo','Speed')+'</label>'+
     '<div class="rng-row"><span>&#9201;</span><input type="range" id="'+p+'_aspeed" min="1" max="100" value="'+(s.customspeed||50)+'" oninput="onCustomFx('+idx+',true)"><div class="rng-val" id="'+p+'_aspeedv">'+(s.customspeed||50)+'</div></div>';
  h+='<div class="info-note">'+L('1 = langsam &middot; 100 = schnell.','1 = slow &middot; 100 = fast.')+'</div>';
  h+='<div id="'+p+'_arevrow" style="margin-top:8px"><label class="checkbox-row"><input type="checkbox" id="'+p+'_arev" '+(s.customreverse?'checked':'')+' onchange="onCustomFx('+idx+')">'+L('Richtung umkehren','Reverse direction')+'</label></div>';
  h+='<div id="'+p+'_aamountrow"><label id="'+p+'_aamountlbl" style="margin-top:10px">'+L('Stärke','Strength')+'</label>'+
     '<div class="rng-row"><span>&#10022;</span><input type="range" id="'+p+'_aamount" min="1" max="100" value="'+(s.customamount||70)+'" oninput="onCustomFx('+idx+',true)"><div class="rng-val" id="'+p+'_aamountv">'+(s.customamount||70)+'</div></div>'+
     '<div class="info-note" id="'+p+'_aamounthint" data-relevant="0"></div></div>';
  h+='<div id="'+p+'_amorphrow"><label style="margin-top:10px">'+L('Morph-Ziel','Morph target')+'</label><select id="'+p+'_morph" onchange="onCustomFx('+idx+')">'+patternOptionsMorph(s.custommorph)+'</select></div>';
  h+='</div></div>';

  h+='<div class="selbox" id="'+p+'_selbox">';
  h+='<div class="selhead"><b id="'+p+'_selcount">'+L('Auswahl: 0 LEDs','Selection: 0 LEDs')+'</b></div>';
  h+='<div class="info-note">'+L('LEDs antippen, um eine oder mehrere gleichzeitig auszuwählen.','Tap LEDs to select one or more at the same time.')+'</div>';
  h+='<div class="selbuttons">'+
     '<button class="btn sm" onclick="selectPixelsMode('+idx+',\'all\')">'+L('Alle','All')+'</button>'+
     '<button class="btn sm" onclick="selectPixelsMode('+idx+',\'none\')">'+L('Keine','None')+'</button>'+
     '<button class="btn sm" onclick="selectPixelsMode('+idx+',\'even\')">'+L('Gerade','Even')+'</button>'+
     '<button class="btn sm" onclick="selectPixelsMode('+idx+',\'odd\')">'+L('Ungerade','Odd')+'</button>'+
     '<button class="btn sm" onclick="selectPixelsMode('+idx+',\'invert\')">'+L('Invertieren','Invert')+'</button></div>';
  h+='<div class="selcolor"><div><label>'+L('Farbe für Auswahl','Color for selection')+'</label><input type="color" id="'+p+'_selcolor" value="#ffffff" disabled oninput="groupPixelPick('+idx+',this.value)"></div>'+
     '<button class="btn sm" id="'+p+'_seloff" disabled onclick="groupPixelsOff('+idx+')">'+L('Schwarz','Black')+'</button></div>';
  h+='<div class="info-note">'+L('Schwarz schaltet nur die ausgewählten LEDs dunkel. Ist der ganze Kanal schwarz, wird der Kanal automatisch ausgeschaltet.','Black only darkens the selected LEDs. If the entire channel is black, the channel is switched off automatically.')+'</div>';
  h+='</div>';

  h+='<div class="info-note">'+L('Farben können auch während einer Animation live geändert werden.','Colors can also be edited live while an animation is running.')+'</div>';
  h+='<div class="pixel-grid" id="'+p+'_pixels"><span style="color:var(--text2);font-size:11px">'+L('Lade LEDs...','Loading LEDs...')+'</span></div>';
  h+='</div>';
  
  h+='</div>';
  return h;
}

function patternOptions(sel){
  var h='<option value="-1">'+L('-- Muster wählen --','-- Select pattern --')+'</option>';
  for(var i=0;i<patterns.length;i++){
    var q=patterns[i];
    h+='<option value="'+q.id+'"'+(q.id==sel?' selected':'')+'>'+escHtml(q.name)+' ('+q.count+' LEDs)</option>';
  }
  return h;
}
function patternOptionsMorph(sel){
  var h='<option value="-1">'+L('-- kein Ziel --','-- no target --')+'</option>';
  for(var i=0;i<patterns.length;i++){
    var q=patterns[i];
    h+='<option value="'+q.id+'"'+(q.id==sel?' selected':'')+'>'+escHtml(q.name)+' ('+q.count+' LEDs)</option>';
  }
  return h;
}
function escHtml(v){
  return String(v).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;').replace(/'/g,'&#39;');
}
function findPattern(id){for(var i=0;i<patterns.length;i++)if(patterns[i].id==id)return patterns[i];return null;}
function patMsg(idx,text,mod){var e=gid(pfx(idx)+'_patmsg');if(e){e.className='patmsg'+(mod?' modified':'');e.textContent=text||'';}}
function refreshPatternSelects(){
  var ids=['sync']; for(var i=0;i<cfg.count;i++)ids.push('ch'+i);
  for(var n=0;n<ids.length;n++){
    var e=gid(ids[n]+'_pat'); if(e){var old=+e.value;e.innerHTML=patternOptions(old);}
    var m=gid(ids[n]+'_morph'); if(m){var mo=+m.value;m.innerHTML=patternOptionsMorph(mo);m.value=String(mo);}
  }
}

function loadPixels(idx){
  var p=pfx(idx), box=gid(p+'_pixels'); if(!box)return;
  fetch('/api/led/pixels?'+targetQuery(idx)).then(function(r){return r.json();}).then(function(d){
    if(!gid(p+'_pixels'))return; // Block wurde inzwischen neu gerendert
    var sel=gid(p+'_pat'); if(sel){sel.innerHTML=patternOptions(d.preset);sel.value=String(d.preset);}
    var q=findPattern(d.preset);
    var ni=gid(p+'_patname'); if(ni&&q)ni.value=q.name;
    var bu=gid(p+'_patupdate'),bd=gid(p+'_patdelete');
    if(bu)bu.disabled=(d.preset<0); if(bd)bd.disabled=(d.preset<0);
    patMsg(idx,d.preset>=0?(q?q.name:L('Preset','Preset'))+(d.modified?L(' (geändert)',' (modified)'):''):(L('Live-Muster','Live pattern')+(d.modified?L(' (geändert)',' (modified)'):'')),d.modified);
    var ae=gid(p+'_anim'); if(ae)ae.value=String(d.anim||0);
    var as=gid(p+'_aspeed'); if(as)as.value=String(d.speed||50); setText(p+'_aspeedv',d.speed||50);
    var aa=gid(p+'_aamount'); if(aa)aa.value=String(d.amount||70); setText(p+'_aamountv',d.amount||70);
    var ar=gid(p+'_arev'); if(ar)ar.checked=!!d.reverse;
    var am=gid(p+'_morph'); if(am){am.innerHTML=patternOptionsMorph(d.morph);am.value=String(d.morph);}
    updateCustomFxVis(idx);
    pixelCounts[p]=d.count;
    var ss=pixelSelections[p]||{};
    for(var sk in ss)if((+sk)>=d.count)delete ss[sk];
    pixelSelections[p]=ss;
    var h='';
    for(var i=0;i<d.count;i++){
      var hex=packedHex(d.pixels[i]||0);
      var sc=ss[i]?' selected':'';
      h+='<div class="pixel'+sc+'" data-pos="'+i+'">'+
         '<button type="button" class="pixel-select" onclick="togglePixelSel('+idx+','+i+',event)">LED '+(i+1)+'</button>'+
         '<input type="color" id="'+p+'_px'+i+'" value="'+hex+'" oninput="pixelPick('+idx+','+i+',this.value)">'+
         '<button class="off" onclick="pixelOff('+idx+','+i+')">'+L('Schwarz','Black')+'</button></div>';
    }
    box.innerHTML=h||'<span style="color:var(--text2);font-size:11px">'+L('Keine LEDs.','No LEDs.')+'</span>';
    updatePixelSelectionUI(idx);
  }).catch(function(){ if(box)box.innerHTML='<span style="color:#ff7676;font-size:11px">'+L('LEDs konnten nicht geladen werden.','Could not load LEDs.')+'</span>'; });
}

function pixelSelKey(idx){return pfx(idx);}
function pixelSelObj(idx){var k=pixelSelKey(idx);if(!pixelSelections[k])pixelSelections[k]={};return pixelSelections[k];}
function selectedPixelPositions(idx){
  var s=pixelSelObj(idx),a=[];
  for(var k in s)if(s[k])a.push(+k);
  a.sort(function(x,y){return x-y;});
  return a;
}
function updatePixelSelectionUI(idx){
  var p=pfx(idx),s=pixelSelObj(idx),box=gid(p+'_pixels');
  if(box){
    var cards=box.querySelectorAll('.pixel[data-pos]');
    for(var i=0;i<cards.length;i++){
      var pos=+cards[i].getAttribute('data-pos');
      if(s[pos])cards[i].classList.add('selected');else cards[i].classList.remove('selected');
    }
  }
  var a=selectedPixelPositions(idx),n=a.length;
  var ce=gid(p+'_selcount');if(ce)ce.textContent=L('Auswahl: ','Selection: ')+n+' LED'+(n==1?'':'s');
  var cp=gid(p+'_selcolor'),off=gid(p+'_seloff');
  if(cp)cp.disabled=(n==0);if(off)off.disabled=(n==0);
  if(cp&&n>0){
    var first=gid(p+'_px'+a[0]);
    if(first)cp.value=first.value;
  }
}
function clearPixelSelection(idx){
  var p=pfx(idx);pixelSelections[p]={};pixelLastSel[p]=-1;updatePixelSelectionUI(idx);
}
function togglePixelSel(idx,pos,ev){
  var p=pfx(idx),s=pixelSelObj(idx),last=(pixelLastSel[p]===undefined?-1:pixelLastSel[p]);
  if(ev&&ev.shiftKey&&last>=0){
    var a=Math.min(last,pos),b=Math.max(last,pos);
    for(var i=a;i<=b;i++)s[i]=true;
  }else{
    s[pos]=!s[pos];if(!s[pos])delete s[pos];
  }
  pixelLastSel[p]=pos;updatePixelSelectionUI(idx);
}
function selectPixelsMode(idx,mode){
  var p=pfx(idx),n=pixelCounts[p]||0,s=pixelSelObj(idx);
  if(mode=='none'){pixelSelections[p]={};pixelLastSel[p]=-1;updatePixelSelectionUI(idx);return;}
  if(mode=='all'){s={};for(var i=0;i<n;i++)s[i]=true;pixelSelections[p]=s;}
  else if(mode=='even'){s={};for(var i=0;i<n;i++)if(((i+1)%2)==0)s[i]=true;pixelSelections[p]=s;}
  else if(mode=='odd'){s={};for(var i=0;i<n;i++)if(((i+1)%2)==1)s[i]=true;pixelSelections[p]=s;}
  else if(mode=='invert'){
    var ns={};for(var i=0;i<n;i++)if(!s[i])ns[i]=true;pixelSelections[p]=ns;
  }
  updatePixelSelectionUI(idx);
}
function debGroupPixels(idx,positions,r,g,b){
  if(!positions.length)return;
  var key=pfx(idx);clearTimeout(pixelGroupTimers[key]);
  var plist=positions.join(',');
  pixelGroupTimers[key]=setTimeout(function(){
    send('/api/led/pixels/batch?'+targetQuery(idx)+'&p='+encodeURIComponent(plist)+'&r='+r+'&g='+g+'&b='+b).catch(function(){});
  },55);
}
function groupPixelPick(idx,hex){
  var a=selectedPixelPositions(idx);if(!a.length)return;
  var r=parseInt(hex.substr(1,2),16),g=parseInt(hex.substr(3,2),16),b=parseInt(hex.substr(5,2),16);
  var p=pfx(idx);
  for(var i=0;i<a.length;i++){var e=gid(p+'_px'+a[i]);if(e)e.value=hex;}
  debGroupPixels(idx,a,r,g,b);patMsg(idx,L('Live geändert (','Live changed (')+a.length+' LEDs)',true);
  applyLocal(idx,function(c){c.effect=10;});
}
function groupPixelsOff(idx){
  var a=selectedPixelPositions(idx);if(!a.length)return;
  var p=pfx(idx),cp=gid(p+'_selcolor');if(cp)cp.value='#000000';
  for(var i=0;i<a.length;i++){var e=gid(p+'_px'+a[i]);if(e)e.value='#000000';}
  debGroupPixels(idx,a,0,0,0);patMsg(idx,L('Live geändert (','Live changed (')+a.length+L(' LEDs aus)',' LEDs black)'),true);
}

function debPixel(idx,pos,r,g,b){
  var key=pfx(idx)+'_'+pos; clearTimeout(pixelTimers[key]);
  pixelTimers[key]=setTimeout(function(){send('/api/led/pixel?'+targetQuery(idx)+'&p='+pos+'&r='+r+'&g='+g+'&b='+b).catch(function(){});},35);
}
function pixelPick(idx,pos,hex){
  var r=parseInt(hex.substr(1,2),16),g=parseInt(hex.substr(3,2),16),b=parseInt(hex.substr(5,2),16);
  debPixel(idx,pos,r,g,b); patMsg(idx,L('Live geändert','Live changed'),true);
  applyLocal(idx,function(c){c.effect=10;});
}
function pixelOff(idx,pos){
  var e=gid(pfx(idx)+'_px'+pos); if(e)e.value='#000000';
  debPixel(idx,pos,0,0,0); patMsg(idx,L('Live geändert','Live changed'),true);
}
function applyPattern(idx){
  var p=pfx(idx),sel=+gv(p+'_pat'); if(sel<0)return;
  var q=findPattern(sel); if(q&&gid(p+'_patname'))gid(p+'_patname').value=q.name;
  patMsg(idx,L('Lade Preset...','Loading preset...'),false);
  send('/api/led/pattern/apply?'+targetQuery(idx)+'&id='+sel).then(function(r){if(!r.ok)throw 0;return r.json();}).then(function(){
    var pe=gid(p+'_eff'); if(pe)pe.value='10'; applyLocal(idx,function(c){c.effect=10;}); clearPixelSelection(idx); updateVis(idx); loadPixels(idx); updateBigOff();
  }).catch(function(){patMsg(idx,L('Preset konnte nicht geladen werden.','Could not load preset.'),true);});
}
function savePatternNew(idx){
  var p=pfx(idx),name=gid(p+'_patname')?gid(p+'_patname').value.trim():'';
  var url='/api/led/pattern/save?'+targetQuery(idx)+'&name='+encodeURIComponent(name);
  patMsg(idx,L('Speichere...','Saving...'),false);
  send(url).then(function(r){if(!r.ok)throw 0;return r.json();}).then(function(d){
    loadPatterns(function(){refreshPatternSelects();var e=gid(p+'_pat');if(e)e.value=String(d.id);loadPixels(idx);});
  }).catch(function(){patMsg(idx,L('Speichern fehlgeschlagen / Preset-Speicher voll.','Saving failed / preset storage full.'),true);});
}
function updatePattern(idx){
  var p=pfx(idx),id=+gv(p+'_pat'); if(id<0)return;
  var name=gid(p+'_patname')?gid(p+'_patname').value.trim():'';
  patMsg(idx,L('Aktualisiere...','Updating...'),false);
  send('/api/led/pattern/save?'+targetQuery(idx)+'&id='+id+'&name='+encodeURIComponent(name)).then(function(r){if(!r.ok)throw 0;return r.json();}).then(function(){
    loadPatterns(function(){refreshPatternSelects();var e=gid(p+'_pat');if(e)e.value=String(id);loadPixels(idx);});
  }).catch(function(){patMsg(idx,L('Aktualisieren fehlgeschlagen.','Update failed.'),true);});
}
function deletePattern(idx){
  var p=pfx(idx),id=+gv(p+'_pat'); if(id<0)return;
  send('/api/led/pattern/delete?id='+id).then(function(r){if(!r.ok)throw 0;return r.text();}).then(function(){
    loadPatterns(function(){refreshPatternSelects();var e=gid(p+'_pat');if(e)e.value='-1';var ni=gid(p+'_patname');if(ni)ni.value='';loadPixels(idx);});
  }).catch(function(){patMsg(idx,L('Löschen fehlgeschlagen.','Delete failed.'),true);});
}

function updateCustomFxVis(idx){
  var p=pfx(idx),e=gid(p+'_anim'); if(!e)return;
  var m=+e.value;
  var opts=gid(p+'_animopts'),rev=gid(p+'_arevrow'),amt=gid(p+'_aamountrow'),morph=gid(p+'_amorphrow'),lbl=gid(p+'_aamountlbl');
  var ah=gid(p+'_animhint'), amountHint=gid(p+'_aamounthint');
  if(opts)opts.style.display=(m==0)?'none':'block';
  if(rev)rev.style.display=(m==1||m==2||m==3||m==4)?'block':'none';
  if(amt)amt.style.display=(m==3||m==4||m==5)?'block':'none';
  if(morph)morph.style.display=(m==6)?'block':'none';
  if(lbl){if(m==3)lbl.textContent=L('Wellentiefe','Wave depth');else if(m==4)lbl.textContent=L('Wellenbreite','Wave width');else if(m==5)lbl.textContent=L('Twinkle-Dichte','Twinkle density');else lbl.textContent=L('Stärke','Strength');}
  if(amountHint){
    var atxt='';
    if(m==3)atxt=L('Bestimmt, wie stark die Helligkeit schwankt. 1 = leichte, bereits sichtbare Welle · 100 = maximale Welle bis ganz dunkel.','Controls how strongly brightness changes. 1 = a light but visible wave · 100 = maximum wave down to fully dark.');
    else if(m==4)atxt=L('Bestimmt die räumliche Wellenbreite. 1 = breite Farbverläufe · 100 = viele schmalere Farbwellen.','Controls the spatial wave width. 1 = broad color transitions · 100 = many narrower color waves.');
    else if(m==5)atxt=L('Bestimmt, wie viele LEDs funkeln. 1 = sehr wenige · 100 = sehr viele.','Controls how many LEDs twinkle. 1 = very few · 100 = very many.');
    amountHint.textContent=atxt;
    amountHint.setAttribute('data-relevant',atxt?'1':'0');
  }
  if(ah){
    var txt='';
    if(m==1)txt=L('Verschiebt das komplette LED-Muster fortlaufend über den Kanal.','Continuously moves the complete LED pattern across the channel.');
    else if(m==2)txt=L('Das Muster läuft bis zum Ende und anschließend wieder zurück.','The pattern moves to the end and then back again.');
    else if(m==3)txt=L('Die Farben bleiben erhalten – nur die Helligkeit wandert über die LEDs.','The colors stay unchanged; only the brightness travels across the LEDs.');
    else if(m==4)txt=L('Die Farben des Musters werden weich ineinander überblendet.','The pattern colors are smoothly blended into each other.');
    else if(m==5)txt=L('Einzelne LEDs funkeln zufällig über dem vorhandenen Muster.','Individual LEDs twinkle randomly over the existing pattern.');
    else if(m==6)txt=L('Blendet zwischen dem aktuellen Muster und dem gewählten Ziel-Preset hin und her.','Fades back and forth between the current pattern and the selected target preset.');
    ah.textContent=txt;
    ah.setAttribute('data-relevant',txt?'1':'0');
  }
}
function onCustomFx(idx,deb){
  var p=pfx(idx),mode=+gv(p+'_anim'),speed=+gv(p+'_aspeed'),amount=+gv(p+'_aamount');
  var rev=gid(p+'_arev')&&gid(p+'_arev').checked?1:0;
  var morph=gid(p+'_morph')?+gv(p+'_morph'):-1;
  setText(p+'_aspeedv',speed);setText(p+'_aamountv',amount);updateCustomFxVis(idx);
  var url='/api/led/customfx?'+targetQuery(idx)+'&mode='+mode+'&speed='+speed+'&amount='+amount+'&reverse='+rev+'&morph='+morph;
  if(deb)debSend(url);else fire(url);
  applyLocal(idx,function(c){c.effect=10;c.customanim=mode;c.customspeed=speed;c.customamount=amount;c.customreverse=!!rev;c.custommorph=morph;});
}

function applyLocal(idx,fn){
  if(idx<0){for(var i=0;i<cfg.count;i++) if(cfg.channels[i].synced) fn(cfg.channels[i]);}
  else if(cfg.channels[idx]) fn(cfg.channels[idx]);
}

function bigIsOn(){   // true wenn irgendein Kanal an ist (Effekt != 0)
  if(!cfg||!cfg.channels) return false;
  for(var i=0;i<cfg.channels.length;i++) if(cfg.channels[i].effect!==0) return true;
  return false;
}
function updateBigOff(){
  var b=gid('bigOff'); if(!b) return;
  var on=bigIsOn();
  b.textContent=on?'AUS':'AN';
  b.style.background=on?'linear-gradient(#d32f2f,#8e0000)':'linear-gradient(#2e7d32,#1b5e20)';
  b.style.borderColor=on?'#ff5a5a':'#5aff7a';
}
function ledsBigToggle(){
  if(bigIsOn()){
    fire('/api/led/alloff');
    if(cfg&&cfg.channels) for(var i=0;i<cfg.channels.length;i++) cfg.channels[i].effect=0;
  } else {
    fire('/api/led/allon');
    if(cfg&&cfg.channels) for(var i=0;i<cfg.channels.length;i++) cfg.channels[i].effect=1;
  }
  renderControls(); updateBigOff();
}
function onEff(idx){
  var p=pfx(idx),e=+gv(p+'_eff');
  updateVis(idx); 
  fire('/api/led/effect?'+tgt(idx)+'&e='+e);
  applyLocal(idx,function(c){c.effect=e;});
  updateBigOff();
  if (e==3){ 
    var r=0,g=0,b=255;
    if(gid(p+'_r')) gid(p+'_r').value=r;
    if(gid(p+'_g')) gid(p+'_g').value=g;
    if(gid(p+'_b')) gid(p+'_b').value=b;
    setText(p+'_rv',r);setText(p+'_gv',g);setText(p+'_bv',b);
    if(gid(p+'_pick')) gid(p+'_pick').value=rgbToHex(r,g,b);
    debSend('/api/led/color?'+tgt(idx)+'&r='+r+'&g='+g+'&b='+b);
    applyLocal(idx,function(c){c.r=r;c.g=g;c.b=b;});
  }
}
function onCol(idx){
  var p=pfx(idx),r=+gv(p+'_r'),g=+gv(p+'_g'),b=+gv(p+'_b');
  setText(p+'_rv',r);setText(p+'_gv',g);setText(p+'_bv',b);
  gid(p+'_pick').value=rgbToHex(r,g,b);
  debSend('/api/led/color?'+tgt(idx)+'&r='+r+'&g='+g+'&b='+b);
  applyLocal(idx,function(c){c.r=r;c.g=g;c.b=b;});
}
function onPick(idx){
  var p=pfx(idx),hex=gid(p+'_pick').value;
  var r=parseInt(hex.substr(1,2),16),g=parseInt(hex.substr(3,2),16),b=parseInt(hex.substr(5,2),16);
  gid(p+'_r').value=r;gid(p+'_g').value=g;gid(p+'_b').value=b;
  setText(p+'_rv',r);setText(p+'_gv',g);setText(p+'_bv',b);
  debSend('/api/led/color?'+tgt(idx)+'&r='+r+'&g='+g+'&b='+b);
  applyLocal(idx,function(c){c.r=r;c.g=g;c.b=b;});
}
function onBri(idx){
  var p=pfx(idx),v=+gv(p+'_br');
  setText(p+'_brv',v);
  debSend('/api/led/bright?'+tgt(idx)+'&v='+v);
  applyLocal(idx,function(c){c.bright=v;});
}
function onSpd(idx){
  var p=pfx(idx),v=+gv(p+'_spd');
  setText(p+'_spdv',v);
  debSend('/api/led/krspeed?'+tgt(idx)+'&v='+v);
  applyLocal(idx,function(c){c.krspeed=v;});
}

function onWid(idx){
  var p=pfx(idx),v=+gv(p+'_wid');
  setText(p+'_widv',v);
  if(gid(p+'_pwid')) gid(p+'_pwid').value=v; 
  setText(p+'_pwidv',v);
  debSend('/api/led/krwidth?'+tgt(idx)+'&v='+v);
  applyLocal(idx,function(c){c.krwidth=v;});
}

function onPWid(idx){
  var p=pfx(idx),v=+gv(p+'_pwid');
  setText(p+'_pwidv',v);
  if(gid(p+'_wid')) gid(p+'_wid').value=v; 
  setText(p+'_widv',v);
  debSend('/api/led/krwidth?'+tgt(idx)+'&v='+v);
  applyLocal(idx,function(c){c.krwidth=v;});
}

function onPolHz(idx){
  var p=pfx(idx),v=+gv(p+'_phz');
  setText(p+'_phzv',v);
  debSend('/api/led/polhz?'+tgt(idx)+'&v='+v);
  applyLocal(idx,function(c){c.polhz=v;});
}

function onSwp(idx, checked){
  var v=checked?1:0;
  debSend('/api/led/swapcol?'+tgt(idx)+'&v='+v);
  applyLocal(idx,function(c){c.swapcolors=checked;});
}

function toggleSync(i,on){
  cfg.channels[i].synced=on;
  fire('/api/led/sync?ch='+i+'&on='+(on?1:0));
  renderControls();
}
function onPolRole(i,role){
  cfg.channels[i].polrole=role;
  fire('/api/led/polrole?ch='+i+'&role='+role);
  renderHw();
}

function chCountDelta(d){
  var n=cfg.count+d; if(n<1)n=1; if(n>4)n=4;
  if(n===cfg.count)return;
  fetch('/api/led/channels?n='+n,{method:'POST'}).then(function(){ load(); }).catch(function(){});
}

function applyHw(){
  var qs=[], usedPins={}, em=gid('hwmsg');
  function belegt(pin,by){ em.textContent=L('GPIO '+pin+' bereits belegt ('+by+')','GPIO '+pin+' already in use ('+by+')'); em.className='msg err'; }
  for(var i=0;i<cfg.count;i++){
    var pv=(gv('hwpin'+i)||'').trim();
    var pin=(pv==='')?-1:parseInt(pv); if(isNaN(pin))pin=-1;
    var cnt=parseInt(gv('hwcnt'+i))||30;
    var co=parseInt(gv('hwco'+i))||0;
    if(pin>=0){
      if(pin===vescRx){ belegt(pin,'VESC RX'); return; }
      if(pin===vescTx){ belegt(pin,'VESC TX'); return; }
      if(usedPins[pin]!==undefined){ belegt(pin,L('Kanal ','Channel ')+(usedPins[pin]+1)); return; }
      usedPins[pin]=i;
    }
    qs.push('p'+i+'='+pin+'&n'+i+'='+cnt+'&o'+i+'='+co);
  }
  var ka=parseInt(gv('hwka')); if(isNaN(ka)||ka<0)ka=150; if(ka>5000)ka=5000;
  var msg=gid('hwmsg');
  fetch('/api/led/hw?'+qs.join('&')+'&ka='+ka,{method:'POST'}).then(function(r){
    if(r.ok){msg.textContent=L('Übernommen','Applied');msg.className='msg ok';}
    else{msg.textContent='Error';msg.className='msg err';}
    setTimeout(function(){msg.className='msg';},2000);
    load();
  }).catch(function(){msg.textContent='Error';msg.className='msg err';});
}

// API-Tab nur anzeigen, wenn Debug freigeschaltet ist (serverseitiges RAM-Flag,
// per 8x-Tippen auf den Titel der Hauptseite; gilt bis zum ESP-Neustart).
function checkApiUnlock(){
  fetch('/api/debug/unlock',{cache:'no-store'}).then(function(r){return r.ok?r.json():null;}).then(function(j){
    if(j&&j.unlocked){var el=document.getElementById('tab-api-link');if(el)el.style.display='';}
  }).catch(function(){});
}
applyInfoState();
checkApiUnlock();
load();
</script>
</body>
</html>
)ledslit";

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
    ledServer->send(200, "text/html", LEDS_PAGE_HTML);
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

  Serial.println("LEDs: multi-channel /leds page + API registered");
}