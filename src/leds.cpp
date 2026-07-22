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
  int  pin     = 4;     
  int  count   = 30;    
  bool synced  = false; 
  int  effect  = 0;     // 0=Aus, 1=Solid, 2=KR, 3=Pol(EU), 4=Pol(US Weiß), 5=Pol(US WigWag), 6=Rainbow, 7=Breath, 8=Sparkle, 9=Meteor, 10=Fire
  int  r       = 0;
  int  g       = 0;
  int  b       = 255;
  int  bright  = 128;
  int  krSpeed = 30;    // ms pro Schritt
  int  krWidth = 3;     // Universal-Parameter (Breite, Menge, Dichte, Höhe)
  int  polHz   = 4;     // Zyklus-Frequenz
  bool swapColors = false; // Tauscht bei US-Police Links/Rechts
  Adafruit_NeoPixel *strip = nullptr;
  
  // Animationszustände
  int  krPos      = 0;
  int  krDir      = 1;
  unsigned long krLastStep = 0;
  
  int  polSide    = 0;   
  int  polFlash   = 0;   
  bool polOn      = false; 
  bool polForce   = false; // erzwingt Neuzeichnen bei Farb-/Helligkeits-/Parameteraenderung waehrend Police laeuft
  int  polSig     = -1;    // Signatur des zuletzt gezeichneten Sichtzustands (An/Aus + Phase); -1 = "noch nie gezeichnet"
  uint16_t rbHue  = 0;     // zeitbasierter Hue-Akkumulator fuer Rainbow (laeuft sauber ueber, kein int-Overflow)
  float    fireT  = 0.0f;  // Zeit-Koordinate ins Noise-Feld fuer den Feuer-Effekt (stroemt ueber den Streifen)
};

static LedChannel ch[LED_MAX_CHANNELS];
static int        channelCount = 1;

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

static const int PIN_MIN = 0, PIN_MAX = 48;
static const int CNT_MIN = 1, CNT_MAX = 300;

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

// ── Clamp ─────────────────────────────────────────────────────────────────────
static void clampChannel(int i) {
  LedChannel &c = ch[i];
  if (c.pin   < PIN_MIN) c.pin = PIN_MIN;  if (c.pin   > PIN_MAX) c.pin = PIN_MAX;
  if (c.count < CNT_MIN) c.count = CNT_MIN; if (c.count > CNT_MAX) c.count = CNT_MAX;
  if (c.effect < 0 || c.effect > 10) c.effect = 0;
  if (c.r < 0) c.r = 0; if (c.r > 255) c.r = 255;
  if (c.g < 0) c.g = 0; if (c.g > 255) c.g = 255;
  if (c.b < 0) c.b = 0; if (c.b > 255) c.b = 255;
  if (c.bright < 0) c.bright = 0; if (c.bright > 255) c.bright = 255;
  
  if (c.krSpeed < 1) c.krSpeed = 1; if (c.krSpeed > 500) c.krSpeed = 500;
  if (c.krWidth < 1) c.krWidth = 1; if (c.krWidth > 50) c.krWidth = 50;
  if (c.polHz < 1) c.polHz = 1;     if (c.polHz > 10) c.polHz = 10;
}
static void clampAll() { for (int i = 0; i < LED_MAX_CHANNELS; i++) clampChannel(i); }

// ── Config laden / speichern ──────────────────────────────────────────────────
static void ledsLoadConfig() {
  ledPrefs.begin("leds", false);
  channelCount = ledPrefs.getInt("chcnt", 1);
  if (channelCount < 1) channelCount = 1;
  if (channelCount > LED_MAX_CHANNELS) channelCount = LED_MAX_CHANNELS;
  for (int i = 0; i < LED_MAX_CHANNELS; i++) {
    String p = "c" + String(i);
    ch[i].pin       = ledPrefs.getInt ((p + "pin").c_str(), 4);
    ch[i].count     = ledPrefs.getInt ((p + "cnt").c_str(), 30);
    ch[i].effect    = ledPrefs.getInt ((p + "eff").c_str(), 0);
    ch[i].r         = ledPrefs.getInt ((p + "r").c_str(),   0);
    ch[i].g         = ledPrefs.getInt ((p + "g").c_str(),   0);
    ch[i].b         = ledPrefs.getInt ((p + "b").c_str(),   255);
    ch[i].bright    = ledPrefs.getInt ((p + "br").c_str(),  128);
    ch[i].krSpeed   = ledPrefs.getInt ((p + "spd").c_str(), 30);
    ch[i].krWidth   = ledPrefs.getInt ((p + "wid").c_str(), 3);
    ch[i].polHz     = ledPrefs.getInt ((p + "phz").c_str(), 4);
    ch[i].synced    = ledPrefs.getBool((p + "syn").c_str(), false);
    ch[i].swapColors = ledPrefs.getBool((p + "swp").c_str(), false);
  }
  ledPrefs.end();
  clampAll();
}

static void ledsSaveConfig() {
  clampAll();
  ledPrefs.begin("leds", false);
  ledPrefs.putInt("chcnt", channelCount);
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
  }
  ledPrefs.end();
}

// ── Debounced NVS Save ────────────────────────────────────────────────────────
static const unsigned long LED_SAVE_DEBOUNCE_MS = 1500;
static bool          ledSavePending = false;
static unsigned long ledSaveLastReq = 0;

static const unsigned long LED_FRAME_MS = 25;     
static bool          ledDirty[LED_MAX_CHANNELS]     = { false };
static bool          ledForceShow[LED_MAX_CHANNELS] = { false }; // umgeht die 25ms-Drosselung (nur fuer event-getriebene Effekte wie Police)
static unsigned long ledLastShow = 0;

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
  c.strip = new Adafruit_NeoPixel(c.count, c.pin, NEO_GRB + NEO_KHZ800);
  c.strip->begin();
  c.strip->setBrightness(c.bright);
  c.strip->clear(); c.strip->show();
  c.krPos = 0; c.krDir = 1; c.krLastStep = 0;
  c.polSide = 0; c.polFlash = 0; c.polOn = false; c.polForce = false; c.polSig = -1;
  c.rbHue = 0; c.fireT = 0.0f;
  ledDirty[i] = false; ledForceShow[i] = false;
  Serial.printf("LEDs ch%d: init pin=%d count=%d\n", i, c.pin, c.count);
}

// ── Effekt Reset & Anwenden ──────────────────────────────────────────────────
static void applyChannel(int i) {
  LedChannel &c = ch[i];
  if (!c.strip) return;
  c.strip->setBrightness(c.bright);
  
  if (c.effect == 0) {
    c.strip->clear();
    markDirtyNow(i);
  } else if (c.effect == 1) {
    uint32_t col = c.strip->Color(c.strip->gamma8(c.r), c.strip->gamma8(c.g), c.strip->gamma8(c.b));
    for (int p = 0; p < c.count; p++) c.strip->setPixelColor(p, col);
    markDirtyNow(i);
  } else if (c.effect == 2 || (c.effect >= 6 && c.effect <= 10)) {
    c.krPos = 0; c.krDir = 1; c.rbHue = 0; c.krLastStep = 0;
    if (c.effect == 10) c.fireT = 0.0f;
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
  uint32_t interval = (uint32_t)c.krSpeed;
  if (interval < LED_FRAME_MS) interval = LED_FRAME_MS;   // nie schneller rechnen/schieben als angezeigt werden kann
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

  uint32_t cycleTimeMs = 1000UL / hz;
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

      if (c.effect == 3) {
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
  uint32_t adv = (elapsed * 1000UL) / (uint32_t)c.krSpeed;
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

  // sin() haengt an now (absolute Zeit) -> Tempo kommt aus krSpeed, nicht aus der Abtastrate.
  float phase = (sin(now / (float)(c.krSpeed * 10.0)) + 1.0) / 2.0; 
  phase = phase * phase * (3.0 - 2.0 * phase); 

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
  uint32_t interval = (uint32_t)c.krSpeed;
  if (interval < LED_FRAME_MS) interval = LED_FRAME_MS;
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
  uint32_t interval = (uint32_t)c.krSpeed;
  if (interval < LED_FRAME_MS) interval = LED_FRAME_MS;
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

// ── 10: Horizontales Feuer (fliessendes Noise-Feuer) ──────────────────────────
// Statt koerniger Einzelfunken: ein mehroktaviges Value-Noise-Feld, das ueber den
// Streifen stroemt -> weiche, organisch wabernde Flammenzungen. Beide Regler wirken
// stufenlos: krSpeed = Stroemungstempo, krWidth = Intensitaet/Helligkeit.
static inline uint32_t fireHash32(uint32_t a) {
  a ^= a >> 16; a *= 0x7feb352dUL;
  a ^= a >> 15; a *= 0x846ca68bUL;
  a ^= a >> 16; return a;
}
// Zufallswert 0..1 am Gitterpunkt (xi,yi). Die Zeit-Achse (yi) ist periodisch (4096)
// -> nahtlose Endlosschleife und stabile float-Praezision auch nach Stunden Laufzeit.
static inline float fireVHash(int xi, int yi) {
  yi &= 4095;
  uint32_t h = fireHash32(((uint32_t)xi * 0x1000193UL) ^ ((uint32_t)yi * 0x9E3779B1UL));
  return (h & 0xFFFF) * (1.0f / 65535.0f);
}
static inline float fireSmooth(float t) { return t * t * (3.0f - 2.0f * t); }
static float fireVNoise(float x, float y) {
  int xi = (int)x, yi = (int)y;                 // x,y >= 0 -> Trunkierung = floor
  float xf = x - xi, yf = y - yi;
  float u = fireSmooth(xf), v = fireSmooth(yf);
  float a  = fireVHash(xi,     yi);
  float b  = fireVHash(xi + 1, yi);
  float cc = fireVHash(xi,     yi + 1);
  float d  = fireVHash(xi + 1, yi + 1);
  float top = a  + u * (b  - a);
  float bot = cc + u * (d  - cc);
  return top + v * (bot - top);
}
static float fireFbm(float x, float y) {        // 3 Oktaven -> 0..1
  float s = 0, amp = 0.6f, f = 1.0f, norm = 0;
  for (int o = 0; o < 3; o++) {
    s += amp * fireVNoise(x * f, y * f);
    norm += amp; f *= 2.0f; amp *= 0.5f;
  }
  return s / norm;
}

// Warme Feuer-Palette: schwarz -> dunkelrot -> rot -> orange -> gold -> warmes Gelb.
// Kein reines Weiss -> sieht nach Glut/Flamme aus, nicht nach Blitzlicht.
static uint32_t firePalette(Adafruit_NeoPixel *s, uint8_t h) {
  static const uint8_t stops[6][4] = {          // {heat, r, g, b}
    {  0,   0,   0,   0},
    { 40,  60,   0,   0},
    {100, 185,  22,   0},
    {160, 255,  82,   0},
    {210, 255, 160,  22},
    {255, 255, 230, 140},
  };
  int k = 0;
  while (k < 4 && h > stops[k + 1][0]) k++;
  const uint8_t *a = stops[k];
  const uint8_t *b = stops[k + 1];
  int span = b[0] - a[0]; if (span < 1) span = 1;
  int t = ((int)h - a[0]) * 255 / span;         // 0..255 innerhalb des Segments
  uint8_t r  = a[1] + (int)(b[1] - a[1]) * t / 255;
  uint8_t g  = a[2] + (int)(b[2] - a[2]) * t / 255;
  uint8_t bl = a[3] + (int)(b[3] - a[3]) * t / 255;
  return s->Color(s->gamma8(r), s->gamma8(g), s->gamma8(bl));
}

static void fireChannel(int i) {
  LedChannel &c = ch[i];
  if (!c.strip) return;
  uint32_t now = millis();
  if (now - c.krLastStep < LED_FRAME_MS) return;
  uint32_t elapsed = now - c.krLastStep;
  c.krLastStep = now;

  const int N = c.count;

  // krSpeed 1..500 (klein = schnell): Stroemungstempo des Noise-Feldes.
  // Kalibriert auf echte Flammen-Flackerphysik: das Band 4.5..14 Hz deckt den ganzen
  // Regler ab (Kerze ~10-12 Hz, grosse Diffusionsflammen ~10-20 Hz, ruhige Luft bis ~4 Hz).
  // Reglermitte (~ks 250) landet bei ~9 Hz = natuerlichster, kerzenartiger Punkt.
  int ks = constrain(c.krSpeed, 1, 500);
  c.fireT += elapsed * (0.00374f + (500 - ks) * 0.0000158f);
  if (c.fireT >= 4096.0f) c.fireT -= 4096.0f;   // periodisch -> nahtlos + float-stabil

  // krWidth 1..50: Intensitaet/Helligkeit (multiplikativ -> keine Weiss-Saettigung).
  int kw = constrain(c.krWidth, 1, 50);
  float inten = 0.35f + kw * 0.016f;            // ~0.37 (dunkle Glut) .. 1.15 (loderndes Hell)

  const float xscale = 0.14f;                   // Flammenbreite entlang des Streifens
  for (int p = 0; p < N; p++) {
    float n = fireFbm(p * xscale, c.fireT);
    n = n * n * (3.0f - 2.0f * n);              // Kontrast: dunkle Luecken, klarere Zungen
    int h = (int)(n * 255.0f * inten);
    if (h > 255) h = 255; else if (h < 0) h = 0;
    c.strip->setPixelColor(p, firePalette(c.strip, (uint8_t)h));
  }
  markDirty(i);
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

void ledsLoop(int32_t erpm) {
  (void)erpm;
  ledsFrameNow = millis();

  for (int i = 0; i < channelCount; i++) {
    if (ch[i].effect == 2)                        knightRiderChannel(i);
    else if (ch[i].effect >= 3 && ch[i].effect <= 5)   policeChannel(i);
    else if (ch[i].effect == 6)                   rainbowChannel(i);
    else if (ch[i].effect == 7)                   breathingChannel(i);
    else if (ch[i].effect == 8)                   sparkleChannel(i);
    else if (ch[i].effect == 9)                   meteorChannel(i);
    else if (ch[i].effect == 10)                  fireChannel(i);
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
    if (ch[i].strip) {
      ch[i].strip->clear();
      ch[i].strip->show();
    }
    ledDirty[i]     = false;
    ledForceShow[i] = false;
  }
  ledsUnlock();
}

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

// ── HTML Weboberfläche ────────────────────────────────────────────────────────
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
    <div class="tab" onclick="location.href='/?tab=api'">API</div>
    <div class="tab active" onclick="location.href='/leds'">LED</div>
  </div>

  <div class="section">
    <h3 id="lbl-hw">Channels</h3>
    <div id="hwrows"></div>
    <button class="btn" onclick="applyHw()" id="btn-hw">Apply hardware</button>
    <div class="msg" id="hwmsg"></div>
  </div>

  <div id="controls"></div>
</div>

<script>
var lang=(document.cookie.match(/lang=([a-z]+)/)||[])[1]||(navigator.language.startsWith('de')?'de':'en');
function de(){return lang==='de';}
function gid(id){return document.getElementById(id);}

var theme=(document.cookie.match(/theme=([a-z]+)/)||[])[1]||(window.matchMedia('(prefers-color-scheme:dark)').matches?'dark':'light');
function applyTheme(){document.documentElement.setAttribute('data-theme',theme);var b=gid('themeBtn');if(b)b.textContent=theme==='dark'?'\u2600\uFE0F':'\uD83C\uDF19';document.cookie='theme='+theme+';path=/;max-age=31536000';}
function toggleTheme(){theme=theme==='dark'?'light':'dark';applyTheme();}
applyTheme();
function toggleLang(){lang=lang==='de'?'en':'de';document.cookie='lang='+lang+';path=/;max-age=31536000';location.reload();}
gid('langBtn').textContent=de()?'EN':'DE';

(function(){
  var s=function(id,en,d){var el=gid(id);if(el)el.textContent=de()?d:en;};
  s('lbl-hw','Channels','Kanäle');
  s('btn-hw','Apply hardware','Hardware übernehmen');
  var sb=gid('statusBar'); if(sb && sb.textContent==='Loading...') sb.textContent=de()?'Lädt...':'Loading...';
})();

function loadStatus(){
  fetch('/api/info').then(function(r){return r.json();}).then(function(d){
    gid('statusBar').textContent=d.mode==='ap'&&!d.ssid?'AP: '+d.ip:'WiFi: '+d.ssid+' ('+d.ip+')';
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

var cfg={count:1,channels:[]};
var t=null;
function send(url){fetch(url,{method:'POST'}).catch(function(){});}
function debSend(url){clearTimeout(t);t=setTimeout(function(){send(url);},60);}

function load(){
  fetch('/api/led/config').then(function(r){return r.json();}).then(function(d){
    cfg=d; render();
  });
}

function render(){ renderHw(); renderControls(); }

function renderHw(){
  var h='';
  h+='<div class="cnt-ctrl">';
  h+='<span style="font-size:13px;color:var(--text2)">'+(de()?'Aktive Kanäle':'Active channels')+': '+cfg.count+'</span>';
  h+='<button class="btn red sm" onclick="chCountDelta(-1)" '+(cfg.count<=1?'disabled':'')+'>&#8722;</button>';
  h+='<button class="btn green sm" onclick="chCountDelta(1)" '+(cfg.count>=4?'disabled':'')+'>+</button>';
  h+='</div>';
  for(var i=0;i<cfg.count;i++){
    var c=cfg.channels[i];
    h+='<div class="chrow">';
    h+='<div style="font-size:12px;color:var(--text2);margin-bottom:6px">'+(de()?'Kanal':'Channel')+' '+(i+1)+'</div>';
    h+='<div style="display:grid;grid-template-columns:1fr 1fr;gap:8px">';
    h+='<div><label>GPIO</label><input type="text" id="hwpin'+i+'" maxlength="2" value="'+c.pin+'"></div>';
    h+='<div><label>'+(de()?'Anzahl':'Count')+'</label><input type="text" id="hwcnt'+i+'" maxlength="3" value="'+c.count+'"></div>';
    h+='</div>';
    h+='<label class="checkbox-row" style="margin-top:8px"><input type="checkbox" '+(c.synced?'checked':'')+' onchange="toggleSync('+i+',this.checked)">'+(de()?'Synchronisiert':'Synced')+'</label>';
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
  var wswp = gid(p+'_wrap_swp');
  
  if(wc) wc.style.display = (e==1 || e==2 || e==3 || e==7 || e==8 || e==9) ? 'block' : 'none';
  if(wb) wb.style.display = (e>=1) ? 'block' : 'none';
  if(wkr) wkr.style.display = (e==2 || e==6 || e==7 || e==8 || e==9 || e==10) ? 'block' : 'none';
  if(wpol) wpol.style.display = (e>=3 && e<=5) ? 'block' : 'none';
  
  if(wswp) wswp.style.display = (e==4 || e==5) ? 'block' : 'none';
  
  var lblSpd = gid(p+'_lbl_spd'), lblWid = gid(p+'_lbl_wid');
  var rowWid = gid(p+'_row_wid');
  
  if(lblSpd && lblWid && rowWid) {
      rowWid.style.display = 'grid'; 
      lblWid.style.display = 'block'; 
      
      // Hinweis-Text Steuerung
      var hint = gid(p+'_hint');
      if(hint) {
          hint.style.display = (e == 2) ? 'block' : 'none';
          hint.innerText = 'Tipp: Für den richtigen KITT-Effekt stell die Breite auf 1/3 der LED-Länge.';
      }

      if(e==2)      { lblSpd.innerText = de()?'Tempo (Knight Rider)':'Speed';    lblWid.innerText = de()?'Breite (Knight Rider)':'Width'; }
      else if(e==6) { lblSpd.innerText = de()?'Regenbogen-Tempo':'Rainbow Speed'; lblWid.innerText = de()?'Farbdichte':'Color Density'; }
      else if(e==7) { lblSpd.innerText = de()?'Atem-Tempo':'Breathing Speed';    rowWid.style.display='none'; lblWid.style.display='none'; }
      else if(e==8) { lblSpd.innerText = de()?'Glitzer-Tempo':'Sparkle Speed';   lblWid.innerText = de()?'Sterne (Menge)':'Amount of Stars'; }
      else if(e==9) { lblSpd.innerText = de()?'Meteor-Tempo':'Meteor Speed';     lblWid.innerText = de()?'Schweif-Länge':'Trail Length'; }
      else if(e==10){ lblSpd.innerText = de()?'Knister-Tempo':'Flicker Speed';   lblWid.innerText = de()?'Feuer-Intensität':'Fire Intensity'; }
  }
}

function renderControls(){
  var out='';
  var synced=[];
  for(var i=0;i<cfg.count;i++) if(cfg.channels[i].synced) synced.push(i);
  if(synced.length>0){
    out+=buildBlock(-1,(de()?'Synchronisierte Kanäle':'Synced channels')+' ('+synced.length+')',cfg.channels[synced[0]]);
  }
  for(var i=0;i<cfg.count;i++){
    if(cfg.channels[i].synced) continue;
    out+=buildBlock(i,(de()?'Kanal':'Channel')+' '+(i+1),cfg.channels[i]);
  }
  gid('controls').innerHTML=out;

  if(synced.length>0) updateVis(-1);
  for(var i=0;i<cfg.count;i++) if(!cfg.channels[i].synced) updateVis(i);
}

function buildBlock(idx,title,s){
  var p=pfx(idx);
  var h='<div class="section"><h3>'+title+'</h3>';
  h+='<label>'+(de()?'Effekt':'Effect')+'</label>';
  h+='<select id="'+p+'_eff" onchange="onEff('+idx+')">';
  h+='<option value="0"'+(s.effect==0?' selected':'')+'>'+(de()?'Aus':'Off')+'</option>';
  h+='<option value="1"'+(s.effect==1?' selected':'')+'>'+(de()?'Feste Farbe':'Solid')+'</option>';
  h+='<option value="2"'+(s.effect==2?' selected':'')+'>Knight Rider</option>';
  h+='<option value="3"'+(s.effect==3?' selected':'')+'>'+(de()?'Blaulicht (Police EU)':'Police light (EU)')+'</option>';
  h+='<option value="4"'+(s.effect==4?' selected':'')+'>'+(de()?'US-Police (mit Weiß)':'US police (with white)')+'</option>';
  h+='<option value="5"'+(s.effect==5?' selected':'')+'>'+(de()?'US-Police (nur Rot/Blau)':'US police (red/blue only)')+'</option>';
  h+='<option value="6"'+(s.effect==6?' selected':'')+'>'+(de()?'Regenbogen-Welle':'Rainbow Wave')+'</option>';
  h+='<option value="7"'+(s.effect==7?' selected':'')+'>'+(de()?'Atmen / Pulsieren':'Breathing')+'</option>';
  h+='<option value="8"'+(s.effect==8?' selected':'')+'>'+(de()?'Glitzern (Sparkle)':'Sparkle')+'</option>';
  h+='<option value="9"'+(s.effect==9?' selected':'')+'>'+(de()?'Meteor Schauer':'Meteor Rain')+'</option>';
  h+='<option value="10"'+(s.effect==10?' selected':'')+'>'+(de()?'Höllenglut':'Hellfire Embers')+'</option>';
  h+='</select>';
  
  h+='<div id="'+p+'_wrap_col">';
  h+='<label style="margin-top:10px">'+(de()?'Farbe':'Color')+'</label>';
  h+='<input type="color" id="'+p+'_pick" value="'+rgbToHex(s.r,s.g,s.b)+'" oninput="onPick('+idx+')" style="width:100%;height:44px;border:1px solid var(--border2);border-radius:8px;background:var(--bg3);cursor:pointer;padding:2px">';
  h+='<div class="rng-row" style="margin-top:8px"><span>R</span><input type="range" id="'+p+'_r" min="0" max="255" value="'+s.r+'" oninput="onCol('+idx+')"><div class="rng-val" id="'+p+'_rv">'+s.r+'</div></div>';
  h+='<div class="rng-row"><span>G</span><input type="range" id="'+p+'_g" min="0" max="255" value="'+s.g+'" oninput="onCol('+idx+')"><div class="rng-val" id="'+p+'_gv">'+s.g+'</div></div>';
  h+='<div class="rng-row"><span>B</span><input type="range" id="'+p+'_b" min="0" max="255" value="'+s.b+'" oninput="onCol('+idx+')"><div class="rng-val" id="'+p+'_bv">'+s.b+'</div></div>';
  h+='</div>';

  h+='<div id="'+p+'_wrap_bri">';
  h+='<label style="margin-top:10px">'+(de()?'Helligkeit':'Brightness')+'</label>';
  h+='<div class="rng-row"><span>&#9788;</span><input type="range" id="'+p+'_br" min="0" max="255" value="'+s.bright+'" oninput="onBri('+idx+')"><div class="rng-val" id="'+p+'_brv">'+s.bright+'</div></div>';
  h+='</div>';

  h+='<div id="'+p+'_wrap_kr">';
  h+='<label id="'+p+'_lbl_spd" style="margin-top:10px">'+(de()?'Tempo (Animation)':'Animation Speed')+'</label>';
  h+='<div class="rng-row"><span>&#9201;</span><input type="range" id="'+p+'_spd" min="1" max="200" value="'+s.krspeed+'" oninput="onSpd('+idx+')"><div class="rng-val" id="'+p+'_spdv">'+s.krspeed+'</div></div>';
  h+='<label id="'+p+'_lbl_wid" style="margin-top:10px">'+(de()?'Breite / Menge':'Width / Amount')+'</label>';
  h+='<div class="rng-row" id="'+p+'_row_wid"><span>&#9646;</span><input type="range" id="'+p+'_wid" min="1" max="50" value="'+s.krwidth+'" oninput="onWid('+idx+')"><div class="rng-val" id="'+p+'_widv">'+s.krwidth+'</div></div>';
  h+='<div id="'+p+'_hint" style="margin-top:6px; color:var(--accent); font-size:11px; font-weight:bold; display:none;">Tipp: Für den richtigen KITT-Effekt stell die Breite auf 1/3 der LED-Länge.</div>';
  h+='</div>';

  h+='<div id="'+p+'_wrap_pol">';
  h+='<label style="margin-top:10px">'+(de()?'Blaulicht-Frequenz (Hz)':'Police frequency (Hz)')+'</label>';
  h+='<div class="rng-row"><span>Hz</span><input type="range" id="'+p+'_phz" min="1" max="10" value="'+(s.polhz||4)+'" oninput="onPolHz('+idx+')"><div class="rng-val" id="'+p+'_phzv">'+(s.polhz||4)+'</div></div>';
  h+='<label style="margin-top:10px">'+(de()?'Blitze pro Burst':'Flashes per burst')+'</label>';
  h+='<div class="rng-row"><span>&#9646;</span><input type="range" id="'+p+'_pwid" min="1" max="10" value="'+s.krwidth+'" oninput="onPWid('+idx+')"><div class="rng-val" id="'+p+'_pwidv">'+s.krwidth+'</div></div>';
  h+='<div id="'+p+'_wrap_swp" style="margin-top:10px;">';
  h+='<label class="checkbox-row"><input type="checkbox" id="'+p+'_swp" '+(s.swapcolors?'checked':'')+' onchange="onSwp('+idx+',this.checked)">'+(de()?'Seiten tauschen (Rot/Blau)':'Swap sides (Red/Blue)')+'</label>';
  h+='</div>';
  h+='</div>';
  
  h+='</div>';
  return h;
}

function applyLocal(idx,fn){
  if(idx<0){for(var i=0;i<cfg.count;i++) if(cfg.channels[i].synced) fn(cfg.channels[i]);}
  else if(cfg.channels[idx]) fn(cfg.channels[idx]);
}

function onEff(idx){
  var p=pfx(idx),e=+gv(p+'_eff');
  updateVis(idx); 
  send('/api/led/effect?'+tgt(idx)+'&e='+e);
  applyLocal(idx,function(c){c.effect=e;});
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
  var p=pfx(idx), v=checked?1:0;
  debSend('/api/led/swapcol?'+tgt(idx)+'&v='+v);
  applyLocal(idx,function(c){c.swapcolors=checked;});
}

function toggleSync(i,on){
  cfg.channels[i].synced=on;
  send('/api/led/sync?ch='+i+'&on='+(on?1:0));
  renderControls();
}

function chCountDelta(d){
  var n=cfg.count+d; if(n<1)n=1; if(n>4)n=4;
  if(n===cfg.count)return;
  fetch('/api/led/channels?n='+n,{method:'POST'}).then(function(){ load(); });
}

function applyHw(){
  var qs=[];
  for(var i=0;i<cfg.count;i++){
    var pin=parseInt(gv('hwpin'+i))||4;
    var cnt=parseInt(gv('hwcnt'+i))||30;
    qs.push('p'+i+'='+pin+'&n'+i+'='+cnt);
  }
  var msg=gid('hwmsg');
  fetch('/api/led/hw?'+qs.join('&'),{method:'POST'}).then(function(r){
    if(r.ok){msg.textContent=de()?'Übernommen':'Applied';msg.className='msg ok';}
    else{msg.textContent='Error';msg.className='msg err';}
    setTimeout(function(){msg.className='msg';},2000);
    load();
  }).catch(function(){msg.textContent='Error';msg.className='msg err';});
}

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
  for (int i = 0; i < channelCount; i++) { initStripFor(i); applyChannel(i); }
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
    String j = "{\"count\":" + String(channelCount) + ",\"channels\":[";
    for (int i = 0; i < LED_MAX_CHANNELS; i++) {
      if (i) j += ",";
      j += "{\"pin\":"       + String(ch[i].pin);
      j += ",\"count\":"     + String(ch[i].count);
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
      if (n > old) for (int i = old; i < n; i++) { initStripFor(i); applyChannel(i); }
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

  ledServer->on("/api/led/color", HTTP_POST, [](){
    int tg[LED_MAX_CHANNELS]; int n = resolveTargets(tg);
    for (int k = 0; k < n; k++) {
      int i = tg[k];
      if (ledServer->hasArg("r")) ch[i].r = ledServer->arg("r").toInt();
      if (ledServer->hasArg("g")) ch[i].g = ledServer->arg("g").toInt();
      if (ledServer->hasArg("b")) ch[i].b = ledServer->arg("b").toInt();
      clampChannel(i);
      if (ch[i].effect == 1) applyChannel(i); 
      else if (ch[i].effect >= 3 && ch[i].effect <= 5) ch[i].polForce = true;
    }
    ledsRequestSave();   
    ledServer->send(200, "text/plain", "OK");
  });

  ledServer->on("/api/led/bright", HTTP_POST, [](){
    int tg[LED_MAX_CHANNELS]; int n = resolveTargets(tg);
    for (int k = 0; k < n; k++) {
      int i = tg[k];
      if (ledServer->hasArg("v")) ch[i].bright = ledServer->arg("v").toInt();
      clampChannel(i);
      if (ch[i].strip) ch[i].strip->setBrightness(ch[i].bright);
      if (ch[i].effect == 1) applyChannel(i);
      else if (ch[i].effect >= 3 && ch[i].effect <= 5) ch[i].polForce = true;
    }
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
    for (int k = 0; k < n; k++) {
      int i = tg[k];
      if (ledServer->hasArg("e")) ch[i].effect = ledServer->arg("e").toInt();
      clampChannel(i);
      applyChannel(i);
    }
    ledsSaveConfig();
    ledServer->send(200, "text/plain", "OK");
  });

  ledServer->on("/api/led/hw", HTTP_POST, [](){
    for (int i = 0; i < channelCount; i++) {
      String pk = "p" + String(i), nk = "n" + String(i);
      if (ledServer->hasArg(pk.c_str())) ch[i].pin   = ledServer->arg(pk.c_str()).toInt();
      if (ledServer->hasArg(nk.c_str())) ch[i].count = ledServer->arg(nk.c_str()).toInt();
    }
    clampAll();
    ledsSaveConfig();
    ledsLock();   
    for (int i = 0; i < channelCount; i++) { initStripFor(i); applyChannel(i); }
    ledsUnlock();
    ledServer->send(200, "text/plain", "OK");
  });

  Serial.println("LEDs: multi-channel /leds page + API registered");
}