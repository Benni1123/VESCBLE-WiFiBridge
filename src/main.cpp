#include <Arduino.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <WiFiAP.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Update.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include "version.h"

#define DEFAULT_BLE_NAME  "VESC-BLE-WiFi"
#define DEFAULT_AP_SSID   "VESC-BLE-WiFi"
#define DEFAULT_AP_PASS   ""
#define DEFAULT_HOSTNAME  "vesc-ble-wifi"
#define DEFAULT_UPDATE_URL  "https://github.com/Benni1123/VESCBLE-WiFiBridge/releases/latest/download/firmware.bin"
#define DEFAULT_VERSION_URL "https://github.com/Benni1123/VESCBLE-WiFiBridge/releases/latest/download/version.txt"
#define VESC_RX_PIN       6
#define VESC_TX_PIN       5
#define VESC_TCP_PORT     65101
#define MAX_WIFI_NETWORKS 10

#define VESC_SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define VESC_CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define VESC_CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

Preferences prefs;

String cfg_ble_name;
String cfg_ap_ssid;
String cfg_ap_pass;
String cfg_hostname;
int    cfg_port;
bool   cfg_vesc_poll;
String cfg_update_url;
String cfg_version_url;
int    cfg_rx_pin;
int    cfg_tx_pin;

struct WiFiEntry {
  String ssid;
  String pass;
  bool   staticIp = false;
  String ip;
  String gateway;
  String subnet;
  String dns;
};
std::vector<WiFiEntry> cfg_wifi;

void loadConfig() {
  prefs.begin("vesccfg", false);
  cfg_ble_name = prefs.getString("ble_name", DEFAULT_BLE_NAME);
  cfg_ap_ssid  = prefs.getString("ap_ssid",  DEFAULT_AP_SSID);
  cfg_ap_pass  = prefs.getString("ap_pass",  DEFAULT_AP_PASS);
  cfg_hostname = prefs.getString("hostname", DEFAULT_HOSTNAME);
  cfg_port        = prefs.getInt("port", VESC_TCP_PORT);
  cfg_vesc_poll   = prefs.getBool("vesc_poll", true);
  cfg_update_url  = prefs.getString("update_url", DEFAULT_UPDATE_URL);
  cfg_version_url = prefs.getString("version_url", DEFAULT_VERSION_URL);
  cfg_rx_pin      = prefs.getInt("rx_pin", VESC_RX_PIN);
  cfg_tx_pin      = prefs.getInt("tx_pin", VESC_TX_PIN);
  int count    = prefs.getInt("wifi_count", 0);
  cfg_wifi.clear();
  for (int i = 0; i < count && i < MAX_WIFI_NETWORKS; i++) {
    WiFiEntry e;
    e.ssid     = prefs.getString(("wssid" + String(i)).c_str(), "");
    e.pass     = prefs.getString(("wpass" + String(i)).c_str(), "");
    e.staticIp = prefs.getBool(("wstatic" + String(i)).c_str(), false);
    e.ip       = prefs.getString(("wip"  + String(i)).c_str(), "");
    e.gateway  = prefs.getString(("wgw"  + String(i)).c_str(), "");
    e.subnet   = prefs.getString(("wsub" + String(i)).c_str(), "255.255.255.0");
    e.dns      = prefs.getString(("wdns" + String(i)).c_str(), "");
    if (e.ssid.length() > 0) cfg_wifi.push_back(e);
  }
  prefs.end();
  if (cfg_ble_name.isEmpty()) cfg_ble_name = DEFAULT_BLE_NAME;
  if (cfg_ap_ssid.isEmpty())  cfg_ap_ssid  = DEFAULT_AP_SSID;
  if (cfg_hostname.isEmpty()) cfg_hostname = DEFAULT_HOSTNAME;
  if (cfg_port <= 0 || cfg_port > 65535) cfg_port = VESC_TCP_PORT;
  if (cfg_rx_pin < 0 || cfg_rx_pin > 48) cfg_rx_pin = VESC_RX_PIN;
  if (cfg_tx_pin < 0 || cfg_tx_pin > 48) cfg_tx_pin = VESC_TX_PIN;
}

void saveConfig() {
  prefs.begin("vesccfg", false);
  prefs.putString("ble_name", cfg_ble_name);
  prefs.putString("ap_ssid",  cfg_ap_ssid);
  prefs.putString("ap_pass",  cfg_ap_pass);
  prefs.putString("hostname", cfg_hostname);
  prefs.putInt("port", cfg_port);
  prefs.putBool("vesc_poll", cfg_vesc_poll);
  prefs.putString("update_url",  cfg_update_url);
  prefs.putString("version_url", cfg_version_url);
  prefs.putInt("rx_pin", cfg_rx_pin);
  prefs.putInt("tx_pin", cfg_tx_pin);
  prefs.putInt("wifi_count", cfg_wifi.size());
  for (int i = 0; i < (int)cfg_wifi.size(); i++) {
    prefs.putString(("wssid"   + String(i)).c_str(), cfg_wifi[i].ssid);
    prefs.putString(("wpass"   + String(i)).c_str(), cfg_wifi[i].pass);
    prefs.putBool(  ("wstatic" + String(i)).c_str(), cfg_wifi[i].staticIp);
    prefs.putString(("wip"     + String(i)).c_str(), cfg_wifi[i].ip);
    prefs.putString(("wgw"     + String(i)).c_str(), cfg_wifi[i].gateway);
    prefs.putString(("wsub"    + String(i)).c_str(), cfg_wifi[i].subnet);
    prefs.putString(("wdns"    + String(i)).c_str(), cfg_wifi[i].dns);
  }
  prefs.end();
}

WiFiMulti wifiMulti;

NimBLEServer         *pServer               = nullptr;
NimBLECharacteristic *pCharacteristicVescTx = nullptr;
NimBLECharacteristic *pCharacteristicVescRx = nullptr;

bool deviceConnected    = false;
bool oldDeviceConnected = false;
int  MTU_SIZE           = 128;
int  PACKET_SIZE        = MTU_SIZE - 3;

struct VescStatus {
  bool    connected   = false;
  float   voltage     = 0.0;
  float   tempFet     = 0.0;
  float   tempMotor   = 0.0;
  int     faultCode   = 0;
  unsigned long lastUpdate = 0;
} vescStatus;

static unsigned long lastBrowserPing = 0;

struct UpdateState {
  String availableVersion;
  String error;
} updateState;

static const uint8_t VESC_GET_VALUES_PKT[] = {0x02, 0x01, 0x04, 0x40, 0x84, 0x03};

static WiFiServer server(0);
static WiFiClient wifiClient;
static WebServer  otaServer(80);
static DNSServer  dnsServer;
static bool       isAPMode = false;

const size_t MAX_BUF         = 256;
const size_t MAX_VESC_BUFFER = 1024;
uint8_t buf[MAX_BUF];

static String vescFaultToString(int code);

static const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>🛴 VESC BLE/WiFi</title>
  <style>
    :root{
      --bg:#000000;--bg2:#111111;--bg3:#1a1a1a;
      --border:#222222;--border2:#333333;
      --text:#e0e0e0;--text2:#aaa;--text3:#666;
      --accent:#00bcd4;--accent2:#00acc1;
      --ok:#4caf50;--err:#f44336;
      --ok-bg:#0a1f0d;--err-bg:#1f0a0a;
    }
    [data-theme=light]{
      --bg:#f5f5f5;--bg2:#ffffff;--bg3:#ebebeb;
      --border:#dddddd;--border2:#cccccc;
      --text:#111111;--text2:#555555;--text3:#999999;
      --accent:#0288d1;--accent2:#0277bd;
      --ok:#388e3c;--err:#c62828;
      --ok-bg:#e8f5e9;--err-bg:#ffebee;
    }
    @media(prefers-color-scheme:light){
      :root:not([data-theme=dark]){
        --bg:#f5f5f5;--bg2:#ffffff;--bg3:#ebebeb;
        --border:#dddddd;--border2:#cccccc;
        --text:#111111;--text2:#555555;--text3:#999999;
        --accent:#0288d1;--accent2:#0277bd;
        --ok:#388e3c;--err:#c62828;
        --ok-bg:#e8f5e9;--err-bg:#ffebee;
      }
    }
    *{box-sizing:border-box;margin:0;padding:0}
    body{font-family:monospace;background:var(--bg);color:var(--text);min-height:100vh;padding:16px}
    .wrap{max-width:600px;margin:0 auto;padding:0 16px}
    h1{color:var(--accent);font-size:18px;margin-bottom:4px}
    .sub{color:var(--text3);font-size:12px;margin-bottom:24px}
    .tabs{display:flex;gap:4px;margin-bottom:16px}
    .tab{padding:8px 16px;background:var(--bg2);border:1px solid var(--border);border-radius:6px;cursor:pointer;font-family:monospace;font-size:13px;color:var(--text2)}
    .tab.active{background:var(--accent);color:#111;border-color:var(--accent)}
    .panel{display:none}.panel.active{display:block}
    .section{background:var(--bg2);border:1px solid var(--border2);border-radius:8px;padding:20px;margin-bottom:12px}
    .section h3{color:var(--accent);font-size:13px;margin-bottom:14px;text-transform:uppercase;letter-spacing:1px}
    label{display:block;font-size:12px;color:var(--text2);margin-bottom:4px;margin-top:10px}
    label:first-of-type{margin-top:0}
    input[type=text],input[type=password]{width:100%;padding:8px 10px;background:var(--bg3);border:1px solid var(--border);border-radius:4px;color:var(--text);font-family:monospace;font-size:13px}
    input:focus{outline:none;border-color:var(--accent)}
    .checkbox-row{display:flex;align-items:center;gap:10px;margin-top:12px;font-size:13px;color:var(--text2);cursor:pointer}
    .checkbox-row input[type=checkbox]{width:16px;height:16px;accent-color:var(--accent);cursor:pointer}
    .btn{width:100%;padding:11px;background:var(--accent);color:#111;border:none;border-radius:6px;font-family:monospace;font-size:14px;font-weight:bold;cursor:pointer;margin-top:14px}
    .btn:hover{background:var(--accent2)}
    .btn.sm{padding:6px 12px;font-size:12px;width:auto;margin-top:0}
    .btn.red{background:var(--err);color:#fff}.btn.red:hover{opacity:0.85}
    .btn.green{background:var(--ok);color:#111}.btn.green:hover{opacity:0.85}
    .btn:disabled{background:var(--border);color:var(--text3);cursor:not-allowed}
    .msg{margin-top:10px;padding:8px 12px;border-radius:4px;font-size:13px;display:none}
    .msg.ok{background:var(--ok-bg);color:var(--ok);display:block}
    .msg.err{background:var(--err-bg);color:var(--err);display:block}
    .wifi-entry{background:var(--bg3);border:1px solid var(--border2);border-radius:6px;padding:12px;margin-bottom:8px}
    .wifi-entry-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px}
    .wifi-entry-num{font-size:11px;color:var(--text3)}
    .wifi-entry-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
    .wifi-entry-grid label{margin-top:0}
    .add-btn-wrap{margin-top:8px}
    .drop-zone{border:2px dashed var(--border);border-radius:6px;padding:28px;text-align:center;cursor:pointer;transition:border-color .2s,background .2s;margin-bottom:12px}
    .drop-zone:hover,.drop-zone.dragover{border-color:var(--accent);background:var(--bg2)}
    .drop-zone .icon{font-size:32px;margin-bottom:6px}
    .drop-zone .label{color:var(--text2);font-size:13px}
    .drop-zone .filename{color:var(--accent);font-size:12px;margin-top:6px}
    input[type=file]{display:none}
    .progress-wrap{display:none;margin-top:12px}
    .progress-bar-bg{background:var(--border);border-radius:4px;height:8px;overflow:hidden}
    .progress-bar{background:var(--accent);height:8px;width:0%;transition:width .2s}
    .status{margin-top:8px;font-size:12px;color:var(--text2);text-align:center}
    .status.ok{color:var(--ok)}.status.err{color:var(--err)}
    .info-row{display:flex;justify-content:space-between;font-size:12px;padding:4px 0;border-bottom:1px solid var(--border2)}
    .info-row:last-child{border:none}
    .info-val{color:var(--accent)}
    .lang-btn{position:fixed;top:12px;right:12px;padding:4px 10px;background:var(--bg2);border:1px solid var(--border);border-radius:4px;color:var(--text2);font-family:monospace;font-size:12px;cursor:pointer}
    .lang-btn:hover{border-color:var(--accent);color:var(--accent)}
    .theme-btn{position:fixed;top:12px;right:56px;padding:4px 10px;background:var(--bg2);border:1px solid var(--border);border-radius:4px;color:var(--text2);font-family:monospace;font-size:12px;cursor:pointer}
    .theme-btn:hover{border-color:var(--accent);color:var(--accent)}
  </style>
</head>
<body>
<button class="theme-btn" onclick="toggleTheme()" id="themeBtn">☀️</button>
<button class="lang-btn" onclick="toggleLang()" id="langBtn">DE</button>
<div class="wrap">
  <h1>&#x1F6F4; VESC BLE/WiFi</h1>
  <div class="sub" id="statusBar">Loading...</div>

  <div class="tabs">
    <div class="tab active" onclick="showTab('info')" data-de="Info" data-en="Info">Info</div>
    <div class="tab" onclick="showTab('config')" data-de="Config" data-en="Config">Config</div>
    <div class="tab" onclick="showTab('ota')" data-de="OTA Flash" data-en="OTA Flash">OTA Flash</div>
  </div>

  <!-- INFO -->
  <div class="panel active" id="tab-info">
    <div class="section">
      <h3 id="lbl-status">Status</h3>
      <div id="infoContent"><div style="color:#666;font-size:13px" id="lbl-loading">Loading...</div></div>
    </div>
  </div>

  <!-- CONFIG -->
  <div class="panel" id="tab-config">
    <div class="section">
      <h3>BLE</h3>
      <label id="lbl-ble-name">BLE Name (visible in VESC Tool)</label>
      <input type="text" id="ble_name" maxlength="32" placeholder="VESC-BLE-WiFi">
    </div>

    <div class="section">
      <h3 id="lbl-ap-title">Access Point (Fallback)</h3>
      <label id="lbl-ap-name">AP Name (SSID)</label>
      <input type="text" id="ap_ssid" maxlength="32" placeholder="VESC-BLE-WiFi">
      <label id="lbl-ap-pass">AP Password (leave empty for open network)</label>
      <input type="password" id="ap_pass" maxlength="64" id="ph-ap-pass" placeholder="leave empty for open network">
    </div>

    <div class="section">
      <h3 id="lbl-conn-title">Connection</h3>
      <label id="lbl-port">TCP Port (default: 65101)</label>
      <input type="text" id="vesc_port" maxlength="5" placeholder="65101">
      <div style="display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:10px">
        <div><label>UART RX Pin</label><input type="text" id="rx_pin" maxlength="3" placeholder="6"></div>
        <div><label>UART TX Pin</label><input type="text" id="tx_pin" maxlength="3" placeholder="5"></div>
      </div>
      <label class="checkbox-row" style="margin-top:12px">
        <input type="checkbox" id="vesc_poll">
        <span id="lbl-vesc-poll">Read VESC data (voltage, temp, fault)</span>
      </label>
    </div>

    <div class="section">
      <h3 id="lbl-update-title">Update Server</h3>
      <label id="lbl-version-url">Version URL (version.txt)</label>
      <input type="text" id="version_url" placeholder="http://...">
      <label id="lbl-firmware-url">Firmware URL (firmware.bin)</label>
      <input type="text" id="update_url" placeholder="http://...">
    </div>

    <div class="section">
      <h3 id="lbl-wifi-title">WiFi Networks</h3>
      <div class="add-btn-wrap" style="display:flex;gap:8px;margin-bottom:10px">
        <button class="btn sm" onclick="scanWifi()" id="scanBtn">Scan</button>
        <button class="btn green sm" onclick="addWifi()" id="addBtn">+ Manual</button>
      </div>
      <div id="scanResults" style="display:none;margin-bottom:10px"></div>
      <div id="wifiList"></div>
    </div>

    <button class="btn" onclick="saveConfig()" id="saveBtn">Save &amp; Restart</button>
    <div class="msg" id="cfgMsg"></div>
    <button class="btn red" style="margin-top:8px" onclick="factoryReset()" id="factoryBtn">Factory Reset</button>
  </div>

  <!-- OTA -->
  <div class="panel" id="tab-ota">
    <div class="section">
      <h3 id="lbl-server-update">Server Update</h3>
      <div id="updateInfo" style="font-size:12px;color:#666;margin-bottom:12px"></div>
      <button class="btn" id="checkBtn" onclick="checkUpdate()" style="margin-bottom:8px">Check for Updates</button>
      <button class="btn green" id="installBtn" style="display:none" onclick="installUpdate()">Install Update</button>
      <div class="msg" id="updateMsg"></div>
    </div>
    <div class="section">
      <h3 id="lbl-manual-flash">Manual Flash</h3>
      <div class="drop-zone" id="dropZone" onclick="document.getElementById('fileInput').click()">
        <div class="icon">&#128190;</div>
        <div class="label" id="lbl-drop">Drop firmware.bin here<br>or click to select</div>
        <div class="filename" id="fileName"></div>
      </div>
      <input type="file" id="fileInput" accept=".bin">
      <button class="btn" id="uploadBtn" disabled onclick="startUpload()">Flash</button>
      <div class="progress-wrap" id="progressWrap">
        <div class="progress-bar-bg"><div class="progress-bar" id="progressBar"></div></div>
        <div class="status" id="otaStatus">Uploading...</div>
      </div>
    </div>
  </div>
</div>

<script>
function showTab(name) {
  document.querySelectorAll('.tab').forEach((t,i)=>t.classList.toggle('active',['info','config','ota'][i]===name));
  document.querySelectorAll('.panel').forEach(p=>p.classList.remove('active'));
  document.getElementById('tab-'+name).classList.add('active');
  applyTranslations();
  if(name==='info') loadInfo();
  if(name==='config') loadConfig();
  if(name==='ota') loadUpdateStatus();
}

function getThemeCookie() {
  const m = document.cookie.match(/theme=([a-z]+)/);
  return m ? m[1] : null;
}
function setThemeCookie(t) {
  document.cookie = 'theme=' + t + ';path=/;max-age=31536000';
}

const prefersDark = window.matchMedia('(prefers-color-scheme:dark)').matches;
let theme = getThemeCookie() || (prefersDark ? 'dark' : 'light');

function applyTheme() {
  document.documentElement.setAttribute('data-theme', theme);
  const btn = document.getElementById('themeBtn');
  if (btn) btn.textContent = theme === 'dark' ? '☀️' : '🌙';
  setThemeCookie(theme);
}

function toggleTheme() {
  theme = theme === 'dark' ? 'light' : 'dark';
  applyTheme();
}

applyTheme();

function getLangCookie() {
  const m = document.cookie.match(/lang=([a-z]+)/);
  return m ? m[1] : null;
}
function setLangCookie(l) {
  document.cookie = 'lang=' + l + ';path=/;max-age=31536000';
}

const T = {
  en: {
    info:'Info', config:'Config', ota:'OTA Flash',
    connected:'Connected', disconnected:'Disconnected',
    save:'Save & Restart', factory:'Factory Reset',
    scan:'Scan', manual:'+ Manual', remove:'Remove',
    staticIp:'Static IP', noNetworks:'No networks configured',
    noScanResults:'No networks found', network:'Network',
    checkUpdate:'Check for Updates', installUpdate:'Install Update',
    checking:'Checking...', upToDate:'Already up to date.',
    newVersion:'New version available!', connectionError:'Connection error',
    saved:'Saved! Restarting...', saveError:'Error saving',
    currentVersion:'Current Version', serverVersion:'Server Version',
    noUpdateServer:'No update server configured',
    uploading:'Uploading...', done:'Done! Restarting...',
    flashError:'Error: ', onlyBin:'Only .bin files!',
    vescData:'Read VESC data (voltage, temp, fault)',
    loading:'Loading...'
  },
  de: {
    info:'Info', config:'Config', ota:'OTA Flash',
    connected:'Verbunden', disconnected:'Getrennt',
    save:'Speichern & Neustart', factory:'Werkseinstellungen',
    scan:'Scan', manual:'+ Manuell', remove:'Entfernen',
    staticIp:'Statische IP', noNetworks:'Keine Netzwerke konfiguriert',
    noScanResults:'Keine Netzwerke gefunden', network:'Netzwerk',
    checkUpdate:'Auf Updates prüfen', installUpdate:'Update installieren',
    checking:'Prüfe...', upToDate:'Bereits aktuell.',
    newVersion:'Neue Version verfügbar!', connectionError:'Verbindungsfehler',
    saved:'Gespeichert! Neustart...', saveError:'Fehler beim Speichern',
    currentVersion:'Aktuelle Version', serverVersion:'Server Version',
    noUpdateServer:'Kein Update-Server konfiguriert',
    uploading:'Uploading...', done:'Fertig! Neustart...',
    flashError:'Fehler: ', onlyBin:'Nur .bin Dateien!',
    vescData:'VESC Daten auslesen (Spannung, Temp, Fault)',
    loading:'Laden...'
  }
};

let lang = getLangCookie() ||
  (navigator.language.startsWith('de') ? 'de' : 'en');

function applyLang() {
  document.getElementById('langBtn').textContent = lang === 'de' ? 'EN' : 'DE';
  document.documentElement.lang = lang;
  setLangCookie(lang);
  applyTranslations();
}

function applyTranslations() {
  const de = lang === 'de';
  const s = (id, en, de_) => { const el = document.getElementById(id); if(el) el.textContent = de ? de_ : en; };
  const sh = (id, en, de_) => { const el = document.getElementById(id); if(el) el.innerHTML = de ? de_ : en; };
  s('lbl-status',       'Status',                          'Status');
  s('lbl-loading',      'Loading...',                      'Laden...');
  s('lbl-ble-name',     'BLE Name (visible in VESC Tool)', 'BLE Name (sichtbar in VESC Tool)');
  s('lbl-ap-title',     'Access Point (Fallback)',         'Access Point (Fallback)');
  s('lbl-ap-name',      'AP Name (SSID)',                  'AP Name (SSID)');
  s('lbl-ap-pass',      'AP Password (leave empty for open network)', 'AP Passwort (leer = offenes Netz)');
  s('lbl-conn-title',   'Connection',                      'Verbindung');
  s('lbl-port',         'TCP Port (default: 65101)',        'TCP Port (Standard: 65101)');
  s('lbl-vesc-poll',    'Read VESC data (voltage, temp, fault)', 'VESC Daten auslesen (Spannung, Temp, Fault)');
  s('lbl-update-title', 'Update Server',                   'Update Server');
  s('lbl-version-url',  'Version URL (version.txt)',       'Version URL (version.txt)');
  s('lbl-firmware-url', 'Firmware URL (firmware.bin)',     'Firmware URL (firmware.bin)');
  s('lbl-wifi-title',   'WiFi Networks',                   'WiFi Netzwerke');
  s('scanBtn',          'Scan',                            'Scan');
  s('addBtn',           '+ Manual',                        '+ Manuell');
  s('saveBtn',          'Save & Restart',                  'Speichern & Neustart');
  s('factoryBtn',       'Factory Reset',                   'Werkseinstellungen');
  s('lbl-server-update','Server Update',                   'Server Update');
  s('checkBtn',         'Check for Updates',               'Auf Updates prüfen');
  s('installBtn',       'Install Update',                  'Update installieren');
  s('lbl-manual-flash', 'Manual Flash',                    'Manueller Flash');
  sh('lbl-drop',        'Drop firmware.bin here<br>or click to select', 'firmware.bin hier ablegen<br>oder klicken');
  s('uploadBtn',        'Flash',                           'Flashen');
}

function toggleLang() {
  lang = lang === 'de' ? 'en' : 'de';
  setLangCookie(lang);
  location.reload();
}

function t(key) { return T[lang][key] || T.en[key] || key; }

applyLang();

function loadInfo() {
  fetch('/api/info').then(r=>r.json()).then(d=>{
    const de = lang==='de';
    document.getElementById('statusBar').textContent = d.mode==='ap'?'AP Mode: '+d.ip:'WiFi: '+d.ssid+' ('+d.ip+')';
    document.getElementById('infoContent').innerHTML=`
      <div class="info-row"><span>BLE Name</span><span class="info-val">${d.ble_name}</span></div>
      <div class="info-row"><span>BLE MAC</span><span class="info-val">${d.ble_mac}</span></div>
      <div class="info-row"><span>WiFi Mode</span><span class="info-val">${d.mode==='ap'?(de?'Access Point':'Access Point'):(de?'Client':'Client')}</span></div>
      <div class="info-row"><span>IP</span><span class="info-val">${d.ip}</span></div>
      ${d.mode!=='ap'?`<div class="info-row"><span>SSID</span><span class="info-val">${d.ssid}</span></div>`:''}
      ${d.mode!=='ap'?`<div class="info-row"><span>RSSI</span><span class="info-val">${d.rssi} dBm</span></div>`:''}
      <div class="info-row"><span>BLE Client</span><span class="info-val">${d.ble_connected?(de?'Verbunden':'Connected'):(de?'Getrennt':'Disconnected')}</span></div>
      <div class="info-row"><span>WiFi Client</span><span class="info-val">${d.wifi_client_connected?(de?'Verbunden':'Connected'):(de?'Getrennt':'Disconnected')}</span></div>
      <div class="info-row"><span>TCP Port</span><span class="info-val">${d.port}</span></div>
      <div class="info-row"><span>UART</span><span class="info-val">RX=GPIO${d.rx_pin} TX=GPIO${d.tx_pin}</span></div>
      <div style="margin:10px 0 6px 0;font-size:11px;color:#666;text-transform:uppercase;letter-spacing:1px">VESC</div>
      <div class="info-row"><span>VESC</span><span class="info-val" style="color:${d.vesc_connected?'#81c784':'#e57373'}">${d.vesc_connected?(de?'Verbunden':'Connected'):(de?'Nicht verbunden':'Disconnected')}</span></div>
      <div class="info-row" style="${d.vesc_connected?'':'opacity:0.4'}"><span>${de?'Akkuspannung':'Battery Voltage'}</span><span class="info-val">${d.vesc_voltage} V</span></div>
      <div class="info-row" style="${d.vesc_connected?'':'opacity:0.4'}"><span>Temp FET</span><span class="info-val">${d.vesc_temp_fet} °C</span></div>
      <div class="info-row" style="${d.vesc_connected?'':'opacity:0.4'}"><span>Temp Motor</span><span class="info-val">${d.vesc_temp_motor} °C</span></div>
      <div class="info-row" style="${d.vesc_connected?'':'opacity:0.4'}"><span>${lang==='de'?'Fehlercode':'Fault Code'}</span><span class="info-val" style="color:${d.vesc_fault===0?'#81c784':'#e57373'}">${d.vesc_fault_str||'OK'}</span></div>
      <div class="info-row"><span>${de?'Uptime':'Uptime'}</span><span class="info-val">${d.uptime}</span></div>
      <div class="info-row"><span>Build</span><span class="info-val">${d.build}</span></div>
    `;
  }).catch(()=>{document.getElementById('infoContent').innerHTML='<div style="color:#e57373;font-size:13px">'+(lang==='de'?'Fehler beim Laden':'Error loading data')+'</div>';});
}
loadInfo();
setInterval(()=>{ if(document.getElementById('tab-info').classList.contains('active')) loadInfo(); }, 1000);

setInterval(()=>fetch('/api/ping'), 2000);

function loadUpdateStatus() {
  fetch('/api/update/status').then(r=>r.json()).then(d=>{
    const cv = (typeof t === 'function') ? t('currentVersion') : 'Current Version';
    const sv = (typeof t === 'function') ? t('serverVersion') : 'Server Version';
    const nu = (typeof t === 'function') ? t('noUpdateServer') : 'No update server configured';
    document.getElementById('updateInfo').innerHTML =
      `<div class="info-row"><span>${cv}</span><span class="info-val">${d.current||''}</span></div>` +
      (d.available ? `<div class="info-row"><span>${sv}</span><span class="info-val">${d.available}</span></div>` : '') +
      (d.error ? `<div style="color:var(--err);margin-top:6px">${d.error}</div>` : '') +
      (!d.update_url ? `<div style="color:var(--text3);margin-top:6px">${nu}</div>` : '');
  }).catch(()=>{
    document.getElementById('updateInfo').innerHTML = '';
  });
}

function checkUpdate() {
  const msg = document.getElementById('updateMsg');
  const btn = document.getElementById('checkBtn');
  msg.className='msg'; msg.style.display='none';
  btn.disabled = true; btn.textContent=lang==='de'?'Prüfe...':'Checking...';
  fetch('/api/update/check').then(r=>r.json()).then(d=>{
    btn.disabled=false; btn.textContent=lang==='de'?'Auf Updates prüfen':'Check for Updates';
    document.getElementById('updateInfo').innerHTML =
      `<div class="info-row"><span>${t('currentVersion')}</span><span class="info-val">${d.current}</span></div>` +
      `<div class="info-row"><span>${t('serverVersion')}</span><span class="info-val">${d.available}</span></div>`;
    if (d.update_available) {
      msg.textContent=lang==='de'?'Neue Version verfügbar!':'New version available!'; msg.className='msg ok';
      document.getElementById('installBtn').style.display='block';
    } else if (d.error) {
      msg.textContent='Error: '+d.error; msg.className='msg err';
    } else {
      msg.textContent=lang==='de'?'Bereits aktuell.':'Already up to date.'; msg.className='msg ok';
      document.getElementById('installBtn').style.display='none';
    }
  }).catch(()=>{
    btn.disabled=false; btn.textContent=lang==='de'?'Auf Updates prüfen':'Check for Updates';
    msg.textContent=lang==='de'?'Verbindungsfehler':'Connection error'; msg.className='msg err';
  });
}

function installUpdate() {
  const msg = document.getElementById('updateMsg');
  const btn = document.getElementById('installBtn');
  btn.disabled=true; btn.textContent='Installiere...';
  msg.textContent=lang==='de'?'Update wird installiert...':'Downloading and installing update...'; msg.className='msg ok';
  fetch('/api/update/install', {method:'POST'}).then(()=>{
    msg.textContent=lang==='de'?'Update gestartet! ESP startet neu...':'Update started! ESP restarting...'; msg.className='msg ok';
    setTimeout(()=>location.reload(), 15000);
  }).catch(()=>{
    msg.textContent=lang==='de'?'Verbindungsfehler':'Connection error'; msg.className='msg err';
    btn.disabled=false;
  });
}

function scanWifi() {
  const btn = document.getElementById('scanBtn');
  const results = document.getElementById('scanResults');
  const de = lang === 'de';
  btn.disabled = true; btn.textContent = de ? 'Scanne...' : 'Scanning...';
  results.style.display = 'none';
  fetch('/api/wifi/scan').then(r=>r.json()).then(nets => {
    btn.disabled = false; btn.textContent = 'Scan';
    if (nets.length === 0) {
      results.innerHTML = '<div style="color:#666;font-size:12px">'+(de?'Keine Netzwerke gefunden':'No networks found')+'</div>';
    } else {
      results.innerHTML = nets.sort((a,b)=>b.rssi-a.rssi).map(n=>`
        <div style="display:flex;justify-content:space-between;align-items:center;padding:6px 8px;background:#2a2a2a;border-radius:4px;margin-bottom:4px;font-size:12px;cursor:pointer" onclick="addWifiFromScan('${escHtml(n.ssid)}')">
          <span>${escHtml(n.ssid)} ${n.secure?'🔒':''}</span>
          <span style="color:#666">${n.rssi} dBm</span>
        </div>`).join('');
    }
    results.style.display = 'block';
  }).catch(()=>{
    btn.disabled = false; btn.textContent = 'Scan';
  });
}

function addWifiFromScan(ssid) {
  if (wifiNetworks.length >= 10) {
    alert(lang==='de' ? 'Maximal 10 Netzwerke möglich' : 'Maximum 10 networks allowed');
    return;
  }
  wifiNetworks.push({ssid, pass:'', static:false, ip:'', gateway:'', subnet:'255.255.255.0', dns:''});
  renderWifiList();
  document.getElementById('scanResults').style.display = 'none';
  const pwInputs = document.querySelectorAll('#wifiList input[type=password]');
  if (pwInputs.length) pwInputs[pwInputs.length-1].focus();
}

function factoryReset() {
  if (!confirm('Alle Einstellungen zurücksetzen?')) return;
  fetch('/api/factory-reset', {method:'POST'}).then(()=>{
    alert(lang==='de'?'Zurückgesetzt! Neustart...':'Reset! ESP restarting...');
  }).catch(()=>{});
}

let wifiNetworks = [];

function renderWifiList() {
  const list = document.getElementById('wifiList');
  const de = lang === 'de';
  if (wifiNetworks.length === 0) {
    list.innerHTML = '<div style="color:#666;font-size:12px;padding:8px 0">'+(de?'Keine Netzwerke konfiguriert':'No networks configured')+'</div>';
    return;
  }
  list.innerHTML = wifiNetworks.map((n,i)=>`
    <div class="wifi-entry">
      <div class="wifi-entry-header">
        <span class="wifi-entry-num">${de?'Netzwerk':'Network'} ${i+1}</span>
        <button class="btn red sm" onclick="removeWifi(${i})">&#x2715; ${de?'Entfernen':'Remove'}</button>
      </div>
      <div class="wifi-entry-grid">
        <div><label>SSID</label><input type="text" maxlength="32" placeholder="SSID" value="${escHtml(n.ssid)}" onchange="wifiNetworks[${i}].ssid=this.value"></div>
        <div><label>${de?'Passwort':'Password'}</label><input type="password" maxlength="64" placeholder="${de?'Passwort':'Password'}" value="${escHtml(n.pass)}" onchange="wifiNetworks[${i}].pass=this.value"></div>
      </div>
      <label class="checkbox-row" style="margin-top:10px">
        <input type="checkbox" ${n.static?'checked':''} onchange="wifiNetworks[${i}].static=this.checked;renderWifiList()">
        ${de?'Statische IP':'Static IP'}
      </label>
      ${n.static?`
      <div style="display:grid;grid-template-columns:1fr 1fr 1fr 1fr;gap:8px;margin-top:8px">
        <div><label>IP</label><input type="text" maxlength="15" placeholder="192.168.1.100" value="${escHtml(n.ip||'')}" onchange="wifiNetworks[${i}].ip=this.value"></div>
        <div><label>Gateway</label><input type="text" maxlength="15" placeholder="192.168.1.1" value="${escHtml(n.gateway||'')}" onchange="wifiNetworks[${i}].gateway=this.value"></div>
        <div><label>Subnet</label><input type="text" maxlength="15" placeholder="255.255.255.0" value="${escHtml(n.subnet||'255.255.255.0')}" onchange="wifiNetworks[${i}].subnet=this.value"></div>
        <div><label>DNS</label><input type="text" maxlength="15" placeholder="8.8.8.8" value="${escHtml(n.dns||'')}" onchange="wifiNetworks[${i}].dns=this.value"></div>
      </div>`:''}
    </div>
  `).join('');
}

function escHtml(s) { return s.replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/</g,'&lt;'); }

function addWifi() {
  if (wifiNetworks.length >= 10) {
    alert(lang==='de' ? 'Maximal 10 Netzwerke möglich' : 'Maximum 10 networks allowed');
    return;
  }
  wifiNetworks.push({ssid:'', pass:'', static:false, ip:'', gateway:'', subnet:'255.255.255.0', dns:''});
  renderWifiList();
  const inputs = document.querySelectorAll('#wifiList input[type=text]');
  if(inputs.length) inputs[inputs.length-1].focus();
}

function removeWifi(i) {
  wifiNetworks.splice(i,1);
  renderWifiList();
}

function loadConfig() {
  fetch('/api/config').then(r=>r.json()).then(d=>{
    document.getElementById('ble_name').value    = d.ble_name||'';
    document.getElementById('ap_ssid').value     = d.ap_ssid||'';
    document.getElementById('ap_pass').value     = d.ap_pass||'';
    document.getElementById('vesc_port').value   = d.port||65101;
    document.getElementById('rx_pin').value      = d.rx_pin||6;
    document.getElementById('tx_pin').value      = d.tx_pin||5;
    document.getElementById('vesc_poll').checked = d.vesc_poll !== false;
    document.getElementById('version_url').value = d.version_url||'';
    document.getElementById('update_url').value  = d.update_url||'';
    wifiNetworks = (d.wifi||[]).map(n=>({
      ssid:    n.ssid||'',
      pass:    n.pass||'',
      static:  n.static||false,
      ip:      n.ip||'',
      gateway: n.gateway||'',
      subnet:  n.subnet||'255.255.255.0',
      dns:     n.dns||''
    }));
    renderWifiList();
  });
}

function saveConfig() {
  const wifi = wifiNetworks.filter(n=>n.ssid.trim().length>0);

  const body = JSON.stringify({
    ble_name:    document.getElementById('ble_name').value,
    ap_ssid:     document.getElementById('ap_ssid').value,
    ap_pass:     document.getElementById('ap_pass').value,
    port:        parseInt(document.getElementById('vesc_port').value)||65101,
    rx_pin:      parseInt(document.getElementById('rx_pin').value)||6,
    tx_pin:      parseInt(document.getElementById('tx_pin').value)||5,
    vesc_poll:   document.getElementById('vesc_poll').checked,
    version_url: document.getElementById('version_url').value,
    update_url:  document.getElementById('update_url').value,
    wifi
  });
  const msg = document.getElementById('cfgMsg');
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body})
    .then(r=>{
      if(r.ok){msg.textContent=lang==='de'?'Gespeichert! Neustart...':'Saved! Restarting...';msg.className='msg ok';setTimeout(()=>location.reload(),4000);}
      else{msg.textContent=lang==='de'?'Fehler beim Speichern':'Error saving';msg.className='msg err';}
    }).catch(()=>{msg.textContent=lang==='de'?'Verbindungsfehler':'Connection error';msg.className='msg err';});
}

const dropZone=document.getElementById('dropZone');
const fileInput=document.getElementById('fileInput');
fileInput.addEventListener('change',e=>selectFile(e.target.files[0]));
dropZone.addEventListener('dragover',e=>{e.preventDefault();dropZone.classList.add('dragover');});
dropZone.addEventListener('dragleave',()=>dropZone.classList.remove('dragover'));
dropZone.addEventListener('drop',e=>{e.preventDefault();dropZone.classList.remove('dragover');if(e.dataTransfer.files.length)selectFile(e.dataTransfer.files[0]);});
let selectedFile=null;
function selectFile(file){
  if(!file||!file.name.endsWith('.bin')){
    document.getElementById('otaStatus').textContent=lang==='de'?'Nur .bin Dateien!':'Only .bin files!';
    document.getElementById('otaStatus').className='status err';
    document.getElementById('progressWrap').style.display='block';
    return;
  }
  selectedFile=file;
  document.getElementById('fileName').textContent=file.name+' ('+(file.size/1024).toFixed(1)+' KB)';
  document.getElementById('uploadBtn').disabled=false;
  document.getElementById('progressWrap').style.display='none';
}
function startUpload(){
  if(!selectedFile)return;
  document.getElementById('uploadBtn').disabled=true;
  document.getElementById('progressWrap').style.display='block';
  document.getElementById('progressBar').style.width='0%';
  document.getElementById('otaStatus').textContent=lang==='de'?'Uploading...':'Uploading...';
  document.getElementById('otaStatus').className='status';
  const fd=new FormData();
  fd.append('firmware',selectedFile,selectedFile.name);
  const xhr=new XMLHttpRequest();
  xhr.open('POST','/update',true);
  xhr.upload.onprogress=e=>{if(e.lengthComputable){const p=Math.round(e.loaded/e.total*100);document.getElementById('progressBar').style.width=p+'%';document.getElementById('otaStatus').textContent='Uploading... '+p+'%';}};
  xhr.onload=()=>{
    if(xhr.status===200){document.getElementById('progressBar').style.width='100%';document.getElementById('otaStatus').textContent=lang==='de'?'Fertig! Neustart...':'Done! Restarting...';document.getElementById('otaStatus').className='status ok';setTimeout(()=>location.reload(),7000);}
    else{document.getElementById('otaStatus').textContent='Fehler: '+xhr.responseText;document.getElementById('otaStatus').className='status err';document.getElementById('uploadBtn').disabled=false;}
  };
  xhr.onerror=()=>{document.getElementById('otaStatus').textContent='Connection error';document.getElementById('otaStatus').className='status err';document.getElementById('uploadBtn').disabled=false;};
  xhr.send(fd);
}
</script>
</body>
</html>
)rawliteral";

void handleCaptivePortal() {
  otaServer.sendHeader("Location", "http://192.168.4.1/", true);
  otaServer.send(302, "text/plain", "");
}

bool isCaptivePortalRequest() {
  String host = otaServer.hostHeader();
  return (host != "192.168.4.1" && host != cfg_hostname + ".local");
}

void handlePage() {
  if (isAPMode && isCaptivePortalRequest()) { handleCaptivePortal(); return; }
  otaServer.send(200, "text/html", PAGE_HTML);
}

void handleApiInfo() {
  unsigned long up = millis() / 1000;
  String uptime = String(up/3600)+"h "+String((up%3600)/60)+"m "+String(up%60)+"s";
  String json = "{";
  json += "\"ble_name\":\"" + cfg_ble_name + "\",";
  json += "\"ble_connected\":" + String(deviceConnected?"true":"false") + ",";
  json += "\"ble_mac\":\"" + String(NimBLEDevice::getAddress().toString().c_str()) + "\",";
  json += "\"wifi_client_connected\":" + String((wifiClient && wifiClient.connected())?"true":"false") + ",";
  json += "\"heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"uptime\":\"" + uptime + "\",";
  json += "\"build\":\"" + String(FIRMWARE_VERSION) + " (" + String(__DATE__) + " " + String(__TIME__) + ")\",";
  json += "\"port\":" + String(cfg_port) + ",";
  json += "\"rx_pin\":" + String(cfg_rx_pin) + ",";
  json += "\"tx_pin\":" + String(cfg_tx_pin) + ",";
  json += "\"vesc_connected\":" + String(vescStatus.connected ? "true" : "false") + ",";
  json += "\"vesc_voltage\":" + String(vescStatus.voltage, 2) + ",";
  json += "\"vesc_temp_fet\":" + String(vescStatus.tempFet, 1) + ",";
  json += "\"vesc_temp_motor\":" + String(vescStatus.tempMotor, 1) + ",";
  json += "\"vesc_fault\":" + String(vescStatus.faultCode) + ",";
  json += "\"vesc_fault_str\":\"" + vescFaultToString(vescStatus.faultCode) + "\",";
  if (isAPMode) {
    json += "\"mode\":\"ap\",";
    json += "\"ip\":\"" + WiFi.softAPIP().toString() + "\"";
  } else {
    json += "\"mode\":\"client\",";
    json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
    json += "\"ssid\":\"" + WiFi.SSID() + "\",";
    json += "\"rssi\":" + String(WiFi.RSSI());
  }
  json += "}";
  otaServer.send(200, "application/json", json);
}

void handleApiConfigGet() {
  String json = "{";
  json += "\"ble_name\":\"" + cfg_ble_name + "\",";
  json += "\"ap_ssid\":\"" + cfg_ap_ssid + "\",";
  json += "\"ap_pass\":\"" + cfg_ap_pass + "\",";
  json += "\"port\":" + String(cfg_port) + ",";
  json += "\"vesc_poll\":" + String(cfg_vesc_poll ? "true" : "false") + ",";
  json += "\"rx_pin\":" + String(cfg_rx_pin) + ",";
  json += "\"tx_pin\":" + String(cfg_tx_pin) + ",";
  json += "\"update_url\":\"" + cfg_update_url + "\",";
  json += "\"version_url\":\"" + cfg_version_url + "\",";
  json += "\"wifi\":[";
  for (int i = 0; i < (int)cfg_wifi.size(); i++) {
    json += "{\"ssid\":\"" + cfg_wifi[i].ssid + "\",\"pass\":\"" + cfg_wifi[i].pass + "\"";
    json += ",\"static\":" + String(cfg_wifi[i].staticIp ? "true" : "false");
    json += ",\"ip\":\"" + cfg_wifi[i].ip + "\"";
    json += ",\"gateway\":\"" + cfg_wifi[i].gateway + "\"";
    json += ",\"subnet\":\"" + cfg_wifi[i].subnet + "\"";
    json += ",\"dns\":\"" + cfg_wifi[i].dns + "\"}";
    if (i < (int)cfg_wifi.size()-1) json += ",";
  }
  json += "]}";
  otaServer.send(200, "application/json", json);
}

void handleApiConfigPost() {
  String body = otaServer.arg("plain");

  auto extract = [&](String key) -> String {
    String search = "\"" + key + "\":\"";
    int start = body.indexOf(search);
    if (start < 0) return "";
    start += search.length();
    int end = body.indexOf("\"", start);
    return end < 0 ? "" : body.substring(start, end);
  };

  cfg_ble_name    = extract("ble_name");
  cfg_ap_ssid     = extract("ap_ssid");
  cfg_ap_pass     = extract("ap_pass");
  cfg_update_url  = extract("update_url");
  cfg_version_url = extract("version_url");

  int portStart = body.indexOf("\"port\":");
  if (portStart >= 0) {
    portStart += 7;
    int portEnd = body.indexOf(",", portStart);
    if (portEnd < 0) portEnd = body.indexOf("}", portStart);
    if (portEnd > 0) {
      int p = body.substring(portStart, portEnd).toInt();
      cfg_port = (p > 0 && p <= 65535) ? p : VESC_TCP_PORT;
    }
  }

  cfg_vesc_poll = (body.indexOf("\"vesc_poll\":true") >= 0);

  auto parseInt2 = [&](String key, int def) -> int {
    String search = "\"" + key + "\":";
    int start = body.indexOf(search);
    if (start < 0) return def;
    start += search.length();
    int end = body.indexOf(",", start);
    if (end < 0) end = body.indexOf("}", start);
    if (end < 0) return def;
    int val = body.substring(start, end).toInt();
    return (val >= 0 && val <= 48) ? val : def;
  };
  cfg_rx_pin = parseInt2("rx_pin", VESC_RX_PIN);
  cfg_tx_pin = parseInt2("tx_pin", VESC_TX_PIN);

  cfg_wifi.clear();
  int arrStart = body.indexOf("\"wifi\":[");
  if (arrStart >= 0) {
    String arr = body.substring(arrStart + 8);
    int pos = 0;
    while (pos < (int)arr.length() && cfg_wifi.size() < MAX_WIFI_NETWORKS) {
      int objStart = arr.indexOf('{', pos);
      if (objStart < 0) break;
      int objEnd = arr.indexOf('}', objStart);
      if (objEnd < 0) break;
      String entry = arr.substring(objStart, objEnd+1);
      auto exLocal = [&](String k) -> String {
        String s = "\"" + k + "\":\"";
        int st = entry.indexOf(s);
        if (st < 0) return "";
        st += s.length();
        int en = entry.indexOf("\"", st);
        return en < 0 ? "" : entry.substring(st, en);
      };
      String ssid = exLocal("ssid");
      if (ssid.length() > 0) {
        WiFiEntry e;
        e.ssid     = ssid;
        e.pass     = exLocal("pass");
        e.staticIp = (entry.indexOf("\"static\":true") >= 0);
        e.ip       = exLocal("ip");
        e.gateway  = exLocal("gateway");
        e.subnet   = exLocal("subnet");
        e.dns      = exLocal("dns");
        if (e.subnet.isEmpty()) e.subnet = "255.255.255.0";
        cfg_wifi.push_back(e);
      }
      pos = objEnd + 1;
    }
  }

  if (cfg_ble_name.isEmpty()) cfg_ble_name = DEFAULT_BLE_NAME;
  if (cfg_ap_ssid.isEmpty())  cfg_ap_ssid  = DEFAULT_AP_SSID;

  saveConfig();
  otaServer.send(200, "text/plain", "OK");
  delay(500);
  ESP.restart();
}

void handleOTAUpdate() {
  HTTPUpload &upload = otaServer.upload();
  if (upload.status == UPLOAD_FILE_START) {
    Serial.printf("OTA Start: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) Update.printError(Serial);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) Serial.printf("OTA Success: %u bytes\n", upload.totalSize);
    else Update.printError(Serial);
  }
}

void handleOTAFinish() {
  if (Update.hasError()) {
    otaServer.send(500, "text/plain", Update.errorString());
  } else {
    otaServer.send(200, "text/plain", "OK");
    delay(500);
    ESP.restart();
  }
}

String getCurrentVersion() {
  return String(FIRMWARE_VERSION);
}

void handleApiUpdateCheck() {
  if (isAPMode) { otaServer.send(400, "application/json", "{\"error\":\"Only available in WiFi mode\"}"); return; }
  if (cfg_version_url.isEmpty()) { otaServer.send(400, "application/json", "{\"error\":\"No version URL configured\"}"); return; }

  HTTPClient http;
  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  bool isHttps = cfg_version_url.startsWith("https");
  if (isHttps) http.begin(secureClient, cfg_version_url);
  else http.begin(cfg_version_url);
  http.setTimeout(8000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int code = http.GET();
  if (code == 200) {
    String ver = http.getString();
    ver.trim();
    updateState.availableVersion = ver;
    updateState.error = "";
    String json = "{\"current\":\"" + getCurrentVersion() + "\",\"available\":\"" + ver + "\",\"update_available\":" + (ver != getCurrentVersion() ? "true" : "false") + "}";
    otaServer.send(200, "application/json", json);
  } else {
    updateState.error = "HTTP " + String(code);
    otaServer.send(500, "application/json", "{\"error\":\"Server unreachable (HTTP " + String(code) + ")\"}");
  }
  http.end();
}

void handleApiUpdateInstall() {
  if (isAPMode) { otaServer.send(400, "text/plain", "Only available in WiFi mode"); return; }
  if (cfg_update_url.isEmpty()) { otaServer.send(400, "text/plain", "No update URL configured"); return; }

  otaServer.send(200, "text/plain", "OK");
  delay(500);

  Serial.println("Starting HTTP OTA update...");
  bool isHttps = cfg_update_url.startsWith("https");
  t_httpUpdate_return ret;
  if (isHttps) {
    WiFiClientSecure secureClient;
    secureClient.setInsecure();
    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    ret = httpUpdate.update(secureClient, cfg_update_url);
  } else {
    WiFiClient client;
    ret = httpUpdate.update(client, cfg_update_url);
  }
  switch (ret) {
    case HTTP_UPDATE_FAILED:
      Serial.printf("OTA failed: %s\n", httpUpdate.getLastErrorString().c_str());
      break;
    case HTTP_UPDATE_NO_UPDATES:
      Serial.println("No update available");
      break;
    case HTTP_UPDATE_OK:
      Serial.println("OTA OK — restarting");
      break;
  }
}

void handleApiUpdateStatus() {
  String json = "{";
  json += "\"current\":\"" + getCurrentVersion() + "\",";
  json += "\"available\":\"" + updateState.availableVersion + "\",";
  json += "\"update_url\":\"" + cfg_update_url + "\",";
  json += "\"version_url\":\"" + cfg_version_url + "\",";
  json += "\"error\":\"" + updateState.error + "\"";
  json += "}";
  otaServer.send(200, "application/json", json);
}

void handleApiReset() {
  otaServer.send(200, "text/plain", "OK");
  delay(500);
  ESP.restart();
}

void handleApiFactoryReset() {
  prefs.begin("vesccfg", false);
  prefs.clear();
  prefs.end();
  otaServer.send(200, "text/plain", "OK");
  delay(500);
  ESP.restart();
}

void handleApiWifiScan() {
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; i++) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"secure\":" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  json += "]";
  WiFi.scanDelete();
  otaServer.send(200, "application/json", json);
}

void setupWebServer() {
  otaServer.on("/", HTTP_GET, handlePage);
  otaServer.on("/api/info",           HTTP_GET,  handleApiInfo);
  otaServer.on("/api/config",         HTTP_GET,  handleApiConfigGet);
  otaServer.on("/api/config",         HTTP_POST, handleApiConfigPost);
  otaServer.on("/api/factory-reset", HTTP_POST, handleApiFactoryReset);
  otaServer.on("/api/wifi/scan",     HTTP_GET,  handleApiWifiScan);
  otaServer.on("/api/update/status",  HTTP_GET,  handleApiUpdateStatus);
  otaServer.on("/api/update/check",   HTTP_GET,  handleApiUpdateCheck);
  otaServer.on("/api/update/install", HTTP_POST, handleApiUpdateInstall);
  otaServer.on("/api/ping",   HTTP_GET,  []() {
    lastBrowserPing = millis();
    otaServer.send(200, "text/plain", "ok");
  });
  otaServer.on("/update", HTTP_POST, handleOTAFinish, handleOTAUpdate);
  otaServer.on("/generate_204",              HTTP_GET, handleCaptivePortal);
  otaServer.on("/gen_204",                   HTTP_GET, handleCaptivePortal);
  otaServer.on("/hotspot-detect.html",       HTTP_GET, handlePage);
  otaServer.on("/library/test/success.html", HTTP_GET, handlePage);
  otaServer.on("/ncsi.txt",                  HTTP_GET, handleCaptivePortal);
  otaServer.on("/connecttest.txt",           HTTP_GET, handleCaptivePortal);
  otaServer.on("/redirect",                  HTTP_GET, handleCaptivePortal);
  otaServer.onNotFound([]() {
    if (isAPMode) handleCaptivePortal();
    else otaServer.send(404, "text/plain", "Not found");
  });
  otaServer.begin();
}

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) {
    Serial.printf("BLE connected: %s\n", NimBLEAddress(desc->peer_ota_addr).toString().c_str());
    deviceConnected = true;
    NimBLEDevice::startAdvertising();
  }
  void onDisconnect(NimBLEServer *pServer) {
    Serial.println("BLE disconnected");
    deviceConnected = false;
    NimBLEDevice::startAdvertising();
  }
  void onMTUChange(uint16_t MTU, ble_gap_conn_desc *desc) {
    Serial.printf("BLE MTU: %d\n", MTU);
    MTU_SIZE    = MTU;
    PACKET_SIZE = MTU_SIZE - 3;
  }
};

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string rxValue = pCharacteristic->getValue();
    if (rxValue.length() > 0 && pCharacteristic->getUUID().equals(pCharacteristicVescRx->getUUID())) {
      Serial1.write((const uint8_t*)rxValue.data(), rxValue.length());
    }
  }
};

bool setupWiFiClient() {
  if (cfg_wifi.empty()) {
    Serial.println("WiFi: no networks configured");
    return false;
  }
  Serial.println("WiFi Client: scanning known networks...");
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(cfg_hostname.c_str());
  for (auto &n : cfg_wifi) {
    wifiMulti.addAP(n.ssid.c_str(), n.pass.c_str());
    Serial.printf("  + %s\n", n.ssid.c_str());
  }
  unsigned long start = millis();
  while (wifiMulti.run(15000) != WL_CONNECTED) {
    if (millis() - start > 17000) {
      Serial.println("WiFi: all networks failed!");
      WiFi.disconnect();
      return false;
    }
    delay(500);
    Serial.print(".");
  }

  String connectedSSID = WiFi.SSID();
  for (auto &n : cfg_wifi) {
    if (n.ssid == connectedSSID && n.staticIp && n.ip.length() > 0) {
      IPAddress ip, gw, sub;
      if (ip.fromString(n.ip) && gw.fromString(n.gateway) && sub.fromString(n.subnet)) {
        IPAddress dnsIp;
        if (n.dns.length() > 0 && dnsIp.fromString(n.dns)) {
          WiFi.config(ip, gw, sub, dnsIp);
        } else {
          WiFi.config(ip, gw, sub);
        }
        Serial.printf("Static IP applied: %s\n", n.ip.c_str());
      }
      break;
    }
  }
  Serial.printf("\nWiFi: %s | IP: %s | RSSI: %d dBm\n",
    WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

bool setupAccessPoint() {
  Serial.printf("AP: %s\n", cfg_ap_ssid.c_str());
  const char *pass = cfg_ap_pass.length() > 0 ? cfg_ap_pass.c_str() : nullptr;
  bool ok = WiFi.softAP(cfg_ap_ssid.c_str(), pass, 6, 0, 4);
  if (ok) {
    isAPMode = true;
    Serial.printf("AP IP: %s\n", WiFi.softAPIP().toString().c_str());
  } else {
    Serial.println("AP start failed!");
  }
  return ok;
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n=== VESC BLE/WiFi Bridge ===");

  loadConfig();
  Serial.printf("BLE Name: %s | WiFi networks: %d\n", cfg_ble_name.c_str(), cfg_wifi.size());
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());

  Serial1.setRxBufferSize(512);
  Serial1.setTxBufferSize(512);
  Serial1.begin(115200, SERIAL_8N1, cfg_rx_pin, cfg_tx_pin);
  Serial.printf("VESC Serial: RX=GPIO%d TX=GPIO%d\n", cfg_rx_pin, cfg_tx_pin);

  if (cfg_ble_name.isEmpty()) cfg_ble_name = DEFAULT_BLE_NAME;
  NimBLEDevice::init(cfg_ble_name.c_str());
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(VESC_SERVICE_UUID);
  pCharacteristicVescTx = pService->createCharacteristic(VESC_CHARACTERISTIC_UUID_TX, NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);
  pCharacteristicVescRx = pService->createCharacteristic(VESC_CHARACTERISTIC_UUID_RX, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  pCharacteristicVescRx->setCallbacks(new MyCallbacks());

  pService->start();
  pServer->start();

  NimBLEAdvertising *pAdv = NimBLEDevice::getAdvertising();
  pAdv->addServiceUUID(VESC_SERVICE_UUID);
  pAdv->start();
  Serial.printf("BLE advertising: %s\n", cfg_ble_name.c_str());

  bool wifiOK = setupWiFiClient();
  if (!wifiOK) wifiOK = setupAccessPoint();

  if (wifiOK) {
    IPAddress myIP = isAPMode ? WiFi.softAPIP() : WiFi.localIP();

    setupWebServer();
    Serial.printf("Web: http://%s/\n", myIP.toString().c_str());

    if (isAPMode) {
      dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
      dnsServer.start(53, "*", WiFi.softAPIP());
      Serial.println("Captive Portal DNS aktiv");
    }

    server = WiFiServer(cfg_port);
    server.begin();
    server.setNoDelay(true);
    Serial.printf("VESC TCP: %s:%d\n", myIP.toString().c_str(), cfg_port);
  } else {
    Serial.println("WiFi failed — nur BLE aktiv");
  }

  NimBLEDevice::startAdvertising();
  Serial.println("BLE advertising restarted after WiFi init");

  Serial.printf("Free heap after init: %d bytes\n", ESP.getFreeHeap());
  Serial.println("=== Ready ===\n");
}

static float readFloat16(const uint8_t *buf, float scale) {
  int16_t val = ((int16_t)buf[0] << 8) | buf[1];
  return val / scale;
}

static String vescFaultToString(int code) {
  switch (code) {
    case 0:  return "OK";
    case 1:  return "OVER_VOLTAGE";
    case 2:  return "UNDER_VOLTAGE";
    case 3:  return "DRV";
    case 4:  return "ABS_OVER_CURRENT";
    case 5:  return "OVER_TEMP_FET";
    case 6:  return "OVER_TEMP_MOTOR";
    case 7:  return "GATE_DRIVER_OVER_VOLTAGE";
    case 8:  return "GATE_DRIVER_UNDER_VOLTAGE";
    case 9:  return "MCU_UNDER_VOLTAGE";
    case 10: return "BOOTING_FROM_WATCHDOG_RESET";
    case 11: return "ENCODER_SPI_FAULT";
    case 12: return "ENCODER_SINCOS_BELOW_MIN_AMPLITUDE";
    case 13: return "ENCODER_SINCOS_ABOVE_MAX_AMPLITUDE";
    case 14: return "FLASH_CORRUPTION";
    case 15: return "HIGH_OFFSET_CURRENT_SENSOR_1";
    case 16: return "HIGH_OFFSET_CURRENT_SENSOR_2";
    case 17: return "HIGH_OFFSET_CURRENT_SENSOR_3";
    case 18: return "UNBALANCED_CURRENTS";
    default: return "UNKNOWN_" + String(code);
  }
}

static void parseGetValues(const uint8_t *payload, size_t len) {
  if (len < 38) return;
  if (payload[0] != 0x04) return;
  vescStatus.tempFet   = readFloat16(payload + 1,  10.0f);
  vescStatus.tempMotor = readFloat16(payload + 3,  10.0f);
  vescStatus.voltage   = readFloat16(payload + 27, 10.0f);
  vescStatus.faultCode = payload[37];
  vescStatus.connected  = true;
  vescStatus.lastUpdate = millis();
}

static unsigned long lastVescPoll    = 0;
static std::string   vescPollBuffer;

bool webUiActive() {
  return (millis() - lastBrowserPing < 5000);
}

void pollVesc() {
  unsigned long now = millis();

  if (wifiClient && wifiClient.connected()) return;
  if (deviceConnected) return;

  if (!cfg_vesc_poll) return;
  if (!webUiActive()) return;

  if (now - lastVescPoll < 3000) return;
  lastVescPoll = now;

  Serial1.write(VESC_GET_VALUES_PKT, sizeof(VESC_GET_VALUES_PKT));

  unsigned long start = millis();
  vescPollBuffer.clear();
  while (millis() - start < 100) {
    while (Serial1.available()) {
      vescPollBuffer.push_back(Serial1.read());
    }
    if (vescPollBuffer.size() > 5 && vescPollBuffer.back() == 0x03) break;
    delay(2);
  }

  if (vescPollBuffer.size() > 5 && vescPollBuffer[0] == 0x02 && vescPollBuffer.back() == 0x03) {
    uint8_t plen = vescPollBuffer[1];
    if (vescPollBuffer.size() >= (size_t)(plen + 4)) {
      parseGetValues((const uint8_t*)vescPollBuffer.data() + 2, plen);
    }
  } else {
    if (now - vescStatus.lastUpdate > 6000) {
      vescStatus.connected = false;
    }
  }
}

std::string vescBuffer;

void loop() {
  otaServer.handleClient();

  if (isAPMode) {
    dnsServer.processNextRequest();
  }

  pollVesc();

  if (!wifiClient || !wifiClient.connected()) {
    wifiClient = server.available();
    if (wifiClient) {
      wifiClient.setNoDelay(true);
      wifiClient.setTimeout(100);
      Serial.println("WiFi client connected");
    }
  }

  if (wifiClient && wifiClient.connected()) {
    size_t avail = wifiClient.available();
    if (avail > 0) {
      size_t len = wifiClient.readBytes(buf, min(avail, MAX_BUF));
      if (len > 0) {
        Serial.printf("WiFi => VESC: %d bytes\n", len);
        Serial1.write(buf, len);
      }
    }
  }

  if (Serial1.available()) {
    while (Serial1.available()) {
      vescBuffer.push_back(Serial1.read());
      if (vescBuffer.length() > MAX_VESC_BUFFER) {
        Serial.println("WARNING: vescBuffer overflow!");
        vescBuffer.clear();
        break;
      }
    }
    if (vescBuffer.length() > 0) {
      if (deviceConnected) {
        std::string tmp = vescBuffer;
        while (tmp.length() > 0) {
          size_t chunkSize = min(tmp.length(), (size_t)PACKET_SIZE);
          std::string chunk = tmp.substr(0, chunkSize);
          pCharacteristicVescTx->setValue(chunk);
          pCharacteristicVescTx->notify();
          tmp = tmp.substr(chunkSize);
          delay(5);
        }
      }
      if (wifiClient && wifiClient.connected()) {
        size_t written = wifiClient.write((const uint8_t*)vescBuffer.c_str(), vescBuffer.length());
        if (written > 0) Serial.printf("VESC => WiFi: %d bytes\n", written);
        if (written < vescBuffer.length() && !wifiClient.connected()) {
          Serial.println("WiFi client disconnected");
          wifiClient.stop();
        }
      }
      vescBuffer.clear();
    }
  }

  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    pServer->startAdvertising();
    Serial.println("BLE advertising restarted");
    oldDeviceConnected = false;
  }
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = true;
  }

  yield();
}