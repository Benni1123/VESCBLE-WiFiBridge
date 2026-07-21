#pragma once

// Zentrale Definitionen fuer den Unity-Build der Bridge.
// main.cpp bindet die Modul-.cpp-Dateien in genau einer Translation Unit ein.

#include <Arduino.h>
#include <vector>
#include <string>
#include <cstdarg>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Preferences.h>
#include <nvs_flash.h>        // komplette NVS-Partition loeschen (Factory-Reset)
#include <esp_system.h>        // esp_reset_reason()
#include <esp_sleep.h>         // Deep-Sleep-Wakeup-Ursache
#include <esp_attr.h>          // RTC_NOINIT_ATTR fuer geplante Neustart-Marker
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_wifi.h>          // esp_wifi_set_config (Beacon-Intervall, AP-Feintuning)
#include <esp_netif.h>         // DHCP-Server stoppen/konfigurieren/starten + Lease-Option
#include <esp_wifi_ap_get_sta_list.h> // IDF 5: IP eines am AP verbundenen Clients (ersetzt esp_netif_sta_list.h)
#include <WiFiAP.h>
#include <WiFiMulti.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Update.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include "version.h"
#include "leds.h"

#define DEFAULT_BLE_NAME  "VESC-BLE-WiFi"
#define DEFAULT_AP_SSID   "VESC-BLE-WiFi"
#define DEFAULT_AP_PASS   ""
#define DEFAULT_HOSTNAME  "vesc-ble-wifi"
#define DEFAULT_UPDATE_URL  "https://github.com/Benni1123/VESCBLE-WiFiBridge/releases/latest/download/firmware.bin"
#define DEFAULT_VERSION_URL "https://github.com/Benni1123/VESCBLE-WiFiBridge/releases/latest/download/version.txt"

// Feste, bewaehrte oeffentliche DNS-Server als Fallback. Werden als sekundaerer
// (und ggf. primaerer) DNS gesetzt, falls der vom Nutzer hinterlegte DNS nicht
// antwortet. So bleibt z.B. der Update-Check funktionsfaehig, auch wenn der
// gewuenschte DNS gerade zickt. Reihenfolge: Google, dann Cloudflare.
#define FALLBACK_DNS_PRIMARY   IPAddress(8,8,8,8)
#define FALLBACK_DNS_SECONDARY IPAddress(1,1,1,1)

// AP-IP der Bridge. Bewusst NICHT der uebliche Default 192.168.4.1 (den nutzen
// viele ESP-Projekte und Geraete -> Kollisionsgefahr). 192.168.9.1 ist selten
// belegt und kollidiert nicht mit den 10er-Netzen im Setup. Subnetz 255.255.255.0.
#define AP_IP      IPAddress(192, 168, 9, 1)
#define AP_GATEWAY IPAddress(192, 168, 9, 1)
#define AP_SUBNET  IPAddress(255, 255, 255, 0)


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
int    cfg_debug_filter       = 7; // bitmask: 1=BLE 2=WiFi 4=Poll 8=Status(BT/WLAN)
bool   cfg_roam_enabled       = false; // RSSI-basiertes Roaming (gleiche SSID, anderer AP)
int    cfg_roam_threshold     = -75;   // ab diesem RSSI (dBm) wird nach besserem AP gesucht
int    cfg_roam_hysteresis    = 12;    // neuer AP muss min. so viele dB staerker sein
// Auto-Poll: pollt VESC unabhaengig von Web-UI
bool   cfg_autopoll_enabled   = false;
int    cfg_autopoll_interval  = 5;     // Sekunden zwischen Polls (1-60)
// BLE-Modus: 0=Aus, 1=An, 2=Auto (an bei Bewegung, aus nach Timeout)
int    cfg_ble_mode           = 1;     // Default: An (Verhalten wie bisher)
int    cfg_ble_auto_erpm_on   = 200;   // |ERPM| > diesem Wert -> BLE/AP an, Timer reset
// AP-Modus: 1=An (immer an), 2=Auto (an bei Bewegung, aus nach Idle-Timeout).
// Bewusst KEIN "Aus" — der AP ist der Fallback-Zugang und darf nicht komplett weg.
int    cfg_ap_mode            = 1;     // Default: An (Verhalten wie bisher)
// Wurde jemals eine Konfiguration gespeichert? Nach Werksreset ist NVS leer ->
// false. Solange false, gilt der ESP als "unkonfiguriert" und der AP wird hart
// erzwungen (Sicherheit: nach Reset niemals AP aus). Wird beim ersten Speichern
// true.
bool   cfg_configured         = false;
// BLE-PIN-Pairing (optional): wenn aktiv, verlangt der ESP beim Koppeln die
// Eingabe dieses 6-stelligen Passkeys (MITM-Schutz). Ohne Haken: Just Works
// (Kopplung wird automatisch angenommen, wie bisher).
bool   cfg_ble_pin_enabled    = false;
int    cfg_ble_pin            = 123456;   // 6-stellig, 000000..999999
int    cfg_ble_auto_off_sec   = 120;   // nach X Sekunden ohne Bewegung & Client -> BLE aus
// BLE-Energiesparmodus (Advertising-Drosselung bei Idle) deaktivieren:
// true = volle Leistung — das Advertising bleibt dauerhaft im schnellen
// Intervall (20-40ms), wie in fruehen Firmware-Versionen. Der ESP ist damit
// jederzeit sofort auffindbar/verbindbar, das kostet aber WLAN-Airtime
// (BLE und WiFi teilen sich EIN 2,4-GHz-Radio).
bool   cfg_ble_full_power     = false;
bool   cfg_leds_enabled       = false; // WS28XX LED-Steuerung aktiv (zeigt LED-Reiter + /leds)

struct WiFiEntry {
  String ssid, pass;
  bool   staticIp = false;
  String ip, gateway, subnet, dns;
};
std::vector<WiFiEntry> cfg_wifi;

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
  int32_t erpm       = 0;   // electrical RPM (signed, roh, kein Skalierungsfaktor)
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
static unsigned long apLastClientGone = 0;   // Zeitpunkt an dem das letzte Geraet sich vom AP trennte
static int           apLastStationNum = 0;   // letzte bekannte Anzahl AP-Clients
static bool          apOffByTimeout   = false; // AP wurde durch Timeout abgeschaltet (fuer Wake-on-Move)
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
// Fuer den automatischen WLAN-Stack-Neustart bei hartnaeckigem Haenger:
// Zeitpunkt, seit dem wir durchgehend KEIN bekanntes Netz verbinden konnten.
static unsigned long staDownSince      = 0;     // 0 = aktuell verbunden/ok
static const unsigned long STA_HARD_RESTART_MS = 150000;  // 2,5 Min

// Exponentielles Backoff fuer die STA-Suche. Jeder Scan stoert den AP kurz
// (die Funkeinheit huepft durch alle Kanaele -> AP in der Zeit nicht erreichbar).
// Unterwegs (kein Heimnetz in Reichweite) wuerde staendiges Scannen den AP
// dauernd stoeren. Darum: findet ein Scan KEIN bekanntes Netz, wird der Abstand
// bis zum naechsten Scan verdoppelt (bis STA_SCAN_MAX_MS). Sobald ein Netz
// gefunden wird, zurueck auf das Minimum -> heimkommen wird zuegig erkannt.
static const unsigned long STA_SCAN_MIN_MS = 10000;   // 10 s (Start/nach Fund)
static const unsigned long STA_SCAN_MAX_MS = 60000;   // 60 s (unterwegs)
static unsigned long staScanInterval  = STA_SCAN_MIN_MS;

// Non-blocking STA-Verbindungsaufbau: statt bis zu 8s zu blockieren (in denen der
// AP steht), wird die Verbindung angestossen und der Status in den folgenden
// Loop-Durchlaeufen gepollt. Der AP laeuft dabei ununterbrochen weiter.
static bool          staConnecting     = false;   // Verbindung laeuft gerade
static unsigned long staConnectStart   = 0;       // Startzeitpunkt des Versuchs
static int           staConnectFails   = 0;       // fehlgeschlagene Versuche
static const unsigned long STA_CONNECT_TIMEOUT_MS = 8000;

// ── Diagnose-Zaehler (im Info-Tab der WebUI ablesbar) ─────────────────────────
// Damit laesst sich ohne Serial-Log (ESP im Scooter) sehen, was im Funk-Betrieb
// passiert: wie oft gescannt/verbunden/getrennt wird, wie oft der AP-Watchdog
// eingreift, wie lange der laengste Loop-Durchlauf war (Blockade-Indikator) und
// der Speicher-Tiefstand. Zusammen ergeben sie ein Bild statt Raterei.
static uint32_t      diagScanCount       = 0;   // gestartete STA-Scans
static uint32_t      diagStaConnects     = 0;   // erfolgreiche STA-Verbindungen
static uint32_t      diagStaDisconnects  = 0;   // STA-Verbindungsabbrueche
static uint32_t      diagApClientConn    = 0;   // AP-Client-Verbindungen (Handy)
static uint32_t      diagApClientDisc    = 0;   // AP-Client-Trennungen
static uint32_t      diagApWatchdogFires = 0;   // AP-Watchdog-Eingriffe (SSID weg)
static uint8_t       diagLastDiscReason  = 0;   // letzter STA-Disconnect-Grund
static unsigned long diagMaxLoopUs       = 0;   // laengster Loop-Durchlauf (us)
static unsigned long diagLoopWindowStart = 0;   // Fenster-Start fuer Loop-Freq
static uint32_t      diagLoopWindowCount = 0;   // Loops im aktuellen Fenster
static uint32_t      diagLoopsPerSec     = 0;   // zuletzt gemessene Loop-Frequenz
static uint32_t      diagMinHeap         = 0xFFFFFFFF;  // niedrigster freier Heap
static uint32_t      diagProbeReqs       = 0;   // empfangene Probe-Requests (Handy funkt AP an)
static int           diagLastProbeRssi   = 0;   // RSSI des letzten Probe-Requests

// Nachlauf-Sperre: nachdem der letzte AP-Client weg ist, wird noch fuer
// STA_SUPPRESS_AFTER_AP_MS KEIN STA-Scan gestartet. Verhindert, dass direkt
// nach dem Trennen sofort wieder ein storender Scan kommt (und gibt einem
// direkt erneut verbindenden Geraet Ruhe). 0 = keine Sperre aktiv.
static unsigned long apClientGoneAt        = 0;
static const unsigned long STA_SUPPRESS_AFTER_AP_MS = 20000;   // 20 s Nachlauf

// ── Roaming-State ─────────────────────────────────────────────────────────────
static unsigned long lastRssiCheck     = 0;
static unsigned long weakSince         = 0;     // seit wann RSSI unter Schwelle
static bool          roamScanRunning   = false;
static unsigned long roamScanStart     = 0;
static unsigned long lastRoamSwitch    = 0;

const size_t MAX_BUF         = 256;
const size_t MAX_VESC_BUFFER = 1024;
uint8_t buf[MAX_BUF];




// Moduluebergreifende Vorwaertsdeklarationen.
static String vescFaultToString(int code);
bool ensureAP(bool force);
void handleRoaming();
void handleBleMode();
bool webUiActive();