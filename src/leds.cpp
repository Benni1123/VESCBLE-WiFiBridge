#include "leds.h"
#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// ── Mehrkanal-LED-Steuerung (bis zu 4 Kanaele) ────────────────────────────────
// Jeder Kanal hat eigene Hardware (Pin, Anzahl) und eigene Einstellungen
// (Effekt, Farbe, Helligkeit, KR-Tempo) plus einen "synced"-Haken. Synchron
// geschaltete Kanaele werden ueber einen gemeinsamen Block gesteuert (eine
// Aenderung wird auf alle synced Kanaele verteilt), nicht-synchrone einzeln.
// Eigener Preferences-Namespace "leds" (getrennt von "vesccfg").

#define LED_MAX_CHANNELS 4

struct LedChannel {
  int  pin     = 4;     // Daten-GPIO
  int  count   = 30;    // Anzahl LEDs
  bool synced  = false; // gehoert zur Sync-Gruppe
  int  effect  = 0;     // 0=Aus, 1=Solid RGB, 2=Knight Rider
  int  r       = 0;
  int  g       = 0;
  int  b       = 255;
  int  bright  = 128;
  int  krSpeed = 30;    // ms pro Schritt (kleiner = schneller)
  int  krWidth = 3;     // Anzahl gleichzeitig leuchtender LEDs (Punkt-/Schweifbreite)
  Adafruit_NeoPixel *strip = nullptr;
  // Knight-Rider Animationszustand
  int  krPos      = 0;
  int  krDir      = 1;
  unsigned long krLastStep = 0;
};

static LedChannel ch[LED_MAX_CHANNELS];
static int        channelCount = 1;   // aktive Kanaele (1..4)

// ── LED-Task auf Kern 1 ───────────────────────────────────────────────────────
// Das LED-Rendering laeuft in einem EIGENEN FreeRTOS-Task (Kern 1), damit es
// unabhaengig vom Haupt-loop() laeuft: haengt der Loop mal, laufen die LEDs
// weiter -- und ein langsames show() bremst umgekehrt den Loop nicht mehr.
// BEWUSST Kern 1 (nicht 0): Adafruit_NeoPixel show() sperrt fuer ~4,3ms die
// Interrupts; auf Kern 0 (Funk-Stack) wuerde das WLAN/BLE stoeren.
// Ein Mutex serialisiert Task-Rendering gegen die Web-Handler, die Strips neu
// anlegen/loeschen koennen (sonst Use-after-free waehrend show()).
static SemaphoreHandle_t ledsMutex      = nullptr;
static TaskHandle_t      ledsTaskHandle = nullptr;
static volatile bool     ledsEnabled    = false;
static volatile int32_t  ledsLatestErpm = 0;

static inline void ledsLock()   { if (ledsMutex) xSemaphoreTake(ledsMutex, portMAX_DELAY); }
static inline void ledsUnlock() { if (ledsMutex) xSemaphoreGive(ledsMutex); }

static Preferences ledPrefs;
static WebServer  *ledServer = nullptr;

// Grenzen
static const int PIN_MIN = 0, PIN_MAX = 48;
static const int CNT_MIN = 1, CNT_MAX = 300;

// ── Clamp ─────────────────────────────────────────────────────────────────────
static void clampChannel(int i) {
  LedChannel &c = ch[i];
  if (c.pin   < PIN_MIN) c.pin = PIN_MIN;  if (c.pin   > PIN_MAX) c.pin = PIN_MAX;
  if (c.count < CNT_MIN) c.count = CNT_MIN; if (c.count > CNT_MAX) c.count = CNT_MAX;
  if (c.effect < 0 || c.effect > 2) c.effect = 0;
  if (c.r < 0) c.r = 0; if (c.r > 255) c.r = 255;
  if (c.g < 0) c.g = 0; if (c.g > 255) c.g = 255;
  if (c.b < 0) c.b = 0; if (c.b > 255) c.b = 255;
  if (c.bright < 0) c.bright = 0; if (c.bright > 255) c.bright = 255;
  if (c.krSpeed < 5) c.krSpeed = 5; if (c.krSpeed > 500) c.krSpeed = 500;
  if (c.krWidth < 1) c.krWidth = 1; if (c.krWidth > 50) c.krWidth = 50;
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
    ch[i].pin     = ledPrefs.getInt ((p + "pin").c_str(), 4);
    ch[i].count   = ledPrefs.getInt ((p + "cnt").c_str(), 30);
    ch[i].effect  = ledPrefs.getInt ((p + "eff").c_str(), 0);
    ch[i].r       = ledPrefs.getInt ((p + "r").c_str(),   0);
    ch[i].g       = ledPrefs.getInt ((p + "g").c_str(),   0);
    ch[i].b       = ledPrefs.getInt ((p + "b").c_str(),   255);
    ch[i].bright  = ledPrefs.getInt ((p + "br").c_str(),  128);
    ch[i].krSpeed = ledPrefs.getInt ((p + "spd").c_str(), 30);
    ch[i].krWidth = ledPrefs.getInt ((p + "wid").c_str(), 3);
    ch[i].synced  = ledPrefs.getBool((p + "syn").c_str(), false);
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
    ledPrefs.putBool((p + "syn").c_str(), ch[i].synced);
  }
  ledPrefs.end();
}

// ── Verzoegertes (debounced) Speichern ────────────────────────────────────────
// Live-Endpoints (Farbe/Helligkeit/KR-Speed/KR-Width) feuern beim Schieben in
// der App sehr schnell hintereinander. Wuerde jeder Request sofort ins NVS
// (Flash) schreiben, staut sich der Webserver -> hakeliges/springendes Verhalten
// und unnoetiger Flash-Verschleiss. Stattdessen merken wir uns nur "muss noch
// gespeichert werden" + Zeitstempel; ledsLoop() schreibt erst, wenn das Schieben
// kurz geruht hat (LED_SAVE_DEBOUNCE_MS).
static const unsigned long LED_SAVE_DEBOUNCE_MS = 1500;
static bool          ledSavePending = false;
static unsigned long ledSaveLastReq = 0;

// ── Gedrosseltes Zeichnen (gegen Hakeln beim Farbe-Schieben) ─────────────────
// Ein NeoPixel show() fuer z.B. 144 LEDs dauert ~4,3 ms und sperrt dabei die
// Interrupts (Bitbanging) -> blockiert WiFi/Webserver. Bei schnellem Schieben
// am Farbrad wuerde jeder einzelne POST so ein show() ausloesen und den Stack
// immer wieder kurz einfrieren. Loesung: POSTs setzen nur die Pixel + markieren
// den Kanal "dirty"; ledsLoop() macht hoechstens alle LED_FRAME_MS EIN show()
// und buendelt damit viele schnelle Aenderungen zu einem einzigen Frame.
static const unsigned long LED_FRAME_MS = 25;     // max. ~40 FPS Ausgabe
static bool          ledDirty[LED_MAX_CHANNELS] = { false };
static unsigned long ledLastShow = 0;

// Markiert einen Kanal als "muss neu gezeichnet werden".
static void markDirty(int i) { if (i >= 0 && i < LED_MAX_CHANNELS) ledDirty[i] = true; }

// Live-Wert wurde geaendert -> Speichern nur vormerken (nicht sofort schreiben).
static void ledsRequestSave() {
  ledSavePending = true;
  ledSaveLastReq = millis();
}

// Von ledsLoop() aufgerufen: schreibt das NVS, sobald lange genug Ruhe war.
static void ledsFlushPendingSave() {
  if (ledSavePending && (millis() - ledSaveLastReq >= LED_SAVE_DEBOUNCE_MS)) {
    ledSavePending = false;
    ledsSaveConfig();
  }
}

// ── Strip eines Kanals (neu) initialisieren ───────────────────────────────────
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
  Serial.printf("LEDs ch%d: init pin=%d count=%d\n", i, c.pin, c.count);
}

// ── Statischen Effekt eines Kanals anwenden (live) ────────────────────────────
// Setzt nur den Pixel-Buffer + Helligkeit und markiert den Kanal dirty.
// Das eigentliche show() macht gebuendelt ledsShowDirty() aus ledsLoop().
// Ausnahme effect==0 (Aus): sofort zeichnen, da einmalig und sofort sichtbar.
static void applyChannel(int i) {
  LedChannel &c = ch[i];
  if (!c.strip) return;
  c.strip->setBrightness(c.bright);
  if (c.effect == 0) {
    c.strip->clear(); c.strip->show();
  } else if (c.effect == 1) {
    uint32_t col = c.strip->Color(c.r, c.g, c.b);
    for (int p = 0; p < c.count; p++) c.strip->setPixelColor(p, col);
    markDirty(i);   // show() gebuendelt in ledsLoop()
  } else if (c.effect == 2) {
    c.strip->clear(); markDirty(i);   // Startbild; Animation in ledsLoop()
  }
}

// ── Knight Rider eines Kanals (non-blocking) ──────────────────────────────────
static void knightRiderChannel(int i) {
  LedChannel &c = ch[i];
  if (!c.strip) return;
  unsigned long now = millis();
  if (now - c.krLastStep < (unsigned long)c.krSpeed) return;
  c.krLastStep = now;

  c.strip->clear();
  // Breite = Anzahl gleichzeitig leuchtender LEDs, aber nie mehr als der Strip
  // hat. Der leuchtende Block wandert hin und her.
  int width = c.krWidth;
  if (width > c.count) width = c.count;
  if (width < 1) width = 1;
  uint32_t col = c.strip->Color(c.r, c.g, c.b);
  for (int w = 0; w < width; w++) {
    int p = c.krPos + w;   // Block ab krPos, width LEDs lang
    if (p >= 0 && p < c.count) c.strip->setPixelColor(p, col);
  }
  c.strip->show();

  // Bewegung: der Block laeuft so, dass sein vorderer Rand bis ans Ende kommt
  // und dann zurueck. Endpunkte unter Beruecksichtigung der Blockbreite.
  int maxPos = c.count - width;   // letzte gueltige Startposition
  if (maxPos < 0) maxPos = 0;
  c.krPos += c.krDir;
  if (c.krPos >= maxPos) { c.krPos = maxPos; c.krDir = -1; }
  else if (c.krPos <= 0) { c.krPos = 0;      c.krDir =  1; }
}

// Zeichnet dirty markierte Kanaele gebuendelt aus, aber hoechstens alle
// LED_FRAME_MS. So werden viele schnell eintreffende Farb-POSTs zu einem
// einzigen show() zusammengefasst -> kein wiederholtes Interrupt-Sperren.
static void ledsShowDirty() {
  if (millis() - ledLastShow < LED_FRAME_MS) return;
  bool any = false;
  for (int i = 0; i < channelCount; i++) {
    if (ledDirty[i] && ch[i].strip) {
      ch[i].strip->show();
      ledDirty[i] = false;
      any = true;
    }
  }
  if (any) ledLastShow = millis();
}

void ledsLoop(int32_t erpm) {
  (void)erpm;   // spaeter fuer Speed-Effekte
  for (int i = 0; i < channelCount; i++) {
    if (ch[i].effect == 2) knightRiderChannel(i);
  }
  ledsShowDirty();          // gebuendeltes, gedrosseltes Zeichnen (Solid-Farbe)
  ledsFlushPendingSave();   // verzoegertes NVS-Speichern nach Slider-Aktivitaet
}

// Vom Haupt-loop() aufgerufen: aktualisiert nur die geteilten Werte (billig).
// Das eigentliche Rendering macht der LED-Task.
void ledsUpdateState(bool enabled, int32_t erpm) {
  ledsEnabled    = enabled;
  ledsLatestErpm = erpm;
}

// Der LED-Task: rendert unter Mutex, unabhaengig vom Haupt-loop().
static void ledsTaskFn(void *) {
  for (;;) {
    if (ledsEnabled) {
      ledsLock();
      ledsLoop(ledsLatestErpm);
      ledsUnlock();
    }
    vTaskDelay(pdMS_TO_TICKS(5));   // ~ Frame-Granularitaet, gibt CPU frei
  }
}

// Startet den LED-Task auf Kern 1 (einmalig). Nach ledsSetup() aufrufen.
void ledsStartTask() {
  if (ledsTaskHandle) return;   // schon gestartet
  xTaskCreatePinnedToCore(ledsTaskFn, "ledsTask", 4096, nullptr, 1,
                          &ledsTaskHandle, 1 /* Kern 1 */);
}

// Schaltet alle Kanaele hart aus. Vor Neustart/OTA aufrufen, damit die LEDs
// nicht im letzten Frame haengen bleiben. Setzt auch effect=0 im RAM, damit
// ein evtl. noch laufender ledsLoop() nichts mehr zeichnet.
void ledsOff() {
  // Ausstehende (debounced) Speicherung noch schnell schreiben, sonst gingen
  // gerade geschobene Werte beim folgenden Reboot/OTA verloren.
  if (ledSavePending) { ledSavePending = false; ledsSaveConfig(); }
  ledsLock();   // kein paralleles show()/Zugriff vom Task waehrend Abschalten
  for (int i = 0; i < LED_MAX_CHANNELS; i++) {
    ch[i].effect = 0;
    if (ch[i].strip) {
      ch[i].strip->clear();
      ch[i].strip->show();
    }
  }
  ledsUnlock();
}

// ── Zielkanaele aufloesen ─────────────────────────────────────────────────────
// sync=1 -> alle synchronisierten Kanaele; ch=N -> nur Kanal N.
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

// ── /leds Seite (Mehrkanal, dynamisch per JS aufgebaut) ───────────────────────
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

// ── Kopf: Theme, Sprache, IP-Status (wie Hauptseite) ──
var theme=(document.cookie.match(/theme=([a-z]+)/)||[])[1]||(window.matchMedia('(prefers-color-scheme:dark)').matches?'dark':'light');
function applyTheme(){document.documentElement.setAttribute('data-theme',theme);var b=gid('themeBtn');if(b)b.textContent=theme==='dark'?'\u2600\uFE0F':'\uD83C\uDF19';document.cookie='theme='+theme+';path=/;max-age=31536000';}
function toggleTheme(){theme=theme==='dark'?'light':'dark';applyTheme();}
applyTheme();
function toggleLang(){lang=lang==='de'?'en':'de';document.cookie='lang='+lang+';path=/;max-age=31536000';location.reload();}
gid('langBtn').textContent=de()?'EN':'DE';
// Statische Beschriftungen uebersetzen (die dynamischen Bloecke sind schon de/en)
(function(){
  var s=function(id,en,d){var el=gid(id);if(el)el.textContent=de()?d:en;};
  s('lbl-hw','Channels','Kanäle');
  s('btn-hw','Apply hardware','Hardware übernehmen');
  var sb=gid('statusBar'); if(sb && sb.textContent==='Loading...') sb.textContent=de()?'Lädt...':'Loading...';
})();
// IP/Status in die statusBar laden (gleiche Quelle wie Hauptseite)
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

// Debounce fuer Slider
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
}

function buildBlock(idx,title,s){
  var p=pfx(idx);
  var h='<div class="section"><h3>'+title+'</h3>';
  h+='<label>'+(de()?'Effekt':'Effect')+'</label>';
  h+='<select id="'+p+'_eff" onchange="onEff('+idx+')">';
  h+='<option value="0"'+(s.effect==0?' selected':'')+'>'+(de()?'Aus':'Off')+'</option>';
  h+='<option value="1"'+(s.effect==1?' selected':'')+'>'+(de()?'Feste Farbe':'Solid')+'</option>';
  h+='<option value="2"'+(s.effect==2?' selected':'')+'>Knight Rider</option>';
  h+='</select>';
  h+='<label style="margin-top:10px">'+(de()?'Farbe':'Color')+'</label>';
  h+='<input type="color" id="'+p+'_pick" value="'+rgbToHex(s.r,s.g,s.b)+'" oninput="onPick('+idx+')" style="width:100%;height:44px;border:1px solid var(--border2);border-radius:8px;background:var(--bg3);cursor:pointer;padding:2px">';
  h+='<div class="rng-row" style="margin-top:8px"><span>R</span><input type="range" id="'+p+'_r" min="0" max="255" value="'+s.r+'" oninput="onCol('+idx+')"><div class="rng-val" id="'+p+'_rv">'+s.r+'</div></div>';
  h+='<div class="rng-row"><span>G</span><input type="range" id="'+p+'_g" min="0" max="255" value="'+s.g+'" oninput="onCol('+idx+')"><div class="rng-val" id="'+p+'_gv">'+s.g+'</div></div>';
  h+='<div class="rng-row"><span>B</span><input type="range" id="'+p+'_b" min="0" max="255" value="'+s.b+'" oninput="onCol('+idx+')"><div class="rng-val" id="'+p+'_bv">'+s.b+'</div></div>';
  h+='<label style="margin-top:10px">'+(de()?'Helligkeit':'Brightness')+'</label>';
  h+='<div class="rng-row"><span>&#9788;</span><input type="range" id="'+p+'_br" min="0" max="255" value="'+s.bright+'" oninput="onBri('+idx+')"><div class="rng-val" id="'+p+'_brv">'+s.bright+'</div></div>';
  h+='<label style="margin-top:10px">'+(de()?'Knight Rider Geschw.':'Knight Rider speed')+'</label>';
  h+='<div class="rng-row"><span>&#9201;</span><input type="range" id="'+p+'_spd" min="5" max="200" value="'+s.krspeed+'" oninput="onSpd('+idx+')"><div class="rng-val" id="'+p+'_spdv">'+s.krspeed+'</div></div>';
  h+='<label style="margin-top:10px">'+(de()?'Knight Rider Breite (LEDs gleichzeitig)':'Knight Rider width (LEDs at once)')+'</label>';
  h+='<div class="rng-row"><span>&#9646;</span><input type="range" id="'+p+'_wid" min="1" max="20" value="'+s.krwidth+'" oninput="onWid('+idx+')"><div class="rng-val" id="'+p+'_widv">'+s.krwidth+'</div></div>';
  h+='</div>';
  return h;
}

function applyLocal(idx,fn){
  if(idx<0){for(var i=0;i<cfg.count;i++) if(cfg.channels[i].synced) fn(cfg.channels[i]);}
  else if(cfg.channels[idx]) fn(cfg.channels[idx]);
}

function onEff(idx){
  var p=pfx(idx),e=+gv(p+'_eff');
  debSend('/api/led/effect?'+tgt(idx)+'&e='+e);
  applyLocal(idx,function(c){c.effect=e;});
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
  debSend('/api/led/krwidth?'+tgt(idx)+'&v='+v);
  applyLocal(idx,function(c){c.krwidth=v;});
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

// ── Setup: Seite + API-Endpoints ──────────────────────────────────────────────
void ledsSetup(WebServer *server) {
  ledServer = server;
  if (!ledServer) return;

  // Mutex fuer die Serialisierung Task <-> Web-Handler zuerst anlegen.
  if (!ledsMutex) ledsMutex = xSemaphoreCreateMutex();

  ledsLoadConfig();
  // WICHTIG: Nach jedem Neustart sollen die LEDs AUS sein, nie der letzte
  // Effekt. Deshalb effect aller Kanaele auf 0 (Aus) zwingen und die Strips
  // initialisieren + leeren. Der gespeicherte Effekt-Wert wird ueberschrieben
  // (bewusst: frischer Start = alles aus).
  for (int i = 0; i < LED_MAX_CHANNELS; i++) ch[i].effect = 0;
  for (int i = 0; i < channelCount; i++) { initStripFor(i); applyChannel(i); }

  // Seite
  ledServer->on("/leds", HTTP_GET, [](){
    ledServer->send(200, "text/html", LEDS_PAGE_HTML);
  });

  // Komplette Config als JSON (alle 4 Kanaele; count = aktive)
  ledServer->on("/api/led/config", HTTP_GET, [](){
    String j = "{\"count\":" + String(channelCount) + ",\"channels\":[";
    for (int i = 0; i < LED_MAX_CHANNELS; i++) {
      if (i) j += ",";
      j += "{\"pin\":"      + String(ch[i].pin);
      j += ",\"count\":"   + String(ch[i].count);
      j += ",\"synced\":"  + String(ch[i].synced ? "true" : "false");
      j += ",\"effect\":"  + String(ch[i].effect);
      j += ",\"r\":"       + String(ch[i].r);
      j += ",\"g\":"       + String(ch[i].g);
      j += ",\"b\":"       + String(ch[i].b);
      j += ",\"bright\":"  + String(ch[i].bright);
      j += ",\"krspeed\":" + String(ch[i].krSpeed);
      j += ",\"krwidth\":" + String(ch[i].krWidth);
      j += "}";
    }
    j += "]}";
    ledServer->send(200, "application/json", j);
  });

  // Anzahl aktiver Kanaele aendern (1..4) -> neue Kanaele initialisieren,
  // entfernte freigeben.
  ledServer->on("/api/led/channels", HTTP_POST, [](){
    if (ledServer->hasArg("n")) {
      int n = ledServer->arg("n").toInt();
      if (n < 1) n = 1; if (n > LED_MAX_CHANNELS) n = LED_MAX_CHANNELS;
      ledsLock();   // gegen Task-show() waehrend delete/realloc schuetzen
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

  // Sync-Haken eines Kanals setzen
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

  // Farbe (live) — fuer ch=N oder alle synced (sync=1)
  ledServer->on("/api/led/color", HTTP_POST, [](){
    int tg[LED_MAX_CHANNELS]; int n = resolveTargets(tg);
    for (int k = 0; k < n; k++) {
      int i = tg[k];
      if (ledServer->hasArg("r")) ch[i].r = ledServer->arg("r").toInt();
      if (ledServer->hasArg("g")) ch[i].g = ledServer->arg("g").toInt();
      if (ledServer->hasArg("b")) ch[i].b = ledServer->arg("b").toInt();
      clampChannel(i);
      if (ch[i].effect == 1) applyChannel(i);
    }
    ledsRequestSave();   // debounced: erst nach Schiebe-Ruhe ins NVS
    ledServer->send(200, "text/plain", "OK");
  });

  // Helligkeit (live)
  ledServer->on("/api/led/bright", HTTP_POST, [](){
    int tg[LED_MAX_CHANNELS]; int n = resolveTargets(tg);
    for (int k = 0; k < n; k++) {
      int i = tg[k];
      if (ledServer->hasArg("v")) ch[i].bright = ledServer->arg("v").toInt();
      clampChannel(i);
      if (ch[i].strip) ch[i].strip->setBrightness(ch[i].bright);
      applyChannel(i);
    }
    ledsRequestSave();   // debounced
    ledServer->send(200, "text/plain", "OK");
  });

  // Knight-Rider Geschwindigkeit (live)
  ledServer->on("/api/led/krspeed", HTTP_POST, [](){
    int tg[LED_MAX_CHANNELS]; int n = resolveTargets(tg);
    for (int k = 0; k < n; k++) {
      int i = tg[k];
      if (ledServer->hasArg("v")) ch[i].krSpeed = ledServer->arg("v").toInt();
      clampChannel(i);
    }
    ledsRequestSave();   // debounced
    ledServer->send(200, "text/plain", "OK");
  });

  // Knight-Rider Breite (Anzahl gleichzeitig leuchtender LEDs, live)
  ledServer->on("/api/led/krwidth", HTTP_POST, [](){
    int tg[LED_MAX_CHANNELS]; int n = resolveTargets(tg);
    for (int k = 0; k < n; k++) {
      int i = tg[k];
      if (ledServer->hasArg("v")) ch[i].krWidth = ledServer->arg("v").toInt();
      clampChannel(i);
    }
    ledsRequestSave();   // debounced
    ledServer->send(200, "text/plain", "OK");
  });

  // Effekt (live)
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

  // Hardware aller Kanaele (Pin/Anzahl) -> Strips neu initialisieren
  ledServer->on("/api/led/hw", HTTP_POST, [](){
    for (int i = 0; i < channelCount; i++) {
      String pk = "p" + String(i), nk = "n" + String(i);
      if (ledServer->hasArg(pk.c_str())) ch[i].pin   = ledServer->arg(pk.c_str()).toInt();
      if (ledServer->hasArg(nk.c_str())) ch[i].count = ledServer->arg(nk.c_str()).toInt();
    }
    clampAll();
    ledsSaveConfig();
    ledsLock();   // gegen Task-show() waehrend Strip-Reinit schuetzen
    for (int i = 0; i < channelCount; i++) { initStripFor(i); applyChannel(i); }
    ledsUnlock();
    ledServer->send(200, "text/plain", "OK");
  });

  Serial.println("LEDs: multi-channel /leds page + API registered");
}