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

// ── Config vars ──────────────────────────────────────────────────────────────
String cfg_ble_name;
String cfg_ap_ssid;
String cfg_ap_pass;
String cfg_hostname;
int    cfg_port;
bool   cfg_vesc_poll;
int    cfg_ap_timeout;
String cfg_update_url;
String cfg_version_url;
int    cfg_rx_pin;
int    cfg_tx_pin;
bool   cfg_autoreboot         = false;
int    cfg_autoreboot_time    = 300;
bool   cfg_autoreboot_no_wifi = false;
bool   cfg_debug              = false;
int    cfg_log_size           = 50;
int    cfg_debug_filter       = 7; // bitmask: 1=BLE 2=WiFi 4=Poll
bool   cfg_roam_enabled       = false; // RSSI-basiertes Roaming (gleiche SSID, anderer AP)
int    cfg_roam_threshold     = -75;   // ab diesem RSSI (dBm) wird nach besserem AP gesucht
int    cfg_roam_hysteresis    = 12;    // neuer AP muss min. so viele dB staerker sein

struct WiFiEntry {
  String ssid, pass;
  bool   staticIp = false;
  String ip, gateway, subnet, dns;
};
std::vector<WiFiEntry> cfg_wifi;

void loadConfig() {
  prefs.begin("vesccfg", false);
  cfg_ble_name    = prefs.getString("ble_name",    DEFAULT_BLE_NAME);
  cfg_ap_ssid     = prefs.getString("ap_ssid",     DEFAULT_AP_SSID);
  cfg_ap_pass     = prefs.getString("ap_pass",     DEFAULT_AP_PASS);
  cfg_hostname    = prefs.getString("hostname",    DEFAULT_HOSTNAME);
  cfg_port        = prefs.getInt   ("port",        VESC_TCP_PORT);
  cfg_vesc_poll   = prefs.getBool  ("vesc_poll",   true);
  cfg_ap_timeout  = prefs.getInt   ("ap_timeout",  0);
  cfg_update_url  = prefs.getString("update_url",  DEFAULT_UPDATE_URL);
  cfg_version_url = prefs.getString("version_url", DEFAULT_VERSION_URL);
  cfg_rx_pin      = prefs.getInt   ("rx_pin",      VESC_RX_PIN);
  cfg_tx_pin      = prefs.getInt   ("tx_pin",      VESC_TX_PIN);
  cfg_autoreboot         = prefs.getBool("autoreboot",       false);
  cfg_autoreboot_time    = prefs.getInt ("autoreboot_time",  300);
  cfg_autoreboot_no_wifi = prefs.getBool("autoreboot_nowifi",false);
  cfg_debug              = prefs.getBool("debug",            false);
  cfg_log_size           = prefs.getInt ("log_size",         50);
  cfg_debug_filter       = prefs.getInt ("debug_filter",     7);
  cfg_roam_enabled       = prefs.getBool("roam_en",          false);
  cfg_roam_threshold     = prefs.getInt ("roam_thr",         -75);
  cfg_roam_hysteresis    = prefs.getInt ("roam_hyst",        12);
  int count = prefs.getInt("wifi_count", 0);
  cfg_wifi.clear();
  for (int i = 0; i < count && i < MAX_WIFI_NETWORKS; i++) {
    WiFiEntry e;
    e.ssid     = prefs.getString(("wssid"  +String(i)).c_str(), "");
    e.pass     = prefs.getString(("wpass"  +String(i)).c_str(), "");
    e.staticIp = prefs.getBool  (("wstatic"+String(i)).c_str(), false);
    e.ip       = prefs.getString(("wip"    +String(i)).c_str(), "");
    e.gateway  = prefs.getString(("wgw"    +String(i)).c_str(), "");
    e.subnet   = prefs.getString(("wsub"   +String(i)).c_str(), "255.255.255.0");
    e.dns      = prefs.getString(("wdns"   +String(i)).c_str(), "");
    if (e.ssid.length() > 0) cfg_wifi.push_back(e);
  }
  prefs.end();
  if (cfg_ble_name.isEmpty()) cfg_ble_name = DEFAULT_BLE_NAME;
  if (cfg_ap_ssid.isEmpty())  cfg_ap_ssid  = DEFAULT_AP_SSID;
  if (cfg_hostname.isEmpty()) cfg_hostname = DEFAULT_HOSTNAME;
  if (cfg_port <= 0 || cfg_port > 65535) cfg_port = VESC_TCP_PORT;
  if (cfg_rx_pin < 0 || cfg_rx_pin > 48) cfg_rx_pin = VESC_RX_PIN;
  if (cfg_tx_pin < 0 || cfg_tx_pin > 48) cfg_tx_pin = VESC_TX_PIN;
  if (cfg_autoreboot_time < 60) cfg_autoreboot_time = 60;
  if (cfg_log_size < 10)  cfg_log_size = 10;
  if (cfg_log_size > 500) cfg_log_size = 500;
  if (cfg_roam_threshold  > -40) cfg_roam_threshold  = -40;
  if (cfg_roam_threshold  < -90) cfg_roam_threshold  = -90;
  if (cfg_roam_hysteresis < 3)   cfg_roam_hysteresis = 3;
  if (cfg_roam_hysteresis > 30)  cfg_roam_hysteresis = 30;
}

void saveConfig() {
  prefs.begin("vesccfg", false);
  prefs.putString("ble_name",    cfg_ble_name);
  prefs.putString("ap_ssid",     cfg_ap_ssid);
  prefs.putString("ap_pass",     cfg_ap_pass);
  prefs.putString("hostname",    cfg_hostname);
  prefs.putInt   ("port",        cfg_port);
  prefs.putBool  ("vesc_poll",   cfg_vesc_poll);
  prefs.putInt   ("ap_timeout",  cfg_ap_timeout);
  prefs.putString("update_url",  cfg_update_url);
  prefs.putString("version_url", cfg_version_url);
  prefs.putInt   ("rx_pin",      cfg_rx_pin);
  prefs.putInt   ("tx_pin",      cfg_tx_pin);
  prefs.putBool  ("autoreboot",       cfg_autoreboot);
  prefs.putInt   ("autoreboot_time",  cfg_autoreboot_time);
  prefs.putBool  ("autoreboot_nowifi",cfg_autoreboot_no_wifi);
  prefs.putBool  ("debug",       cfg_debug);
  prefs.putInt   ("log_size",    cfg_log_size);
  prefs.putInt   ("debug_filter",cfg_debug_filter);
  prefs.putBool  ("roam_en",     cfg_roam_enabled);
  prefs.putInt   ("roam_thr",    cfg_roam_threshold);
  prefs.putInt   ("roam_hyst",   cfg_roam_hysteresis);
  prefs.putInt   ("wifi_count",  cfg_wifi.size());
  for (int i = 0; i < (int)cfg_wifi.size(); i++) {
    prefs.putString(("wssid"  +String(i)).c_str(), cfg_wifi[i].ssid);
    prefs.putString(("wpass"  +String(i)).c_str(), cfg_wifi[i].pass);
    prefs.putBool  (("wstatic"+String(i)).c_str(), cfg_wifi[i].staticIp);
    prefs.putString(("wip"    +String(i)).c_str(), cfg_wifi[i].ip);
    prefs.putString(("wgw"    +String(i)).c_str(), cfg_wifi[i].gateway);
    prefs.putString(("wsub"   +String(i)).c_str(), cfg_wifi[i].subnet);
    prefs.putString(("wdns"   +String(i)).c_str(), cfg_wifi[i].dns);
  }
  prefs.end();
}

// ── Global state ──────────────────────────────────────────────────────────────
WiFiMulti wifiMulti;

NimBLEServer         *pServer               = nullptr;
NimBLECharacteristic *pCharacteristicVescTx = nullptr;
NimBLECharacteristic *pCharacteristicVescRx = nullptr;

bool deviceConnected    = false;
bool oldDeviceConnected = false;
int  MTU_SIZE           = 128;
int  PACKET_SIZE        = MTU_SIZE - 3;

struct VescStatus {
  bool    connected  = false;
  float   voltage    = 0.0;
  float   tempFet    = 0.0;
  float   tempMotor  = 0.0;
  int     faultCode  = 0;
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
static WebServer  emergencyServer(8080);
static DNSServer  dnsServer;
static bool       isAPMode   = false;
static unsigned long apStartTime = 0;
static bool       apActive   = false;

// ── AP / WiFi resilience state ────────────────────────────────────────────────
// apWanted = "der AP SOLL laufen". Wird einmal beim Start gesetzt und bleibt true
// (AP soll dauerhaft aktiv sein). Der AP-Timeout kann ihn auf false setzen.
static bool          apWanted          = false;
static unsigned long lastApEnsure      = 0;
static unsigned long lastReconnectTry  = 0;
static bool          scanInProgress    = false;
static unsigned long scanStartTime     = 0;
static bool          staWasConnected   = false;

// ── Roaming-State ─────────────────────────────────────────────────────────────
static unsigned long lastRssiCheck     = 0;
static unsigned long weakSince         = 0;     // seit wann RSSI unter Schwelle
static bool          roamScanRunning   = false;
static unsigned long roamScanStart     = 0;
static unsigned long lastRoamSwitch    = 0;

const size_t MAX_BUF         = 256;
const size_t MAX_VESC_BUFFER = 1024;
uint8_t buf[MAX_BUF];

static std::vector<String> uartLog;
static void uartLogAdd(const String &line) {
  if (!cfg_debug) return;
  uartLog.push_back(line);
  while ((int)uartLog.size() > cfg_log_size) uartLog.erase(uartLog.begin());
}

static String vescFaultToString(int code);
bool ensureAP(bool force);
void handleRoaming();

// ── HTML PAGE ─────────────────────────────────────────────────────────────────
static const char PAGE_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>🛴 VESC BLE/WiFi</title>
  <style>
    @font-face{font-family:'Ndot47';src:url(data:font/woff2;base64,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) format('woff2');font-display:swap}
    :root{--bg:#000000;--bg2:#111111;--bg3:#1a1a1a;--border:#222222;--border2:#333333;--text:#e0e0e0;--text2:#aaa;--text3:#666;--accent:#00bcd4;--accent2:#00acc1;--ok:#4caf50;--err:#f44336;--ok-bg:#0a1f0d;--err-bg:#1f0a0a}
    [data-theme=light]{--bg:#f5f5f5;--bg2:#ffffff;--bg3:#ebebeb;--border:#dddddd;--border2:#cccccc;--text:#111111;--text2:#555555;--text3:#999999;--accent:#0288d1;--accent2:#0277bd;--ok:#388e3c;--err:#c62828;--ok-bg:#e8f5e9;--err-bg:#ffebee}
    @media(prefers-color-scheme:light){:root:not([data-theme=dark]){--bg:#f5f5f5;--bg2:#ffffff;--bg3:#ebebeb;--border:#dddddd;--border2:#cccccc;--text:#111111;--text2:#555555;--text3:#999999;--accent:#0288d1;--accent2:#0277bd;--ok:#388e3c;--err:#c62828;--ok-bg:#e8f5e9;--err-bg:#ffebee}}
    *{box-sizing:border-box;margin:0;padding:0}
    body{font-family:'Ndot47',monospace;background:var(--bg);color:var(--text);min-height:100vh;padding:16px}
    .wrap{max-width:600px;margin:0 auto;padding:0 16px}
    h1{color:var(--accent);font-size:18px;margin-bottom:4px}
    .sub{color:var(--text3);font-size:12px;margin-bottom:24px}
    .tabs{display:flex;gap:4px;margin-bottom:16px;flex-wrap:wrap}
    .tab{padding:8px 16px;background:var(--bg2);border:1px solid var(--border);border-radius:6px;cursor:pointer;font-family:'Ndot47',monospace;font-size:13px;color:var(--text2)}
    .tab.active{background:var(--accent);color:#111;border-color:var(--accent)}
    .panel{display:none}.panel.active{display:block}
    .section{background:var(--bg2);border:1px solid var(--border2);border-radius:8px;padding:20px;margin-bottom:12px}
    .section h3{color:var(--accent);font-size:13px;margin-bottom:14px;text-transform:uppercase;letter-spacing:1px}
    label{display:block;font-size:12px;color:var(--text2);margin-bottom:4px;margin-top:10px}
    label:first-of-type{margin-top:0}
    input[type=text],input[type=password]{width:100%;padding:8px 10px;background:var(--bg3);border:1px solid var(--border);border-radius:4px;color:var(--text);font-family:'Ndot47',monospace;font-size:13px}
    input:focus{outline:none;border-color:var(--accent)}
    .checkbox-row{display:flex;align-items:center;gap:10px;margin-top:12px;font-size:13px;color:var(--text2);cursor:pointer}
    .checkbox-row input[type=checkbox]{width:16px;height:16px;accent-color:var(--accent);cursor:pointer}
    .btn{width:100%;padding:11px;background:var(--accent);color:#111;border:none;border-radius:6px;font-family:'Ndot47',monospace;font-size:14px;font-weight:bold;cursor:pointer;margin-top:14px}
    .btn:hover{background:var(--accent2)}.btn.sm{padding:6px 12px;font-size:12px;width:auto;margin-top:0}
    .btn.red{background:var(--err);color:#fff}.btn.red:hover{opacity:0.85}
    .btn.green{background:var(--ok);color:#111}.btn.green:hover{opacity:0.85}
    .btn:disabled{background:var(--border);color:var(--text3);cursor:not-allowed}
    .msg{margin-top:10px;padding:8px 12px;border-radius:4px;font-size:13px;display:none}
    .msg.ok{background:var(--ok-bg);color:var(--ok);display:block}
    .msg.err{background:var(--err-bg);color:var(--err);display:block}
    .wifi-entry{background:var(--bg3);border:1px solid var(--border2);border-radius:6px;padding:12px;margin-bottom:8px}
    .wifi-entry-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:8px}
    .wifi-entry-num{font-size:11px;color:var(--text3)}
    .drop-zone{border:2px dashed var(--border);border-radius:6px;padding:28px;text-align:center;cursor:pointer;margin-bottom:12px}
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
    .lang-btn{position:fixed;top:12px;right:12px;padding:4px 10px;background:var(--bg2);border:1px solid var(--border);border-radius:4px;color:var(--text2);font-family:'Ndot47',monospace;font-size:12px;cursor:pointer}
    .lang-btn:hover{border-color:var(--accent);color:var(--accent)}
    .theme-btn{position:fixed;top:12px;right:56px;padding:4px 10px;background:var(--bg2);border:1px solid var(--border);border-radius:4px;color:var(--text2);font-family:'Ndot47',monospace;font-size:12px;cursor:pointer}
    .theme-btn:hover{border-color:var(--accent);color:var(--accent)}
    .ep{margin-bottom:10px;padding:10px;background:var(--bg3);border-radius:6px;font-size:12px}
    .method{display:inline-block;padding:2px 6px;border-radius:3px;font-weight:bold;margin-right:6px;font-size:11px}
    .method.get{background:#1a3a1a;color:#81c784}.method.post{background:#3a1a1a;color:#e57373}
    .ep .path{color:var(--accent);font-weight:bold}
    .ep a.path{color:var(--accent);font-weight:bold;text-decoration:none}
    .ep a.path:hover{text-decoration:underline}
    .ep .desc{color:var(--text2);margin-top:4px}
    .ep .fields{margin-top:4px;color:var(--text3);font-size:11px}
    .api-h2{color:var(--text2);font-size:12px;margin:14px 0 8px;text-transform:uppercase;letter-spacing:1px}
  </style>
</head>
<body>
<button class="theme-btn" onclick="toggleTheme()" id="themeBtn">☀️</button>
<button class="lang-btn" onclick="toggleLang()" id="langBtn">DE</button>
<div class="wrap">
  <h1>&#x1F6F4; VESC BLE/WiFi</h1>
  <div class="sub" id="statusBar">Loading...</div>
  <div class="tabs">
    <div class="tab active" onclick="showTab('info')">Info</div>
    <div class="tab" onclick="showTab('config')">Config</div>
    <div class="tab" onclick="showTab('ota')">OTA Flash</div>
    <div class="tab" onclick="showTab('api')">API</div>
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
      <input type="password" id="ap_pass" maxlength="64" placeholder="leave empty for open network">
      <label id="lbl-ap-timeout">AP Timeout in seconds (0 = never off)</label>
      <input type="text" id="ap_timeout" maxlength="6" placeholder="0">
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
      <label class="checkbox-row" style="margin-top:8px">
        <input type="checkbox" id="autoreboot" onchange="document.getElementById('autoreboot_opts').style.display=this.checked?'':'none'">
        <span id="lbl-autoreboot">Auto reboot if no client connected</span>
      </label>
      <div id="autoreboot_opts" style="display:none;margin-top:8px">
        <label id="lbl-autoreboot-time">Reboot after (seconds, min 60)</label>
        <input type="text" id="autoreboot_time" maxlength="6" placeholder="300">
        <label class="checkbox-row" style="margin-top:8px">
          <input type="checkbox" id="autoreboot_no_wifi">
          <span id="lbl-autoreboot-nowifi">Reboot even when connected to WiFi (no active VESC client needed)</span>
        </label>
      </div>
    </div>
    <div class="section">
      <h3 id="lbl-roam-title">WiFi Roaming</h3>
      <label class="checkbox-row" style="margin-top:0">
        <input type="checkbox" id="roam_enabled" onchange="document.getElementById('roam_opts').style.display=this.checked?'':'none'">
        <span id="lbl-roam-enabled">Switch to stronger AP with same SSID</span>
      </label>
      <div id="roam_opts" style="display:none;margin-top:8px">
        <label id="lbl-roam-thr">Search threshold (dBm, e.g. -75 — weaker = roam search starts)</label>
        <input type="text" id="roam_threshold" maxlength="4" placeholder="-75">
        <label id="lbl-roam-hyst">Min. improvement (dB) to actually switch</label>
        <input type="text" id="roam_hysteresis" maxlength="3" placeholder="12">
      </div>
    </div>
    <div class="section">
      <h3 id="lbl-update-title">Update Server</h3>
      <label id="lbl-version-url">Version URL (version.txt)</label>
      <input type="text" id="version_url" placeholder="https://...">
      <label id="lbl-firmware-url">Firmware URL (firmware.bin)</label>
      <input type="text" id="update_url" placeholder="https://...">
    </div>
    <div class="section">
      <h3 id="lbl-wifi-title">WiFi Networks</h3>
      <div style="display:flex;gap:8px;margin-bottom:10px">
        <button class="btn sm" onclick="scanWifi()" id="scanBtn">Scan</button>
        <button class="btn green sm" onclick="addWifi()" id="addBtn">+ Manual</button>
      </div>
      <div id="scanResults" style="display:none;margin-bottom:10px"></div>
      <div id="wifiList"></div>
    </div>
    <button class="btn" onclick="saveConfig()" id="saveBtn">Save</button>
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

  <!-- API -->
  <div class="panel" id="tab-api">
    <div class="section">
      <h3>API Reference</h3>
      <div class="api-h2">GET</div>
      <div class="ep"><span class="method get">GET</span><a class="path" href="/api/info" target="_blank">/api/info</a><div class="desc">Device status, VESC data, WiFi/BLE state</div></div>
      <div class="ep"><span class="method get">GET</span><a class="path" href="/api/config" target="_blank">/api/config</a><div class="desc">All configuration values</div></div>
      <div class="ep"><span class="method get">GET</span><a class="path" href="/api/update/status" target="_blank">/api/update/status</a><div class="desc">Firmware version info</div></div>
      <div class="ep"><span class="method get">GET</span><a class="path" href="/api/update/check" target="_blank">/api/update/check</a><div class="desc">Fetch version.txt and compare</div></div>
      <div class="ep"><span class="method get">GET</span><a class="path" href="/api/wifi/scan" target="_blank">/api/wifi/scan</a><div class="desc">Scan WiFi networks</div></div>
      <div class="ep"><span class="method get">GET</span><a class="path" href="/api/uart/log" target="_blank">/api/uart/log</a><div class="desc">UART debug log (requires debug mode)</div></div>
      <div class="ep"><span class="method get">GET</span><a class="path" href="/api/ping" target="_blank">/api/ping</a><div class="desc">Keepalive — activates VESC polling</div></div>
      <div class="api-h2">POST</div>
      <div class="ep"><span class="method post">POST</span><span class="path">/api/config</span><div class="desc">Save config and restart (JSON body)</div></div>
      <div class="ep"><span class="method post">POST</span><span class="path">/api/uart/clear</span><div class="desc">Clear UART debug log</div></div>
      <div class="ep"><span class="method post">POST</span><span class="path">/api/update/install</span><div class="desc">Download and flash from update_url</div></div>
      <div class="ep"><span class="method post">POST</span><span class="path">/api/restart</span><div class="desc">Restart the ESP</div></div>
      <div class="ep"><span class="method post">POST</span><span class="path">/api/factory-reset</span><div class="desc">Clear NVS and restart</div></div>
      <div class="ep"><span class="method post">POST</span><span class="path">/update</span><div class="desc">Manual OTA (multipart/form-data, field: firmware)</div></div>
      <div class="ep"><span class="method post">POST</span><span class="path" style="color:#ffb74d">:8080/update</span><div class="desc">Emergency OTA — always available<br><span style="color:var(--text3)">curl -X POST http://IP:8080/update -F "firmware=@firmware.bin"</span></div></div>
      <div class="api-h2" style="margin-top:16px">Debug</div>
      <div class="section" style="padding:12px">
        <label class="checkbox-row" style="margin-top:0">
          <input type="checkbox" id="debug_toggle" onchange="setDebug(this.checked)">
          <span id="lbl-debug-mode">Debug Mode (UART log)</span>
        </label>
        <div id="debugLogWrap" style="display:none;margin-top:10px">
          <div style="margin-bottom:8px;font-size:12px;color:var(--text2)">
            <label class="checkbox-row" style="margin-top:4px;display:inline-flex;margin-right:12px">
              <input type="checkbox" id="dbg_ble" onchange="updateFilter()"> BLE
            </label>
            <label class="checkbox-row" style="margin-top:4px;display:inline-flex;margin-right:12px">
              <input type="checkbox" id="dbg_wifi" onchange="updateFilter()"> WiFi
            </label>
            <label class="checkbox-row" style="margin-top:4px;display:inline-flex">
              <input type="checkbox" id="dbg_poll" onchange="updateFilter()"> Poll
            </label>
          </div>
          <div style="display:flex;gap:8px;margin-bottom:8px">
            <button class="btn sm" onclick="loadUartLog()">&#x21BB; Refresh</button>
            <button class="btn red sm" onclick="clearUartLog()">Clear</button>
          </div>
          <div id="uartLog" style="background:var(--bg3);border:1px solid var(--border2);border-radius:4px;padding:8px;font-size:11px;max-height:300px;overflow-y:auto;color:var(--text2)">-</div>
        </div>
      </div>
    </div>
  </div>
</div>

<script>
var lang = (document.cookie.match(/lang=([a-z]+)/)||[])[1] || (navigator.language.startsWith('de')?'de':'en');
function de(){return lang==='de';}

function applyTranslations(){
  var s=function(id,en,d){var el=document.getElementById(id);if(el)el.textContent=de()?d:en;};
  var sh=function(id,en,d){var el=document.getElementById(id);if(el)el.innerHTML=de()?d:en;};
  s('lbl-status',           'Status',                                     'Status');
  s('lbl-loading',          'Loading...',                                  'Laden...');
  s('lbl-ble-name',         'BLE Name (visible in VESC Tool)',             'BLE Name (sichtbar in VESC Tool)');
  s('lbl-ap-title',         'Access Point (Fallback)',                     'Access Point (Fallback)');
  s('lbl-ap-name',          'AP Name (SSID)',                              'AP Name (SSID)');
  s('lbl-ap-pass',          'AP Password (leave empty for open network)',   'AP Passwort (leer = offenes Netz)');
  s('lbl-ap-timeout',       'AP Timeout in seconds (0 = never off)',        'AP Timeout in Sekunden (0 = nie aus)');
  s('lbl-conn-title',       'Connection',                                   'Verbindung');
  s('lbl-port',             'TCP Port (default: 65101)',                    'TCP Port (Standard: 65101)');
  s('lbl-vesc-poll',        'Read VESC data (voltage, temp, fault)',        'VESC Daten auslesen (Spannung, Temp, Fault)');
  s('lbl-autoreboot',       'Auto reboot if no client connected',           'Auto-Neustart wenn kein Client verbunden');
  s('lbl-autoreboot-time',  'Reboot after (seconds, min 60)',               'Neustart nach (Sekunden, min 60)');
  s('lbl-autoreboot-nowifi','Reboot even when connected to WiFi (no active VESC client needed)', 'Neustart auch wenn im WLAN (ohne aktiven VESC Client)');
  s('lbl-roam-title',       'WiFi Roaming',                                 'WiFi Roaming');
  s('lbl-roam-enabled',     'Switch to stronger AP with same SSID',         'Zu st\u00e4rkerem AP mit gleicher SSID wechseln');
  s('lbl-roam-thr',         'Search threshold (dBm, e.g. -75 \u2014 weaker = roam search starts)', 'Suchschwelle (dBm, z.B. -75 \u2014 schw\u00e4cher = Roam-Suche startet)');
  s('lbl-roam-hyst',        'Min. improvement (dB) to actually switch',     'Min. Verbesserung (dB) f\u00fcr Wechsel');
  s('lbl-update-title',     'Update Server',                                'Update Server');
  s('lbl-version-url',      'Version URL (version.txt)',                    'Version URL (version.txt)');
  s('lbl-firmware-url',     'Firmware URL (firmware.bin)',                  'Firmware URL (firmware.bin)');
  s('lbl-wifi-title',       'WiFi Networks',                                'WiFi Netzwerke');
  s('scanBtn',              'Scan',                                         'Scan');
  s('addBtn',               '+ Manual',                                     '+ Manuell');
  s('saveBtn',              'Save',                                         'Speichern');
  s('factoryBtn',           'Factory Reset',                                'Werkseinstellungen');
  s('lbl-server-update',    'Server Update',                                'Server Update');
  s('checkBtn',             'Check for Updates',                            'Auf Updates prüfen');
  s('installBtn',           'Install Update',                               'Update installieren');
  s('lbl-manual-flash',     'Manual Flash',                                 'Manueller Flash');
  sh('lbl-drop',            'Drop firmware.bin here<br>or click to select', 'firmware.bin ablegen<br>oder klicken');
  s('uploadBtn',            'Flash',                                        'Flashen');
  s('lbl-debug-mode',       'Debug Mode (UART log)',                        'Debug Modus (UART Log)');
}

function showTab(name){
  document.querySelectorAll('.tab').forEach(function(t,i){t.classList.toggle('active',['info','config','ota','api'][i]===name);});
  document.querySelectorAll('.panel').forEach(function(p){p.classList.remove('active');});
  document.getElementById('tab-'+name).classList.add('active');
  applyTranslations();
  if(name==='info') loadInfo();
  if(name==='config') loadConfig();
  if(name==='ota') loadUpdateStatus();
  if(name==='api') initDebugTab();
}

var theme=(document.cookie.match(/theme=([a-z]+)/)||[])[1]||(window.matchMedia('(prefers-color-scheme:dark)').matches?'dark':'light');
function applyTheme(){document.documentElement.setAttribute('data-theme',theme);var b=document.getElementById('themeBtn');if(b)b.textContent=theme==='dark'?'☀️':'🌙';document.cookie='theme='+theme+';path=/;max-age=31536000';}
function toggleTheme(){theme=theme==='dark'?'light':'dark';applyTheme();}
applyTheme();

function toggleLang(){lang=lang==='de'?'en':'de';document.cookie='lang='+lang+';path=/;max-age=31536000';location.reload();}
document.getElementById('langBtn').textContent=de()?'EN':'DE';
applyTranslations();

function loadInfo(){
  fetch('/api/info').then(function(r){return r.json();}).then(function(d){
    document.getElementById('statusBar').textContent=d.mode==='ap'&&!d.ssid?'AP: '+d.ip:'WiFi: '+d.ssid+' ('+d.ip+')';
    document.getElementById('infoContent').innerHTML=
      '<div class="info-row"><span>BLE Name</span><span class="info-val">'+d.ble_name+'</span></div>'+
      '<div class="info-row"><span>BLE MAC</span><span class="info-val">'+d.ble_mac+'</span></div>'+
      '<div class="info-row"><span>BLE Client</span><span class="info-val">'+( d.ble_connected?(de()?'Verbunden':'Connected'):(de()?'Getrennt':'Disconnected'))+'</span></div>'+
      '<div class="info-row"><span>WiFi Client</span><span class="info-val">'+(d.wifi_client_connected?(de()?'Verbunden':'Connected'):(de()?'Getrennt':'Disconnected'))+'</span></div>'+
      '<div class="info-row"><span>IP</span><span class="info-val">'+d.ip+'</span></div>'+
      (d.mode!=='ap'?'<div class="info-row"><span>SSID</span><span class="info-val">'+d.ssid+'</span></div>':'')+
      (d.mode!=='ap'?'<div class="info-row"><span>RSSI</span><span class="info-val">'+d.rssi+' dBm</span></div>':'')+
      '<div class="info-row"><span>Free RAM</span><span class="info-val">'+(d.heap>=1024?(d.heap/1024).toFixed(1)+' KB':d.heap+' B')+'</span></div>'+
      '<div class="info-row"><span>AP</span><span class="info-val" style="color:'+(d.ap_active?'var(--ok)':'var(--text3)')+'">'+( d.ap_active?(de()?'Aktiv':'Active'):(de()?'Aus':'Off'))+(d.ap_active?' ('+d.ap_ip+')':'')+'</span></div>'+
      (d.ap_active&&d.ap_timeout_remaining>=0?'<div class="info-row"><span>'+(de()?'AP aus in':'AP off in')+'</span><span class="info-val">'+d.ap_timeout_remaining+'s</span></div>':'')+
      '<div class="info-row"><span>TCP Port</span><span class="info-val">'+d.port+'</span></div>'+
      '<div class="info-row"><span>UART</span><span class="info-val">RX=GPIO'+d.rx_pin+' TX=GPIO'+d.tx_pin+'</span></div>'+
      '<div style="margin:10px 0 6px;font-size:11px;color:#666;text-transform:uppercase;letter-spacing:1px">VESC</div>'+
      '<div class="info-row"><span>VESC</span><span class="info-val" style="color:'+(d.vesc_connected?'#81c784':'#e57373')+'">'+(d.vesc_connected?(de()?'Verbunden':'Connected'):(de()?'Nicht verbunden':'Disconnected'))+'</span></div>'+
      '<div class="info-row" style="'+(d.vesc_connected?'':'opacity:0.4')+'"><span>'+(de()?'Spannung':'Voltage')+'</span><span class="info-val">'+d.vesc_voltage+' V</span></div>'+
      '<div class="info-row" style="'+(d.vesc_connected?'':'opacity:0.4')+'"><span>Temp FET</span><span class="info-val">'+d.vesc_temp_fet+' °C</span></div>'+
      '<div class="info-row" style="'+(d.vesc_connected?'':'opacity:0.4')+'"><span>Temp Motor</span><span class="info-val">'+d.vesc_temp_motor+' °C</span></div>'+
      '<div class="info-row" style="'+(d.vesc_connected?'':'opacity:0.4')+'"><span>'+(de()?'Fehlercode':'Fault')+'</span><span class="info-val" style="color:'+(d.vesc_fault===0?'#81c784':'#e57373')+'">'+(d.vesc_fault_str||'OK')+'</span></div>'+
      '<div class="info-row"><span>Uptime</span><span class="info-val">'+d.uptime+'</span></div>'+
      '<div class="info-row"><span>Build</span><span class="info-val">'+d.build+'</span></div>';
  }).catch(function(){document.getElementById('infoContent').innerHTML='<div style="color:#e57373;font-size:13px">'+(de()?'Fehler':'Error')+'</div>';});
}
loadInfo();
setInterval(function(){if(document.getElementById('tab-info').classList.contains('active'))loadInfo();},1000);
setInterval(function(){fetch('/api/ping');},2000);

function loadUpdateStatus(){
  fetch('/api/update/status').then(function(r){return r.json();}).then(function(d){
    document.getElementById('updateInfo').innerHTML=
      '<div class="info-row"><span>'+(de()?'Version':'Version')+'</span><span class="info-val">'+d.current+'</span></div>'+
      (d.available?'<div class="info-row"><span>Server</span><span class="info-val">'+d.available+'</span></div>':'')+
      (d.error?'<div style="color:var(--err);margin-top:6px">'+d.error+'</div>':'')+
      (!d.update_url?'<div style="color:var(--text3);margin-top:6px">'+(de()?'Kein Update-Server':'No update server configured')+'</div>':'');
  }).catch(function(){});
}

function checkUpdate(){
  var msg=document.getElementById('updateMsg'),btn=document.getElementById('checkBtn');
  msg.className='msg';msg.style.display='none';btn.disabled=true;btn.textContent=de()?'Prüfe...':'Checking...';
  fetch('/api/update/check').then(function(r){return r.json();}).then(function(d){
    btn.disabled=false;btn.textContent=de()?'Auf Updates prüfen':'Check for Updates';
    document.getElementById('updateInfo').innerHTML='<div class="info-row"><span>'+(de()?'Aktuell':'Current')+'</span><span class="info-val">'+d.current+'</span></div><div class="info-row"><span>Server</span><span class="info-val">'+d.available+'</span></div>';
    if(d.update_available){msg.textContent=de()?'Neue Version!':'New version!';msg.className='msg ok';document.getElementById('installBtn').style.display='block';}
    else if(d.error){msg.textContent='Error: '+d.error;msg.className='msg err';}
    else{msg.textContent=de()?'Aktuell.':'Up to date.';msg.className='msg ok';document.getElementById('installBtn').style.display='none';}
  }).catch(function(){btn.disabled=false;msg.textContent='Error';msg.className='msg err';});
}

function installUpdate(){
  var msg=document.getElementById('updateMsg'),btn=document.getElementById('installBtn');
  btn.disabled=true;msg.textContent=de()?'Installiere...':'Installing...';msg.className='msg ok';
  fetch('/api/update/install',{method:'POST'}).then(function(){msg.textContent=de()?'Neustart...':'Restarting...';setTimeout(function(){location.reload();},15000);}).catch(function(){msg.textContent='Error';msg.className='msg err';btn.disabled=false;});
}

function scanWifi(){
  var btn=document.getElementById('scanBtn'),res=document.getElementById('scanResults');
  btn.disabled=true;btn.textContent=de()?'Scanne...':'Scanning...';res.style.display='none';
  fetch('/api/wifi/scan').then(function(r){return r.json();}).then(function(nets){
    btn.disabled=false;btn.textContent='Scan';
    if(!nets.length){res.innerHTML='<div style="color:#666;font-size:12px">'+(de()?'Keine':'None found')+'</div>';}
    else{res.innerHTML=nets.sort(function(a,b){return b.rssi-a.rssi;}).map(function(n){return'<div style="display:flex;justify-content:space-between;padding:6px 8px;background:#2a2a2a;border-radius:4px;margin-bottom:4px;font-size:12px;cursor:pointer" onclick="addWifiFromScan(\''+esc(n.ssid)+'\')"><span>'+esc(n.ssid)+' '+(n.secure?'🔒':'')+'</span><span style="color:#666">'+n.rssi+' dBm</span></div>';}).join('');}
    res.style.display='block';
  }).catch(function(){btn.disabled=false;btn.textContent='Scan';});
}

function addWifiFromScan(ssid){
  if(wifiNetworks.length>=10){alert('Max 10');return;}
  wifiNetworks.push({ssid:ssid,pass:'',static:false,ip:'',gateway:'',subnet:'255.255.255.0',dns:''});
  renderWifiList();document.getElementById('scanResults').style.display='none';
  var pw=document.querySelectorAll('#wifiList input[type=password]');if(pw.length)pw[pw.length-1].focus();
}

function factoryReset(){
  if(!confirm(de()?'Zurücksetzen?':'Reset all settings?'))return;
  fetch('/api/factory-reset',{method:'POST'}).catch(function(){});
}

var wifiNetworks=[];
function renderWifiList(){
  var list=document.getElementById('wifiList');
  if(!wifiNetworks.length){list.innerHTML='<div style="color:#666;font-size:12px;padding:8px 0">'+(de()?'Keine Netzwerke':'No networks configured')+'</div>';return;}
  list.innerHTML=wifiNetworks.map(function(n,i){return(
    '<div class="wifi-entry">'+
    '<div class="wifi-entry-header"><span class="wifi-entry-num">'+(de()?'Netzwerk':'Network')+' '+(i+1)+'</span>'+
    '<button class="btn red sm" onclick="removeWifi('+i+')">&#x2715;</button></div>'+
    '<div style="display:grid;grid-template-columns:1fr 1fr;gap:8px">'+
    '<div><label>SSID</label><input type="text" maxlength="32" placeholder="SSID" value="'+esc(n.ssid)+'" onchange="wifiNetworks['+i+'].ssid=this.value"></div>'+
    '<div><label>'+(de()?'Passwort':'Password')+'</label>'+
    '<div style="display:flex;gap:4px">'+
    '<input type="password" id="pw_'+i+'" maxlength="64" value="'+esc(n.pass)+'" onchange="wifiNetworks['+i+'].pass=this.value" style="flex:1;min-width:0">'+
    '<button type="button" onclick="var f=document.getElementById(\'pw_'+i+'\');var show=f.type===\'password\';f.type=show?\'text\':\'password\';this.style.background=show?\'var(--err)\':\' var(--accent)\';this.style.color=\'#111\'" style="padding:0 10px;background:var(--accent);border:1px solid var(--border);border-radius:4px;color:#111;cursor:pointer;font-size:14px;flex-shrink:0">👁</button>'+
    '</div></div></div>'+
    '<label class="checkbox-row" style="margin-top:10px"><input type="checkbox" '+(n.static?'checked':'')+' onchange="wifiNetworks['+i+'].static=this.checked;renderWifiList()">'+(de()?'Statische IP':'Static IP')+'</label>'+
    (n.static?'<div style="display:grid;grid-template-columns:1fr 1fr 1fr 1fr;gap:8px;margin-top:8px">'+
    '<div><label>IP</label><input type="text" maxlength="15" placeholder="192.168.1.100" value="'+esc(n.ip||'')+'" onchange="wifiNetworks['+i+'].ip=this.value"></div>'+
    '<div><label>GW</label><input type="text" maxlength="15" placeholder="192.168.1.1" value="'+esc(n.gateway||'')+'" onchange="wifiNetworks['+i+'].gateway=this.value"></div>'+
    '<div><label>Subnet</label><input type="text" maxlength="15" placeholder="255.255.255.0" value="'+esc(n.subnet||'255.255.255.0')+'" onchange="wifiNetworks['+i+'].subnet=this.value"></div>'+
    '<div><label>DNS</label><input type="text" maxlength="15" placeholder="8.8.8.8" value="'+esc(n.dns||'')+'" onchange="wifiNetworks['+i+'].dns=this.value"></div></div>':'')+
    '</div>');}).join('');
}

function esc(s){return String(s).replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/</g,'&lt;');}
function addWifi(){if(wifiNetworks.length>=10){alert('Max 10');return;}wifiNetworks.push({ssid:'',pass:'',static:false,ip:'',gateway:'',subnet:'255.255.255.0',dns:''});renderWifiList();}
function removeWifi(i){wifiNetworks.splice(i,1);renderWifiList();}

function loadConfig(){
  fetch('/api/config').then(function(r){return r.json();}).then(function(d){
    document.getElementById('ble_name').value    = d.ble_name||'';
    document.getElementById('ap_ssid').value     = d.ap_ssid||'';
    document.getElementById('ap_pass').value     = d.ap_pass||'';
    document.getElementById('ap_timeout').value  = d.ap_timeout||0;
    document.getElementById('vesc_port').value   = d.port||65101;
    document.getElementById('rx_pin').value      = d.rx_pin||6;
    document.getElementById('tx_pin').value      = d.tx_pin||5;
    document.getElementById('vesc_poll').checked = d.vesc_poll!==false;
    document.getElementById('autoreboot').checked = d.autoreboot===true;
    document.getElementById('autoreboot_time').value = d.autoreboot_time||300;
    document.getElementById('autoreboot_no_wifi').checked = d.autoreboot_no_wifi===true;
    document.getElementById('autoreboot_opts').style.display = d.autoreboot?'':'none';
    document.getElementById('roam_enabled').checked = d.roam_enabled===true;
    document.getElementById('roam_threshold').value = d.roam_threshold||-75;
    document.getElementById('roam_hysteresis').value = d.roam_hysteresis||12;
    document.getElementById('roam_opts').style.display = d.roam_enabled?'':'none';
    document.getElementById('version_url').value = d.version_url||'';
    document.getElementById('update_url').value  = d.update_url||'';
    wifiNetworks=(d.wifi||[]).map(function(n){return{ssid:n.ssid||'',pass:n.pass||'',static:n.static||false,ip:n.ip||'',gateway:n.gateway||'',subnet:n.subnet||'255.255.255.0',dns:n.dns||''};});
    renderWifiList();
    markOriginals();
  });
}

function showToast(msg, ok, duration){
  var t=document.getElementById('toast');
  if(!t){t=document.createElement('div');t.id='toast';t.style.cssText='position:fixed;top:12px;left:50%;transform:translateX(-50%);padding:10px 18px;border-radius:6px;font-family:Ndot47,monospace;font-size:13px;z-index:9999;transition:opacity .3s;pointer-events:none';document.body.appendChild(t);}
  t.textContent=msg;
  t.style.background=ok?'var(--ok)':'var(--err)';
  t.style.color=ok?'#111':'#fff';
  t.style.opacity='1';
  clearTimeout(t._hide);
  t._hide=setTimeout(function(){t.style.opacity='0';},duration||3000);
}

// Fields that need reboot
var rebootFields=['ble_name','ap_ssid','ap_pass','ap_timeout','vesc_port','rx_pin','tx_pin'];
function needsReboot(){
  return rebootFields.some(function(id){
    var el=document.getElementById(id);
    return el && el._orig!==undefined && String(el.value)!==String(el._orig);
  });
}

function markOriginals(){
  rebootFields.forEach(function(id){
    var el=document.getElementById(id);
    if(el) el._orig=el.value;
  });
}

function saveConfig(){
  var wifi=wifiNetworks.filter(function(n){return n.ssid.trim().length>0;});
  var reboot=needsReboot();
  var bodyObj={
    ble_name:    document.getElementById('ble_name').value,
    ap_ssid:     document.getElementById('ap_ssid').value,
    ap_pass:     document.getElementById('ap_pass').value,
    ap_timeout:  parseInt(document.getElementById('ap_timeout').value)||0,
    port:        parseInt(document.getElementById('vesc_port').value)||65101,
    rx_pin:      parseInt(document.getElementById('rx_pin').value)||6,
    tx_pin:      parseInt(document.getElementById('tx_pin').value)||5,
    vesc_poll:   document.getElementById('vesc_poll').checked,
    autoreboot:      document.getElementById('autoreboot').checked,
    autoreboot_time: parseInt(document.getElementById('autoreboot_time').value)||300,
    autoreboot_no_wifi: document.getElementById('autoreboot_no_wifi').checked,
    roam_enabled:    document.getElementById('roam_enabled').checked,
    roam_threshold:  parseInt(document.getElementById('roam_threshold').value)||-75,
    roam_hysteresis: parseInt(document.getElementById('roam_hysteresis').value)||12,
    version_url: document.getElementById('version_url').value,
    update_url:  document.getElementById('update_url').value,
    wifi: wifi
  };
  if(!reboot) bodyObj.noreboot=true;
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(bodyObj)})
    .then(function(r){
      if(r.ok){
        if(reboot){
          showToast(de()?'Gespeichert — ESP startet neu...':'Saved — ESP restarting...',true,8000);
          setTimeout(function(){location.reload();},5000);
        } else {
          showToast(de()?'Gespeichert ✓':'Saved ✓',true,3000);
          markOriginals();
        }
      } else {
        showToast(de()?'Fehler beim Speichern':'Error saving',false,4000);
      }
    }).catch(function(){showToast('Connection error',false,4000);});
}

// Debug
function updateFilter(){
  var f=(document.getElementById('dbg_ble').checked?1:0)|(document.getElementById('dbg_wifi').checked?2:0)|(document.getElementById('dbg_poll').checked?4:0);
  fetch('/api/debug?en=1&filter='+f,{method:'POST'});
}
function setDebug(on){
  var f=(document.getElementById('dbg_ble').checked?1:0)|(document.getElementById('dbg_wifi').checked?2:0)|(document.getElementById('dbg_poll').checked?4:0);
  fetch('/api/debug?en='+(on?1:0)+'&filter='+f,{method:'POST'}).then(function(){
    document.getElementById('debugLogWrap').style.display=on?'':'none';
    if(on)loadUartLog();
  });
}
function loadUartLog(){
  fetch('/api/uart/log').then(function(r){return r.json();}).then(function(d){
    var el=document.getElementById('uartLog');
    el.innerHTML=d.length===0?'<span style="color:#666">empty</span>':d.map(function(l){return'<div style="border-bottom:1px solid var(--border);padding:2px 0">'+esc(l)+'</div>';}).join('');
    el.scrollTop=el.scrollHeight;
  });
}
function clearUartLog(){fetch('/api/uart/clear',{method:'POST'}).then(function(){loadUartLog();});}
function initDebugTab(){
  fetch('/api/debug/status').then(function(r){return r.json();}).then(function(d){
    document.getElementById('debug_toggle').checked=d.enabled;
    document.getElementById('dbg_ble').checked  = !!(d.filter & 1);
    document.getElementById('dbg_wifi').checked = !!(d.filter & 2);
    document.getElementById('dbg_poll').checked = !!(d.filter & 4);
    document.getElementById('debugLogWrap').style.display=d.enabled?'':'none';
    if(d.enabled)loadUartLog();
  }).catch(function(){});
}

// OTA
var dropZone=document.getElementById('dropZone'),fileInput=document.getElementById('fileInput');
fileInput.addEventListener('change',function(e){selectFile(e.target.files[0]);});
dropZone.addEventListener('dragover',function(e){e.preventDefault();dropZone.classList.add('dragover');});
dropZone.addEventListener('dragleave',function(){dropZone.classList.remove('dragover');});
dropZone.addEventListener('drop',function(e){e.preventDefault();dropZone.classList.remove('dragover');if(e.dataTransfer.files.length)selectFile(e.dataTransfer.files[0]);});
var selectedFile=null;
function selectFile(file){
  if(!file||!file.name.endsWith('.bin')){document.getElementById('otaStatus').textContent=de()?'Nur .bin!':'Only .bin!';document.getElementById('otaStatus').className='status err';document.getElementById('progressWrap').style.display='block';return;}
  selectedFile=file;document.getElementById('fileName').textContent=file.name+' ('+(file.size/1024).toFixed(1)+' KB)';document.getElementById('uploadBtn').disabled=false;document.getElementById('progressWrap').style.display='none';
}
function startUpload(){
  if(!selectedFile)return;
  document.getElementById('uploadBtn').disabled=true;document.getElementById('progressWrap').style.display='block';
  document.getElementById('progressBar').style.width='0%';document.getElementById('otaStatus').textContent='Uploading...';document.getElementById('otaStatus').className='status';
  var fd=new FormData();fd.append('firmware',selectedFile,selectedFile.name);
  var xhr=new XMLHttpRequest();xhr.open('POST','/update',true);
  xhr.upload.onprogress=function(e){if(e.lengthComputable){var p=Math.round(e.loaded/e.total*100);document.getElementById('progressBar').style.width=p+'%';document.getElementById('otaStatus').textContent='Uploading... '+p+'%';}};
  xhr.onload=function(){if(xhr.status===200){document.getElementById('progressBar').style.width='100%';document.getElementById('otaStatus').textContent=de()?'Fertig! Neustart...':'Done! Restarting...';document.getElementById('otaStatus').className='status ok';setTimeout(function(){location.reload();},7000);}else{document.getElementById('otaStatus').textContent='Error: '+xhr.responseText;document.getElementById('otaStatus').className='status err';document.getElementById('uploadBtn').disabled=false;}};
  xhr.onerror=function(){document.getElementById('otaStatus').textContent='Error';document.getElementById('otaStatus').className='status err';document.getElementById('uploadBtn').disabled=false;};
  xhr.send(fd);
}
</script>
</body>
</html>
)rawliteral";

// ── Web handlers ──────────────────────────────────────────────────────────────
void handleCaptivePortal() {
  otaServer.sendHeader("Location", "http://192.168.4.1/", true);
  otaServer.send(302, "text/plain", "");
}

bool isCaptivePortalRequest() {
  String host = otaServer.hostHeader();
  IPAddress clientIP = otaServer.client().remoteIP();
  if (clientIP[0]==192 && clientIP[1]==168 && clientIP[2]==4)
    return (host != "192.168.4.1" && host != cfg_hostname + ".local");
  return false;
}

void handlePage() {
  if (isCaptivePortalRequest()) { handleCaptivePortal(); return; }
  otaServer.send(200, "text/html", PAGE_HTML);
}

void handleApiInfo() {
  unsigned long up = millis() / 1000;
  String uptime = String(up/3600)+"h "+String((up%3600)/60)+"m "+String(up%60)+"s";
  String json = "{";
  json += "\"ble_name\":\""+cfg_ble_name+"\",";
  json += "\"ble_connected\":"+String(deviceConnected?"true":"false")+",";
  json += "\"ble_mac\":\""+String(NimBLEDevice::getAddress().toString().c_str())+"\",";
  json += "\"wifi_client_connected\":"+String((wifiClient&&wifiClient.connected())?"true":"false")+",";
  json += "\"ap_active\":"+String(apActive?"true":"false")+",";
  if (apActive && cfg_ap_timeout > 0) {
    long r = (long)cfg_ap_timeout - (long)((millis()-apStartTime)/1000);
    json += "\"ap_timeout_remaining\":"+String(max(r,0L))+",";
  } else json += "\"ap_timeout_remaining\":-1,";
  json += "\"ap_ip\":\""+WiFi.softAPIP().toString()+"\",";
  json += "\"heap\":"+String(ESP.getFreeHeap())+",";
  json += "\"uptime\":\""+uptime+"\",";
  json += "\"build\":\""+String(FIRMWARE_VERSION)+" ("+String(__DATE__)+" "+String(__TIME__)+")\",";
  json += "\"port\":"+String(cfg_port)+",";
  json += "\"rx_pin\":"+String(cfg_rx_pin)+",";
  json += "\"tx_pin\":"+String(cfg_tx_pin)+",";
  json += "\"vesc_connected\":"+String(vescStatus.connected?"true":"false")+",";
  json += "\"vesc_voltage\":"+String(vescStatus.voltage,2)+",";
  json += "\"vesc_temp_fet\":"+String(vescStatus.tempFet,1)+",";
  json += "\"vesc_temp_motor\":"+String(vescStatus.tempMotor,1)+",";
  json += "\"vesc_fault\":"+String(vescStatus.faultCode)+",";
  json += "\"vesc_fault_str\":\""+vescFaultToString(vescStatus.faultCode)+"\",";
  if (WiFi.status() != WL_CONNECTED) {
    json += "\"mode\":\"ap\",\"ip\":\""+WiFi.softAPIP().toString()+"\"";
  } else {
    json += "\"mode\":\"client\",\"ip\":\""+WiFi.localIP().toString()+"\",";
    json += "\"ssid\":\""+WiFi.SSID()+"\",\"rssi\":"+String(WiFi.RSSI());
  }
  json += "}";
  otaServer.send(200, "application/json", json);
}

void handleApiConfigGet() {
  String json = "{";
  json += "\"ble_name\":\""+cfg_ble_name+"\",";
  json += "\"ap_ssid\":\""+cfg_ap_ssid+"\",";
  json += "\"ap_pass\":\""+cfg_ap_pass+"\",";
  json += "\"port\":"+String(cfg_port)+",";
  json += "\"vesc_poll\":"+String(cfg_vesc_poll?"true":"false")+",";
  json += "\"ap_timeout\":"+String(cfg_ap_timeout)+",";
  json += "\"rx_pin\":"+String(cfg_rx_pin)+",";
  json += "\"tx_pin\":"+String(cfg_tx_pin)+",";
  json += "\"autoreboot\":"+String(cfg_autoreboot?"true":"false")+",";
  json += "\"autoreboot_time\":"+String(cfg_autoreboot_time)+",";
  json += "\"autoreboot_no_wifi\":"+String(cfg_autoreboot_no_wifi?"true":"false")+",";
  json += "\"roam_enabled\":"+String(cfg_roam_enabled?"true":"false")+",";
  json += "\"roam_threshold\":"+String(cfg_roam_threshold)+",";
  json += "\"roam_hysteresis\":"+String(cfg_roam_hysteresis)+",";
  json += "\"update_url\":\""+cfg_update_url+"\",";
  json += "\"version_url\":\""+cfg_version_url+"\",";
  json += "\"wifi\":[";
  for (int i=0;i<(int)cfg_wifi.size();i++) {
    if (i) json += ",";
    json += "{\"ssid\":\""+cfg_wifi[i].ssid+"\",\"pass\":\""+cfg_wifi[i].pass+"\"";
    json += ",\"static\":"+String(cfg_wifi[i].staticIp?"true":"false");
    json += ",\"ip\":\""+cfg_wifi[i].ip+"\",\"gateway\":\""+cfg_wifi[i].gateway+"\"";
    json += ",\"subnet\":\""+cfg_wifi[i].subnet+"\",\"dns\":\""+cfg_wifi[i].dns+"\"}";
  }
  json += "]}";
  otaServer.send(200, "application/json", json);
}

void handleApiConfigPost() {
  String body = otaServer.arg("plain");
  auto extract = [&](String key) -> String {
    String s = "\""+key+"\":\"";
    int st = body.indexOf(s); if (st<0) return "";
    st += s.length();
    int en = body.indexOf("\"", st);
    return en<0 ? "" : body.substring(st, en);
  };
  auto parseInt2 = [&](String key, int def) -> int {
    String s = "\""+key+"\":";
    int st = body.indexOf(s); if (st<0) return def;
    st += s.length();
    int en = body.indexOf(",", st); if (en<0) en = body.indexOf("}", st); if (en<0) return def;
    int v = body.substring(st, en).toInt(); return v;
  };

  cfg_ble_name    = extract("ble_name");
  cfg_ap_ssid     = extract("ap_ssid");
  cfg_ap_pass     = extract("ap_pass");
  cfg_update_url  = extract("update_url");
  cfg_version_url = extract("version_url");
  cfg_port        = parseInt2("port", VESC_TCP_PORT); if (cfg_port<=0||cfg_port>65535) cfg_port=VESC_TCP_PORT;
  cfg_ap_timeout  = parseInt2("ap_timeout", 0);
  cfg_rx_pin      = parseInt2("rx_pin", VESC_RX_PIN); if (cfg_rx_pin<0||cfg_rx_pin>48) cfg_rx_pin=VESC_RX_PIN;
  cfg_tx_pin      = parseInt2("tx_pin", VESC_TX_PIN); if (cfg_tx_pin<0||cfg_tx_pin>48) cfg_tx_pin=VESC_TX_PIN;
  cfg_vesc_poll          = (body.indexOf("\"vesc_poll\":true") >= 0);
  cfg_autoreboot         = (body.indexOf("\"autoreboot\":true") >= 0);
  cfg_autoreboot_no_wifi = (body.indexOf("\"autoreboot_no_wifi\":true") >= 0);
  cfg_autoreboot_time    = parseInt2("autoreboot_time", 300);
  if (cfg_autoreboot_time < 60) cfg_autoreboot_time = 60;
  cfg_roam_enabled    = (body.indexOf("\"roam_enabled\":true") >= 0);
  cfg_roam_threshold  = parseInt2("roam_threshold", -75);
  cfg_roam_hysteresis = parseInt2("roam_hysteresis", 12);
  if (cfg_roam_threshold  > -40) cfg_roam_threshold  = -40;
  if (cfg_roam_threshold  < -90) cfg_roam_threshold  = -90;
  if (cfg_roam_hysteresis < 3)   cfg_roam_hysteresis = 3;
  if (cfg_roam_hysteresis > 30)  cfg_roam_hysteresis = 30;

  if (cfg_ble_name.isEmpty()) cfg_ble_name = DEFAULT_BLE_NAME;
  if (cfg_ap_ssid.isEmpty())  cfg_ap_ssid  = DEFAULT_AP_SSID;

  cfg_wifi.clear();
  int arrStart = body.indexOf("\"wifi\":[");
  if (arrStart >= 0) {
    String arr = body.substring(arrStart + 8); int pos = 0;
    while (pos < (int)arr.length() && (int)cfg_wifi.size() < MAX_WIFI_NETWORKS) {
      int ob = arr.indexOf('{', pos); if (ob<0) break;
      int oe = arr.indexOf('}', ob); if (oe<0) break;
      String e = arr.substring(ob, oe+1);
      auto ex = [&](String k) -> String {
        String s="\""+k+"\":\""; int st=e.indexOf(s); if(st<0)return "";
        st+=s.length(); int en=e.indexOf("\"",st); return en<0?"":e.substring(st,en);
      };
      String ssid = ex("ssid");
      if (ssid.length() > 0) {
        WiFiEntry w; w.ssid=ssid; w.pass=ex("pass");
        w.staticIp=(e.indexOf("\"static\":true")>=0);
        w.ip=ex("ip"); w.gateway=ex("gateway"); w.subnet=ex("subnet"); w.dns=ex("dns");
        if (w.subnet.isEmpty()) w.subnet="255.255.255.0";
        cfg_wifi.push_back(w);
      }
      pos = oe+1;
    }
  }

  saveConfig();
  bool doReboot = !(otaServer.hasArg("noreboot") || body.indexOf("\"noreboot\":true") >= 0);
  if (!doReboot) {
    // Refresh wifiMulti with new networks without reboot
    wifiMulti = WiFiMulti();
    for (auto &n : cfg_wifi) wifiMulti.addAP(n.ssid.c_str(), n.pass.c_str());
  }
  otaServer.send(200, "text/plain", "OK");
  if (doReboot) { delay(500); ESP.restart(); }
}

void handleOTAUpdate() {
  HTTPUpload &u = otaServer.upload();
  if (u.status==UPLOAD_FILE_START) Update.begin(UPDATE_SIZE_UNKNOWN);
  else if (u.status==UPLOAD_FILE_WRITE) Update.write(u.buf, u.currentSize);
  else if (u.status==UPLOAD_FILE_END) Update.end(true);
}

void handleOTAFinish() {
  if (Update.hasError()) otaServer.send(500,"text/plain",Update.errorString());
  else { otaServer.send(200,"text/plain","OK"); delay(500); ESP.restart(); }
}

void handleApiUpdateCheck() {
  if (WiFi.status()!=WL_CONNECTED){otaServer.send(400,"application/json","{\"error\":\"WiFi only\"}");return;}
  if (cfg_version_url.isEmpty()){otaServer.send(400,"application/json","{\"error\":\"No URL\"}");return;}
  HTTPClient http; WiFiClientSecure sc; sc.setInsecure();
  if (cfg_version_url.startsWith("https")) http.begin(sc,cfg_version_url); else http.begin(cfg_version_url);
  http.setTimeout(8000); http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int code = http.GET();
  if (code==200) {
    String ver=http.getString(); ver.trim();
    updateState.availableVersion=ver; updateState.error="";
    otaServer.send(200,"application/json","{\"current\":\""+String(FIRMWARE_VERSION)+"\",\"available\":\""+ver+"\",\"update_available\":"+(ver!=String(FIRMWARE_VERSION)?"true":"false")+"}");
  } else {
    updateState.error="HTTP "+String(code);
    otaServer.send(500,"application/json","{\"error\":\"HTTP "+String(code)+"\"}");
  }
  http.end();
}

void handleApiUpdateInstall() {
  if (WiFi.status()!=WL_CONNECTED){otaServer.send(400,"text/plain","WiFi only");return;}
  if (cfg_update_url.isEmpty()){otaServer.send(400,"text/plain","No URL");return;}
  otaServer.send(200,"text/plain","OK"); delay(500);
  if (cfg_update_url.startsWith("https")) {
    WiFiClientSecure sc; sc.setInsecure();
    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    httpUpdate.update(sc, cfg_update_url);
  } else { WiFiClient c; httpUpdate.update(c, cfg_update_url); }
}

void setupWebServer() {
  otaServer.on("/",                     HTTP_GET,  handlePage);
  otaServer.on("/api/info",             HTTP_GET,  handleApiInfo);
  otaServer.on("/api/config",           HTTP_GET,  handleApiConfigGet);
  otaServer.on("/api/config",           HTTP_POST, handleApiConfigPost);
  otaServer.on("/api/factory-reset",    HTTP_POST, [](){ prefs.begin("vesccfg",false);prefs.clear();prefs.end();otaServer.send(200,"text/plain","OK");delay(500);ESP.restart(); });
  otaServer.on("/api/wifi/scan",        HTTP_GET,  [](){ int n=WiFi.scanNetworks();String j="[";for(int i=0;i<n;i++){if(i)j+=",";j+="{\"ssid\":\""+WiFi.SSID(i)+"\",\"rssi\":"+String(WiFi.RSSI(i))+",\"secure\":"+String(WiFi.encryptionType(i)!=WIFI_AUTH_OPEN?"true":"false")+"}";}j+="]";WiFi.scanDelete();otaServer.send(200,"application/json",j); });
  otaServer.on("/api/update/status",    HTTP_GET,  [](){ otaServer.send(200,"application/json","{\"current\":\""+String(FIRMWARE_VERSION)+"\",\"available\":\""+updateState.availableVersion+"\",\"update_url\":\""+cfg_update_url+"\",\"version_url\":\""+cfg_version_url+"\",\"error\":\""+updateState.error+"\"}"); });
  otaServer.on("/api/update/check",     HTTP_GET,  handleApiUpdateCheck);
  otaServer.on("/api/update/install",   HTTP_POST, handleApiUpdateInstall);
  otaServer.on("/api/ping",             HTTP_GET,  [](){ lastBrowserPing=millis(); otaServer.send(200,"text/plain","ok"); });
  otaServer.on("/api/restart",          HTTP_POST, [](){ otaServer.send(200,"text/plain","OK");delay(500);ESP.restart(); });
  otaServer.on("/api/debug", HTTP_POST, [](){
    cfg_debug = (otaServer.arg("en") == "1");
    if (otaServer.hasArg("filter")) cfg_debug_filter = otaServer.arg("filter").toInt();
    prefs.begin("vesccfg", false);
    prefs.putBool("debug", cfg_debug);
    prefs.putInt("debug_filter", cfg_debug_filter);
    prefs.end();
    otaServer.send(200, "text/plain", "OK");
  });
  otaServer.on("/api/debug/status", HTTP_GET, [](){ otaServer.send(200,"application/json","{\"enabled\":"+String(cfg_debug?"true":"false")+",\"filter\":"+String(cfg_debug_filter)+"}"); });
  otaServer.on("/api/uart/log",         HTTP_GET,  [](){ String j="[";for(int i=0;i<(int)uartLog.size();i++){if(i)j+=",";j+="\""+uartLog[i]+"\"";}j+="]";otaServer.send(200,"application/json",j); });
  otaServer.on("/api/uart/clear",       HTTP_POST, [](){ uartLog.clear(); otaServer.send(200,"text/plain","OK"); });
  otaServer.on("/update",               HTTP_POST, handleOTAFinish, handleOTAUpdate);
  otaServer.on("/generate_204",         HTTP_GET,  handleCaptivePortal);
  otaServer.on("/gen_204",              HTTP_GET,  handleCaptivePortal);
  otaServer.on("/hotspot-detect.html",  HTTP_GET,  handlePage);
  otaServer.on("/library/test/success.html", HTTP_GET, handlePage);
  otaServer.on("/ncsi.txt",             HTTP_GET,  handleCaptivePortal);
  otaServer.on("/connecttest.txt",      HTTP_GET,  handleCaptivePortal);
  otaServer.on("/redirect",             HTTP_GET,  handleCaptivePortal);
  otaServer.onNotFound([](){ if(isCaptivePortalRequest())handleCaptivePortal();else otaServer.send(404,"text/plain","Not found"); });

  emergencyServer.on("/update", HTTP_POST, [](){
    emergencyServer.sendHeader("Connection","close");
    emergencyServer.send(200,"text/plain",Update.hasError()?"Update failed!":"Update successful. ESP restarting...");
    delay(100); ESP.restart();
  }, [](){
    HTTPUpload &u=emergencyServer.upload();
    if(u.status==UPLOAD_FILE_START)Update.begin(UPDATE_SIZE_UNKNOWN);
    else if(u.status==UPLOAD_FILE_WRITE)Update.write(u.buf,u.currentSize);
    else if(u.status==UPLOAD_FILE_END)Update.end(true);
  });

  otaServer.begin();
  emergencyServer.begin();
  xTaskCreate([](void*){
    for(;;) { emergencyServer.handleClient(); vTaskDelay(1); }
  }, "emergency", 4096, nullptr, 1, nullptr);
}

// ── BLE callbacks ─────────────────────────────────────────────────────────────
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
    MTU_SIZE = MTU; PACKET_SIZE = MTU_SIZE - 3;
  }
};

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string rx = pCharacteristic->getValue();
    if (rx.length()>0 && pCharacteristic->getUUID().equals(pCharacteristicVescRx->getUUID())) {
      if (cfg_debug && (cfg_debug_filter & 1)) { String h="BLE=>VESC: ";for(size_t i=0;i<rx.length();i++){char x[4];snprintf(x,4,"%02X ",(uint8_t)rx[i]);h+=x;} uartLogAdd(h); }
      Serial1.write((const uint8_t*)rx.data(), rx.length());
    }
  }
};

// ── WiFi Event Handler ────────────────────────────────────────────────────────
// Fängt alle relevanten WiFi-Events ab. Der entscheidende Punkt für deinen Bug:
// bei STA_DISCONNECTED darf der AP NICHT mitsterben. Wir setzen den Mode hart
// zurück und ziehen den AP sofort wieder hoch, falls er gefallen ist.
void onWiFiEvent(WiFiEvent_t event) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("[evt] STA connected");
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("[evt] STA got IP: %s\n", WiFi.localIP().toString().c_str());
      staWasConnected = true;
      // Nach erfolgreichem STA-Connect: AP läuft jetzt zwangsweise auf STA-Channel.
      // Sicherstellen, dass der AP-Mode-Bit noch gesetzt ist.
      ensureAP(false);
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      // *** Das ist der kritische Pfad deines Bugs. ***
      // STA hat die Verbindung verloren. Die IDF räumt intern auf — dabei darf
      // der AP NICHT verschwinden. Mode hart auf AP_STA halten und AP prüfen.
      Serial.println("[evt] STA disconnected — protecting AP");
      if (WiFi.getMode() != WIFI_AP_STA) {
        WiFi.mode(WIFI_AP_STA);
      }
      ensureAP(true);   // AP forciert wiederherstellen falls nötig
      staWasConnected = false;
      break;

    case ARDUINO_EVENT_WIFI_AP_START:
      Serial.println("[evt] AP started");
      break;

    case ARDUINO_EVENT_WIFI_AP_STOP:
      // AP wurde gestoppt — wenn wir ihn eigentlich wollen, sofort neu starten.
      Serial.println("[evt] AP stopped");
      if (apWanted) {
        Serial.println("[evt] AP unexpectedly stopped — restarting");
        ensureAP(true);
      }
      break;

    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      Serial.println("[evt] AP: station connected");
      break;

    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      Serial.println("[evt] AP: station disconnected");
      break;

    default:
      break;
  }
}

// ── AP / WiFi helpers ─────────────────────────────────────────────────────────
// ensureAP: garantiert, dass der AP läuft (sofern apWanted == true).
// force == true  -> AP wird in jedem Fall neu konfiguriert (softAP erneut)
// force == false -> AP wird nur (neu) gestartet, wenn er aktuell nicht läuft
bool ensureAP(bool force) {
  if (!apWanted) return false;

  wifi_mode_t mode = WiFi.getMode();
  bool apBitSet  = (mode == WIFI_AP || mode == WIFI_AP_STA);
  bool apIpValid = (WiFi.softAPIP() != IPAddress(0,0,0,0));
  bool apLooksUp = apBitSet && apIpValid;

  if (apLooksUp && !force) {
    apActive = true;
    return true;
  }

  // Mode sicherstellen — niemals AP-Bit verlieren
  if (mode != WIFI_AP_STA) {
    WiFi.mode(WIFI_AP_STA);
    delay(20);
  }

  const char *pass = cfg_ap_pass.length() > 0 ? cfg_ap_pass.c_str() : nullptr;
  // Channel: wenn STA verbunden ist, MUSS der AP auf dessen Channel (HW-Zwang).
  // Sonst Channel 1 als fester Default.
  int ch = (WiFi.status() == WL_CONNECTED) ? WiFi.channel() : 1;
  if (ch < 1 || ch > 13) ch = 1;

  bool ok = WiFi.softAP(cfg_ap_ssid.c_str(), pass, ch, 0, 4);
  if (ok) {
    isAPMode    = true;
    apActive    = true;
    apStartTime = millis();
    Serial.printf("AP (re)started: %s ch=%d ip=%s\n",
                  cfg_ap_ssid.c_str(), ch, WiFi.softAPIP().toString().c_str());
  } else {
    Serial.println("AP start FAILED!");
  }
  return ok;
}

bool setupAccessPoint() {
  Serial.printf("AP: %s\n", cfg_ap_ssid.c_str());
  apWanted = true;            // AP soll dauerhaft laufen
  bool ok = ensureAP(true);   // initial forciert starten
  return ok;
}

// ── WiFi setup ────────────────────────────────────────────────────────────────
bool setupWiFiClient() {
  if (cfg_wifi.empty()) { Serial.println("WiFi: no networks configured"); return false; }
  Serial.println("WiFi Client: connecting...");
  WiFi.mode(WIFI_AP_STA);
  WiFi.setHostname(cfg_hostname.c_str());
  for (auto &n : cfg_wifi) { wifiMulti.addAP(n.ssid.c_str(), n.pass.c_str()); Serial.printf("  + %s\n", n.ssid.c_str()); }
  unsigned long start = millis();
  while (wifiMulti.run(15000) != WL_CONNECTED) {
    if (millis()-start > 17000) {
      Serial.println("WiFi: failed!");
      // WICHTIG: disconnect(false,false) -> Funk bleibt an, Mode bleibt erhalten.
      // Ein nacktes WiFi.disconnect() würde im AP_STA-Modus den AP mitkillen.
      WiFi.disconnect(false, false);
      return false;
    }
    delay(500); Serial.print(".");
  }
  String csid = WiFi.SSID();
  for (auto &n : cfg_wifi) {
    if (n.ssid==csid && n.staticIp && n.ip.length()>0) {
      IPAddress ip,gw,sub;
      if (ip.fromString(n.ip) && gw.fromString(n.gateway) && sub.fromString(n.subnet)) {
        IPAddress dns; if (n.dns.length()>0&&dns.fromString(n.dns)) WiFi.config(ip,gw,sub,dns); else WiFi.config(ip,gw,sub);
      }
      break;
    }
  }
  Serial.printf("\nWiFi: %s | IP: %s | RSSI: %d dBm\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}

// ── VESC parsing ──────────────────────────────────────────────────────────────
static float readFloat16(const uint8_t *buf, float scale) {
  int16_t val = ((int16_t)buf[0]<<8)|buf[1];
  return val / scale;
}

static String vescFaultToString(int code) {
  switch(code) {
    case 0:  return "OK";
    case 1:  return "OVER_VOLTAGE";
    case 2:  return "UNDER_VOLTAGE";
    case 3:  return "DRV";
    case 4:  return "ABS_OVER_CURRENT";
    case 5:  return "OVER_TEMP_FET";
    case 6:  return "OVER_TEMP_MOTOR";
    case 7:  return "GATE_DRIVER_OVER_VOLTAGE";
    case 8:  return "MCU_UNDER_VOLTAGE";
    case 9:  return "BOOTING_FROM_WATCHDOG_RESET";
    case 10: return "GATE_DRIVER_UNDER_VOLTAGE";
    case 11: return "ENCODER_SPI_FAULT";
    case 12: return "ENCODER_SINCOS_BELOW_MIN_AMPLITUDE";
    case 13: return "ENCODER_SINCOS_ABOVE_MAX_AMPLITUDE";
    case 14: return "FLASH_CORRUPTION";
    case 15: return "HIGH_OFFSET_CURRENT_SENSOR_1";
    case 16: return "HIGH_OFFSET_CURRENT_SENSOR_2";
    case 17: return "HIGH_OFFSET_CURRENT_SENSOR_3";
    case 18: return "UNBALANCED_CURRENTS";
    case 19: return "BRK";
    case 20: return "RESOLVER_LOT";
    case 21: return "RESOLVER_DOS";
    case 22: return "RESOLVER_LOS";
    case 23: return "FLASH_CORRUPTION_APP_CFG";
    case 24: return "FLASH_CORRUPTION_MC_CFG";
    case 25: return "ENCODER_NO_MAGNET";
    case 26: return "ENCODER_MAGNET_TOO_STRONG";
    case 27: return "PHASE_FILTER";
    case 28: return "ENCODER_FAULT";
    case 29: return "LV_OUTPUT_FAULT";
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

static unsigned long lastVescPoll = 0;
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
  if (cfg_debug && (cfg_debug_filter & 4)) uartLogAdd("POLL=>VESC: 02 01 04 40 84 03");

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
    if (cfg_debug && (cfg_debug_filter & 4)) { String h="POLL<=VESC: ";for(size_t i=0;i<min(vescPollBuffer.size(),(size_t)40);i++){char x[4];snprintf(x,4,"%02X ",(uint8_t)vescPollBuffer[i]);h+=x;} uartLogAdd(h); }
    if (vescPollBuffer.size() >= (size_t)(plen + 4)) {
      parseGetValues((const uint8_t*)vescPollBuffer.data() + 2, plen);
    }
  } else {
    if (cfg_debug && (cfg_debug_filter & 4)) uartLogAdd("POLL<=VESC: no response ("+String(vescPollBuffer.size())+" bytes)");
    if (now - vescStatus.lastUpdate > 6000) {
      vescStatus.connected = false;
    }
  }
}

// ── WiFi Roaming (gleiche SSID, anderer/staerkerer AP) ────────────────────────
// Prueft periodisch den RSSI der aktuellen Verbindung. Faellt er laenger unter
// die Schwelle, wird ASYNC gescannt; ist ein bekannter AP mit GLEICHER SSID
// deutlich (Hysterese) staerker, wird gezielt auf dessen BSSID umverbunden.
// Komplett non-blocking — friert den Loop nicht ein, AP bleibt aktiv.
void handleRoaming() {
  if (!cfg_roam_enabled) return;
  if (WiFi.status() != WL_CONNECTED) return;
  // Waehrend der normale Reconnect-Scan laeuft, nicht dazwischenfunken.
  if (scanInProgress) return;
  // Nach einem Wechsel 20s Ruhe, damit sich die Verbindung stabilisiert.
  if (lastRoamSwitch != 0 && millis() - lastRoamSwitch < 20000) return;

  unsigned long now = millis();

  // ── Phase A: RSSI ueberwachen ──
  if (!roamScanRunning) {
    if (now - lastRssiCheck < 3000) return;   // alle 3s pruefen
    lastRssiCheck = now;

    int rssi = WiFi.RSSI();
    if (rssi >= cfg_roam_threshold || rssi == 0) {
      // Signal ok (oder ungueltig) -> Timer zuruecksetzen
      weakSince = 0;
      return;
    }
    // Signal zu schwach
    if (weakSince == 0) { weakSince = now; return; }
    if (now - weakSince < 15000) return;      // erst nach 15s anhaltender Schwaeche

    // Schwelle laenger unterschritten -> Roam-Scan anstossen (ASYNC)
    if (cfg_debug && (cfg_debug_filter & 2))
      uartLogAdd("ROAM: RSSI "+String(rssi)+" dBm low -> scanning");
    Serial.printf("ROAM: weak signal %d dBm, scanning for better AP\n", rssi);
    WiFi.scanNetworks(true, false);           // async, sichtbare Netze
    roamScanRunning = true;
    roamScanStart   = now;
    return;
  }

  // ── Phase B: Roam-Scan-Ergebnis auswerten ──
  int16_t res = WiFi.scanComplete();
  if (res == WIFI_SCAN_RUNNING) {
    if (now - roamScanStart > 15000) {        // Timeout
      WiFi.scanDelete();
      roamScanRunning = false;
    }
    return;
  }

  // Scan fertig
  String   curSsid  = WiFi.SSID();
  int      curRssi  = WiFi.RSSI();
  uint8_t *curBssid = WiFi.BSSID();           // MAC des aktuell verbundenen AP

  int     bestIdx   = -1;
  int     bestRssi  = -127;
  if (res > 0) {
    for (int i = 0; i < res; i++) {
      if (WiFi.SSID(i) != curSsid) continue;  // nur gleiche SSID
      uint8_t *b = WiFi.BSSID(i);
      bool sameAsCurrent = (curBssid && b &&
        memcmp(b, curBssid, 6) == 0);
      if (sameAsCurrent) continue;            // aktueller AP -> ignorieren
      if (WiFi.RSSI(i) > bestRssi) {
        bestRssi = WiFi.RSSI(i);
        bestIdx  = i;
      }
    }
  }

  bool doSwitch = false;
  uint8_t targetBssid[6];
  int     targetChannel = 0;
  if (bestIdx >= 0) {
    // Nur wechseln, wenn der andere AP DEUTLICH staerker ist (Hysterese).
    if (bestRssi - curRssi >= cfg_roam_hysteresis) {
      uint8_t *b = WiFi.BSSID(bestIdx);
      if (b) { memcpy(targetBssid, b, 6); targetChannel = WiFi.channel(bestIdx); doSwitch = true; }
    }
  }

  if (doSwitch) {
    Serial.printf("ROAM: switching AP  cur=%d dBm -> new=%d dBm  (ch %d)\n",
                  curRssi, bestRssi, targetChannel);
    if (cfg_debug && (cfg_debug_filter & 2)) {
      char m[32];
      snprintf(m, sizeof(m), "%02X:%02X:%02X:%02X:%02X:%02X",
               targetBssid[0],targetBssid[1],targetBssid[2],
               targetBssid[3],targetBssid[4],targetBssid[5]);
      uartLogAdd("ROAM: -> "+String(m)+" "+String(bestRssi)+" dBm");
    }
    WiFi.scanDelete();
    roamScanRunning = false;
    weakSince       = 0;

    // Passwort der aktuellen SSID aus der Config holen.
    String pw = "";
    for (auto &w : cfg_wifi) { if (w.ssid == curSsid) { pw = w.pass; break; } }

    // Mode sichern — der AP darf beim Umverbinden nicht fallen.
    if (WiFi.getMode() != WIFI_AP_STA) WiFi.mode(WIFI_AP_STA);

    // Gezielt auf die bessere BSSID verbinden.
    WiFi.disconnect(false, false);            // Funk an, Config behalten
    delay(20);
    WiFi.begin(curSsid.c_str(), pw.c_str(), targetChannel, targetBssid, true);

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) {
      delay(100);
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("ROAM: connected to better AP, %d dBm\n", WiFi.RSSI());
      // statische IP ggf. erneut setzen
      for (auto &w : cfg_wifi) {
        if (w.ssid == curSsid && w.staticIp && w.ip.length() > 0) {
          IPAddress ip,gw,sub;
          if (ip.fromString(w.ip)&&gw.fromString(w.gateway)&&sub.fromString(w.subnet)) {
            IPAddress dns;
            if (w.dns.length()>0&&dns.fromString(w.dns)) WiFi.config(ip,gw,sub,dns);
            else WiFi.config(ip,gw,sub);
          }
          break;
        }
      }
    } else {
      Serial.println("ROAM: switch failed — falling back to wifiMulti");
      wifiMulti.run(8000);                    // Notfall: irgendeinen AP nehmen
    }
    lastRoamSwitch = millis();
    ensureAP(false);                          // AP nach dem Wechsel absichern
    NimBLEDevice::startAdvertising();
  } else {
    // Kein lohnender Wechsel gefunden.
    if (cfg_debug && (cfg_debug_filter & 2))
      uartLogAdd("ROAM: no better AP found");
    WiFi.scanDelete();
    roamScanRunning = false;
    // weakSince NICHT zuruecksetzen: bei weiterhin schwachem Signal soll in
    // 15s erneut gesucht werden (vielleicht ist man dann naeher am 2. AP).
    weakSince = millis() - 15000 + 8000;      // naechster Versuch in ~8s
  }
}

// ── WiFi reconnect (non-blocking) ─────────────────────────────────────────────
// Ersetzt den alten blockierenden Reconnect-Block. Der Scan bleibt erhalten
// (du willst ihn behalten), läuft aber ASYNC: scanNetworks(true,...) blockiert
// den Loop nicht mehr -> AP sendet durchgehend Beacons.
void handleWiFiReconnect() {
  if (cfg_wifi.empty()) return;
  if (WiFi.status() == WL_CONNECTED) {
    // verbunden -> evtl. laufenden Scan aufräumen
    if (scanInProgress) {
      int16_t r = WiFi.scanComplete();
      if (r != WIFI_SCAN_RUNNING) { WiFi.scanDelete(); scanInProgress = false; }
    }
    return;
  }

  unsigned long now = millis();

  // Phase 1: kein Scan läuft -> alle 10s einen ASYNC-Scan anstoßen
  if (!scanInProgress) {
    if (now - lastReconnectTry < 10000) return;
    lastReconnectTry = now;
    // async = true, hidden = true. Blockiert NICHT.
    WiFi.scanNetworks(true, true);
    scanInProgress = true;
    scanStartTime  = now;
    return;
  }

  // Phase 2: Scan läuft -> Ergebnis pollen (ohne zu blockieren)
  int16_t res = WiFi.scanComplete();

  if (res == WIFI_SCAN_RUNNING) {
    // noch nicht fertig — Sicherheits-Timeout 15s
    if (now - scanStartTime > 15000) {
      WiFi.scanDelete();
      scanInProgress = false;
    }
    return;
  }

  // Scan fertig (res >= 0) oder Fehler (res == WIFI_SCAN_FAILED)
  bool found = false;
  if (res > 0) {
    for (int i = 0; i < res && !found; i++) {
      for (auto &w : cfg_wifi) {
        if (WiFi.SSID(i) == w.ssid) { found = true; break; }
      }
    }
  }
  WiFi.scanDelete();
  scanInProgress = false;

  if (found) {
    Serial.println("WiFi: known network found, connecting...");
    // Mode sicherstellen — Connect darf den AP nicht abwerfen
    if (WiFi.getMode() != WIFI_AP_STA) WiFi.mode(WIFI_AP_STA);
    // wifiMulti.run() macht intern ein disconnect+connect. Der AP überlebt das,
    // weil wir den Mode danach im Event-Handler / per ensureAP() schützen.
    if (wifiMulti.run(8000) == WL_CONNECTED) {
      String csid = WiFi.SSID();
      for (auto &w : cfg_wifi) {
        if (w.ssid==csid && w.staticIp && w.ip.length()>0) {
          IPAddress ip,gw,sub;
          if (ip.fromString(w.ip)&&gw.fromString(w.gateway)&&sub.fromString(w.subnet)) {
            IPAddress dns; if (w.dns.length()>0&&dns.fromString(w.dns)) WiFi.config(ip,gw,sub,dns); else WiFi.config(ip,gw,sub);
          }
          break;
        }
      }
      Serial.printf("WiFi connected: %s | IP: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
      NimBLEDevice::startAdvertising();
    }
    // Egal ob connect geklappt hat: AP-Zustand wiederherstellen.
    ensureAP(false);
  }
}

// ── Setup ─────────────────────────────────────────────────────────────────────
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

  // WiFi-Event-Handler registrieren BEVOR WiFi gestartet wird.
  WiFi.onEvent(onWiFiEvent);
  // Auto-Reconnect der IDF ausschalten — wir steuern Reconnect selbst und
  // kontrolliert, damit der AP dabei nie unbeabsichtigt fällt.
  WiFi.setAutoReconnect(false);
  // Mode von Anfang an AP_STA, damit AP und STA koexistieren.
  WiFi.mode(WIFI_AP_STA);

  // AP IMMER zuerst starten — er soll dauerhaft laufen, unabhängig von STA.
  setupAccessPoint();

  // Danach STA verbinden (blockierend beim Boot, das ist ok).
  bool staOK = setupWiFiClient();
  (void)staOK;

  // AP nach STA-Connect nochmal absichern (STA-Connect kann Channel ändern).
  ensureAP(false);

  setupWebServer();
  IPAddress apIP  = WiFi.softAPIP();
  IPAddress staIP = WiFi.localIP();
  Serial.printf("Web (AP):  http://%s/\n", apIP.toString().c_str());
  if (staIP[0] != 0) Serial.printf("Web (STA): http://%s/\n", staIP.toString().c_str());

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", WiFi.softAPIP());

  server = WiFiServer(cfg_port);
  server.begin();
  server.setNoDelay(true);
  Serial.printf("VESC TCP: port %d\n", cfg_port);

  NimBLEDevice::startAdvertising();
  Serial.printf("Free heap after init: %d bytes\n", ESP.getFreeHeap());
  Serial.println("=== Ready ===\n");
}

// ── Loop ──────────────────────────────────────────────────────────────────────
std::string vescBuffer;

void loop() {
  otaServer.handleClient();
  dnsServer.processNextRequest();

  // AP-Timeout: schaltet den AP gezielt ab (apWanted = false), wenn konfiguriert
  // und niemand am AP hängt. Danach hält der Watchdog ihn NICHT mehr am Leben.
  if (apActive && cfg_ap_timeout > 0) {
    if (millis() - apStartTime > (unsigned long)cfg_ap_timeout * 1000UL) {
      if (WiFi.softAPgetStationNum() == 0) {
        Serial.println("AP timeout — shutting down");
        apWanted = false;
        WiFi.softAPdisconnect(false);   // false -> Funk/STA bleibt an
        apActive = false;
        isAPMode = false;
      }
    }
  }

  // AP-Watchdog: prüft regelmäßig, ob der AP noch lebt, und zieht ihn bei
  // Bedarf wieder hoch. Greift nur, solange apWanted == true.
  if (apWanted && millis() - lastApEnsure > 5000) {
    lastApEnsure = millis();
    wifi_mode_t mode = WiFi.getMode();
    bool apBitSet  = (mode == WIFI_AP || mode == WIFI_AP_STA);
    bool apIpValid = (WiFi.softAPIP() != IPAddress(0,0,0,0));
    if (!apBitSet || !apIpValid) {
      Serial.println("AP watchdog: AP down — restoring");
      ensureAP(true);
    } else {
      apActive = true;
    }
  }

  // Non-blocking WiFi-Reconnect (async Scan, friert den Loop nicht ein)
  handleWiFiReconnect();

  // RSSI-basiertes Roaming: zu staerkerem AP gleicher SSID wechseln
  handleRoaming();

  // Auto reboot
  if (cfg_autoreboot && cfg_autoreboot_time > 0) {
    static unsigned long lastConnected = millis();
    bool anyConnected = deviceConnected || (wifiClient && wifiClient.connected());
    if (!cfg_autoreboot_no_wifi && WiFi.status() == WL_CONNECTED) anyConnected = true;
    if (WiFi.softAPgetStationNum() > 0) anyConnected = true;
    if (anyConnected) lastConnected = millis();
    else if (millis() - lastConnected > (unsigned long)cfg_autoreboot_time * 1000UL) {
      Serial.println("Auto reboot: no client connected");
      delay(500);
      ESP.restart();
    }
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
        if (cfg_debug && (cfg_debug_filter & 2)) { String h="WiFi=>VESC: ";for(size_t i=0;i<len;i++){char x[4];snprintf(x,4,"%02X ",buf[i]);h+=x;} uartLogAdd(h); }
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
      if (cfg_debug && (cfg_debug_filter & 2)) { String h="VESC=>: ";for(size_t i=0;i<min(vescBuffer.length(),(size_t)40);i++){char x[4];snprintf(x,4,"%02X ",(uint8_t)vescBuffer[i]);h+=x;} uartLogAdd(h); }
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
      // Parse VESC values from bridge traffic too
      if (cfg_vesc_poll && vescBuffer.size() > 5 && (uint8_t)vescBuffer[0]==0x02 && (uint8_t)vescBuffer.back()==0x03) {
        uint8_t plen = (uint8_t)vescBuffer[1];
        if (vescBuffer.size() >= (size_t)(plen+4)) parseGetValues((const uint8_t*)vescBuffer.data()+2, plen);
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
  if (deviceConnected && !oldDeviceConnected) oldDeviceConnected = true;

  yield();
}