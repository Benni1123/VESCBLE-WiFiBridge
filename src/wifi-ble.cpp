// WiFi, Access Point und BLE
// Diese Datei wird ueber main.cpp als Unity-Build eingebunden.
// Dadurch bleiben die bisherigen static-Sichtbarkeiten und Abhaengigkeiten exakt erhalten,
// waehrend der Quellcode logisch in einzelne Dateien aufgeteilt ist.
#if defined(VESC_BRIDGE_UNITY_BUILD)
#include "globals.h"
#include "config.h"
#include "debuglog.h"
#include "time-service.h"
#include "wifi-ble.h"
#include <NimBLEBondMigration.h>   // einmalige Bond-Konvertierung 1.x -> 2.x (vor init!)

// Interne Initialisierungsfunktion des Arduino-Cores (in 3.3.9 verifiziert:
// WiFiGeneric.cpp, externe Linkage, identischer Ablauf wie in 2.x). Sie
// initialisiert Netzwerk-Grundgeruest + WiFi-Treiber, startet den Funk aber
// noch NICHT. Genau dieses Zeitfenster brauchen wir, um die korrekte AP-SSID
// VOR dem ersten Beacon in den Treiber zu schreiben. Danach startet
// WiFi.mode() den Treiber ganz normal (Netif-Erzeugung uebernehmen in Core 3.x
// STA.onEnable()/AP.onEnable() innerhalb von mode()) und haelt seinen internen
// Status konsistent.
bool wifiLowLevelInit(bool persistent);


// Wendet die BLE-Security-Einstellungen an (beim Boot und nach Config-Save).
// NimBLE uebernimmt die Werte zur Laufzeit fuer alle KUENFTIGEN Kopplungen —
// kein Reboot noetig. Bereits gespeicherte Bonds bleiben gueltig.
void applyBleSecurity() {
  if (cfg_ble_pin_enabled) {
    // PIN-Pairing: MITM an + "Display only" -> der ESP "zeigt" den statischen
    // Passkey (steht in der Config), die Gegenseite (Windows/Handy) muss ihn
    // beim Koppeln eingeben. Falsche PIN -> Kopplung schlaegt fehl.
    // NimBLE 2.x: Der tatsaechlich verwendete Passkey kommt beim Pairing aus
    // MyServerCallbacks::onPassKeyDisplay() (liefert cfg_ble_pin). Der Aufruf
    // von setSecurityPasskey bleibt zusaetzlich gesetzt (konsistenter
    // Bibliothekszustand, schadet nicht).
    NimBLEDevice::setSecurityAuth(true /*bonding*/, true /*mitm*/, true /*secure conn*/);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
    NimBLEDevice::setSecurityPasskey((uint32_t)cfg_ble_pin);
    dlog("BLE security: PIN pairing active\n");
  } else {
    // Just Works: Kopplungsanfragen werden automatisch angenommen (wie bisher).
    NimBLEDevice::setSecurityAuth(true /*bonding*/, false /*mitm*/, true /*secure conn*/);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    dlog("BLE security: Just-Works pairing (no PIN)\n");
  }
}

// ── BLE callbacks ─────────────────────────────────────────────────────────────
// Forward state fuer BLE-Mode-Logik (handleBleMode definiert weiter unten,
// aber die Callbacks unten brauchen bleIsAdvertising schon hier).
static bool          bleIsAdvertising  = true;

// Tatsächlicher AP-Laufzustand laut WiFi-Event-System. softAPSSID() eignet sich
// NICHT als Laufzustandsprüfung: Die Funktion liefert auch nach dem Abschalten
// weiterhin die im Treiber gespeicherte SSID zurück. Dadurch entstand bisher
// die Endlosschleife „soll AUS, sendet aber noch“, obwohl kein WLAN sichtbar war.
static volatile bool apStartedByEvent  = false;

bool bleAdvertisingActive() {
  return bleIsAdvertising;
}
static unsigned long lastMovementTime  = 0;

// ── STA-Scan-Backoff ─────────────────────────────────────────────────────────
// Feste Stufen statt Verdopplung: 20s -> 40s -> 60s -> 120s.
// Nach Erreichen der letzten Stufe bleibt der Abstand dauerhaft bei 120s,
// solange kein bekanntes WLAN erfolgreich verbunden wurde. Ein normales
// "kein Netz gefunden" ist unterwegs KEIN WLAN-Stack-Fehler und darf deshalb
// keinen Hard-Reset des STA-Stacks ausloesen.
static const unsigned long STA_SCAN_BACKOFF_MS[] = {
  20000UL,
  40000UL,
  60000UL,
  120000UL
};
static const uint8_t STA_SCAN_BACKOFF_LAST =
    (uint8_t)(sizeof(STA_SCAN_BACKOFF_MS) / sizeof(STA_SCAN_BACKOFF_MS[0]) - 1);
static uint8_t staScanBackoffStage = 0;

static void resetStaScanBackoff(unsigned long now, bool restartWait) {
  staScanBackoffStage = 0;
  staScanInterval = STA_SCAN_BACKOFF_MS[0];
  if (restartWait) lastReconnectTry = now;
}

static void advanceStaScanBackoff(unsigned long now) {
  if (staScanBackoffStage < STA_SCAN_BACKOFF_LAST) {
    staScanBackoffStage++;
  }
  staScanInterval = STA_SCAN_BACKOFF_MS[staScanBackoffStage];
  // Die neue Wartezeit beginnt erst NACH dem abgeschlossenen Scan.
  lastReconnectTry = now;
}

class MyServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo) override {
    dlog("BLE connected: %s\n", connInfo.getAddress().toString().c_str());
    deviceConnected = true;
    // BEWUSST KEIN proaktiver Security-Request (startSecurity) beim Connect:
    // Android beantwortet den auf manchen Geraeten mit einer ERSTEN Kopplungs-
    // runde ohne PIN und erzwingt die MITM-Kopplung (PIN) erst beim folgenden
    // geschuetzten Zugriff -> zwei Kopplungsdialoge hintereinander. Ohne den
    // Request loest der erste geschuetzte GATT-Zugriff GENAU EINE Kopplung mit
    // PIN-Dialog aus. Bereits gekoppelte Geraete verschluesseln beim Zugriff
    // automatisch neu.
    // Aktive Verbindungen laufen DAUERHAFT mit den schnellen Parametern des
    // Handys/PCs — Pairing (PIN), Discovery und VESC-Tool-Kommunikation bleiben
    // flott, keine Timeouts, keine Traegheit. Airtime fuers WLAN wird nur im
    // Idle gespart (langsames Advertising, wenn KEIN Client verbunden ist).
    // Im Auto-Modus: aktive Verbindung haelt Timer pausiert, nicht erneut advertisen.
    // Bei "An"-Modus: weiter advertisen (so dass weitere Clients sich verbinden koennen).
    if (cfg_ble_mode == 1) NimBLEDevice::startAdvertising();
  }
  // NimBLE 2.x: onDisconnect liefert zusaetzlich den Trennungsgrund (HCI-Code).
  // Der Grund landet im Log — hilfreich um z.B. Supervision-Timeouts (0x08/0x13)
  // von normalen Nutzer-Trennungen (0x16) zu unterscheiden.
  void onDisconnect(NimBLEServer *pServer, NimBLEConnInfo &connInfo, int reason) override {
    dlog("BLE disconnected (reason=0x%02X)\n", (unsigned)reason);
    deviceConnected = false;
    // Nur erneut advertisen, wenn der Modus das zulaesst und wir laut Zustand
    // gerade advertisen sollen. Sonst greift handleBleMode() im Loop nach.
    // (NimBLE 2.x startet Advertising nach Disconnect NICHT mehr automatisch —
    //  advertiseOnDisconnect ist per Default aus. Genau richtig fuer uns: die
    //  Kontrolle liegt vollstaendig hier bzw. in handleBleMode().)
    if (cfg_ble_mode == 1 || (cfg_ble_mode == 2 && bleIsAdvertising)) {
      NimBLEDevice::startAdvertising();
    }
  }
  void onMTUChange(uint16_t MTU, NimBLEConnInfo &connInfo) override {
    MTU_SIZE = MTU; PACKET_SIZE = MTU_SIZE - 3;
  }
  // NimBLE 2.x: Bei IOCap DISPLAY_ONLY holt sich der Stack den anzuzeigenden
  // Passkey ueber DIESEN Callback (nicht mehr ueber setSecurityPasskey!).
  // Ohne Override wuerde der Bibliotheks-Default 123456 gelten — der Nutzer-PIN
  // aus der Config waere wirkungslos. Deshalb hier zwingend cfg_ble_pin liefern.
  uint32_t onPassKeyDisplay() override {
    return (uint32_t)cfg_ble_pin;
  }
  // Wird nach einem Pairing-Versuch (z.B. von Windows/VESC Tool) aufgerufen.
  // Just-Works nimmt automatisch an — hier nur das Ergebnis loggen, damit man
  // im UART-Log (Status-Filter) sieht, ob die Kopplung geklappt hat.
  void onAuthenticationComplete(NimBLEConnInfo &connInfo) override {
    dlog("BLE pairing: %s (encrypted=%d bonded=%d)\n",
         connInfo.isEncrypted() ? "OK" : "FAILED",
         (int)connInfo.isEncrypted(), (int)connInfo.isBonded());
  }
};

class MyCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *pCharacteristic, NimBLEConnInfo &connInfo) override {
    // NimBLE 2.x: getValue() liefert NimBLEAttValue (kein std::string mehr) —
    // data()/length() funktionieren identisch, ohne Kopie in einen String.
    NimBLEAttValue rx = pCharacteristic->getValue();
    if (rx.length()>0 && pCharacteristic->getUUID().equals(pCharacteristicVescRx->getUUID())) {
      if (cfg_debug && (cfg_debug_filter & 1)) { String h="BLE=>VESC: ";for(size_t i=0;i<rx.length();i++){char x[4];snprintf(x,4,"%02X ",(uint8_t)rx.data()[i]);h+=x;} uartLogAdd(h); }
      Serial1.write((const uint8_t*)rx.data(), rx.length());
    }
  }
};

// ── WiFi Event Handler ────────────────────────────────────────────────────────
// Schreibt WiFi-Events immer auf Serial und bei aktivem Debug-Modus zusätzlich
// in den API/UI-Debuglog. Die Meldungen gehoeren zum WiFi-Filter (Bit 2), nicht
// zum allgemeinen Status-Filter. Dadurch erscheinen AP-Connect/Disconnect usw.
// im UI, sobald im Debug-Tab "WiFi" aktiviert ist.
static void wifiEventLog(const char *fmt, ...) {
  char msg[192];
  va_list args;
  va_start(args, fmt);
  vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);

  Serial.println(msg);

  if (!cfg_debug || !(cfg_debug_filter & 2)) return;
  uartLogAdd(String(msg));
}

// Vollstaendige Klartext-Zuordnung fuer ESP-IDF wifi_err_reason_t.
// Die Zahlen werden absichtlich direkt verwendet, damit der Code auch mit
// Arduino-ESP32-Versionen kompiliert, deren Header neuere Reason-Konstanten
// noch nicht enthalten. Unbekannte/reservierte Werte bleiben sichtbar.
struct WifiDisconnectReasonEntry {
  uint8_t code;
  const char *name;
};

static const WifiDisconnectReasonEntry WIFI_DISCONNECT_REASONS[] = {
  {  1, "UNSPECIFIED" },
  {  2, "AUTH_EXPIRE" },
  {  3, "AUTH_LEAVE" },
  {  4, "DISASSOC_DUE_TO_INACTIVITY" },
  {  5, "ASSOC_TOOMANY" },
  {  6, "CLASS2_FRAME_FROM_NONAUTH_STA" },
  {  7, "CLASS3_FRAME_FROM_NONASSOC_STA" },
  {  8, "ASSOC_LEAVE" },
  {  9, "ASSOC_NOT_AUTHED" },
  { 10, "DISASSOC_PWRCAP_BAD" },
  { 11, "DISASSOC_SUPCHAN_BAD" },
  { 12, "BSS_TRANSITION_DISASSOC" },
  { 13, "IE_INVALID" },
  { 14, "MIC_FAILURE" },
  { 15, "4WAY_HANDSHAKE_TIMEOUT" },
  { 16, "GROUP_KEY_UPDATE_TIMEOUT" },
  { 17, "IE_IN_4WAY_DIFFERS" },
  { 18, "GROUP_CIPHER_INVALID" },
  { 19, "PAIRWISE_CIPHER_INVALID" },
  { 20, "AKMP_INVALID" },
  { 21, "UNSUPP_RSN_IE_VERSION" },
  { 22, "INVALID_RSN_IE_CAP" },
  { 23, "802_1X_AUTH_FAILED" },
  { 24, "CIPHER_SUITE_REJECTED" },
  { 25, "TDLS_PEER_UNREACHABLE" },
  { 26, "TDLS_UNSPECIFIED" },
  { 27, "SSP_REQUESTED_DISASSOC" },
  { 28, "NO_SSP_ROAMING_AGREEMENT" },
  { 29, "BAD_CIPHER_OR_AKM" },
  { 30, "NOT_AUTHORIZED_THIS_LOCATION" },
  { 31, "SERVICE_CHANGE_PERCLUDES_TS" },
  { 32, "UNSPECIFIED_QOS" },
  { 33, "NOT_ENOUGH_BANDWIDTH" },
  { 34, "MISSING_ACKS" },
  { 35, "EXCEEDED_TXOP" },
  { 36, "STA_LEAVING" },
  { 37, "END_BA" },
  { 38, "UNKNOWN_BA" },
  { 39, "TIMEOUT" },
  { 46, "PEER_INITIATED" },
  { 47, "AP_INITIATED" },
  { 48, "INVALID_FT_ACTION_FRAME_COUNT" },
  { 49, "INVALID_PMKID" },
  { 50, "INVALID_MDE" },
  { 51, "INVALID_FTE" },
  { 67, "TRANSMISSION_LINK_ESTABLISH_FAILED" },
  { 68, "ALTERATIVE_CHANNEL_OCCUPIED" },
  {200, "BEACON_TIMEOUT" },
  {201, "NO_AP_FOUND" },
  {202, "AUTH_FAIL" },
  {203, "ASSOC_FAIL" },
  {204, "HANDSHAKE_TIMEOUT" },
  {205, "CONNECTION_FAIL" },
  {206, "AP_TSF_RESET" },
  {207, "ROAMING" },
  {208, "ASSOC_COMEBACK_TIME_TOO_LONG" },
  {209, "SA_QUERY_TIMEOUT" },
  {210, "NO_AP_FOUND_W_COMPATIBLE_SECURITY" },
  {211, "NO_AP_FOUND_IN_AUTHMODE_THRESHOLD" },
  {212, "NO_AP_FOUND_IN_RSSI_THRESHOLD" }
};

const char *wifiDisconnectReasonName(uint8_t reason) {
  for (size_t i = 0; i < sizeof(WIFI_DISCONNECT_REASONS) / sizeof(WIFI_DISCONNECT_REASONS[0]); i++) {
    if (WIFI_DISCONNECT_REASONS[i].code == reason) return WIFI_DISCONNECT_REASONS[i].name;
  }
  return "UNKNOWN_OR_RESERVED";
}

String wifiDisconnectReasonsJson() {
  String json = "[";
  for (size_t i = 0; i < sizeof(WIFI_DISCONNECT_REASONS) / sizeof(WIFI_DISCONNECT_REASONS[0]); i++) {
    if (i) json += ',';
    json += "{\"code\":" + String(WIFI_DISCONNECT_REASONS[i].code);
    json += ",\"name\":\"" + String(WIFI_DISCONNECT_REASONS[i].name) + "\"}";
  }
  json += "]";
  return json;
}

// Fängt alle relevanten WiFi-Events ab. Der entscheidende Punkt für deinen Bug:
// bei STA_DISCONNECTED darf der AP NICHT mitsterben. Wir setzen den Mode hart
// zurück und ziehen den AP sofort wieder hoch, falls er gefallen ist.
void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      wifiEventLog("[evt] STA connected");
      break;

    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      wifiEventLog("[evt] STA got IP: %s", WiFi.localIP().toString().c_str());
      diagStaConnects++;                    // Diagnose: erfolgreiche STA-Verbindung
      staWasConnected = true;
      // Kein ensureAP() hier (Reentranz vermeiden). Falls der STA-Connect den
      // AP-Channel verschoben hat, korrigiert der Watchdog im loop() das
      // zeitversetzt und ohne Event-Schleife.
      break;

    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      // *** Das ist der kritische Pfad deines Bugs. ***
      // STA hat die Verbindung verloren. Die IDF räumt intern auf — dabei darf
      // der AP NICHT verschwinden. Mode hart auf AP_STA halten und AP prüfen.
      {
        uint8_t reason = info.wifi_sta_disconnected.reason;
        wifiEventLog("[evt] STA disconnected - protecting AP (reason=%u %s)",
                     (unsigned)reason, wifiDisconnectReasonName(reason));
      }
      diagStaDisconnects++;                                 // Diagnose: Abbruch zaehlen
      diagLastDiscReason = info.wifi_sta_disconnected.reason;// Diagnose: Grund merken
      // WLAN ist weg -> der AP wird als Zugang wieder gebraucht, auch wenn ein
      // vorheriger AP-Timeout ihn abgeschaltet hatte. apWanted reaktivieren.
      // WICHTIG: hier KEIN ensureAP() aufrufen (Reentranz -> Event-Schleife).
      // Nur Mode sicherstellen + Flag setzen; der Watchdog im loop() holt den
      // AP zeitversetzt zurueck.
      apWanted = true;
      if (WiFi.getMode() != WIFI_AP_STA) {
        WiFi.mode(WIFI_AP_STA);
      }
      staWasConnected = false;
      break;

    case ARDUINO_EVENT_WIFI_AP_START:
      apStartedByEvent = true;
      wifiEventLog("[evt] AP started");
      break;

    case ARDUINO_EVENT_WIFI_AP_STOP:
      apStartedByEvent = false;
      // AP wurde gestoppt. NICHT hier ensureAP() aufrufen!
      // Grund: softAP() macht intern Stop+Start. Ein Aufruf von ensureAP() aus
      // diesem Event heraus loest erneut ein STOP-Event aus -> Endlosschleife
      // (genau der Bug: AP stopped -> restart -> AP started -> AP stopped ...).
      // Der AP-Watchdog im loop() (zeitversetzt, alle 5s) holt den AP zurueck,
      // ohne diese Reentranz. Hier nur protokollieren.
      wifiEventLog("[evt] AP stopped");
      break;

    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      wifiEventLog("[evt] AP: station connected (clients=%u)", (unsigned)WiFi.softAPgetStationNum());
      diagApClientConn++;                   // Diagnose: Handy hat sich am AP verbunden
      break;

    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      wifiEventLog("[evt] AP: station disconnected (clients=%u)", (unsigned)WiFi.softAPgetStationNum());
      diagApClientDisc++;                   // Diagnose: Handy hat AP verlassen
      // War das der LETZTE Client? Dann Nachlauf-Sperre starten: die naechsten
      // STA_SUPPRESS_AFTER_AP_MS wird nicht gescannt (kein sofortiger Stoerscan).
      if (WiFi.softAPgetStationNum() == 0) apClientGoneAt = millis();
      break;

    case ARDUINO_EVENT_WIFI_AP_PROBEREQRECVED:
      // Ein Geraet funkt den AP an (Probe-Request). Zaehlen + RSSI merken.
      // Damit laesst sich unterscheiden: Handy erreicht den AP ueberhaupt?
      diagProbeReqs++;
      diagLastProbeRssi = info.wifi_ap_probereqrecved.rssi;
      break;

    default:
      break;
  }
}

// ── AP / WiFi helpers ─────────────────────────────────────────────────────────
// Baut exakt dieselbe AP-Konfiguration, die spaeter auch WiFi.softAP() bekommt.
// Wichtig: Diese Struktur wird VOR esp_wifi_start() gesetzt. Dadurch sendet der
// ESP beim Boot nicht erst kurz die IDF-/Arduino-Default-SSID "ESP_XXXX".
static bool buildConfiguredApWifiConfig(wifi_config_t &conf, int channel) {
  memset(&conf, 0, sizeof(conf));

  size_t ssidLen = cfg_ap_ssid.length();
  size_t passLen = cfg_ap_pass.length();
  if (ssidLen == 0 || ssidLen > sizeof(conf.ap.ssid)) {
    dlog("AP preconfig: invalid SSID length %u\n", (unsigned)ssidLen);
    return false;
  }
  if (passLen > sizeof(conf.ap.password) - 1 || (passLen > 0 && passLen < 8)) {
    dlog("AP preconfig: invalid password length %u\n", (unsigned)passLen);
    return false;
  }

  memcpy(conf.ap.ssid, cfg_ap_ssid.c_str(), ssidLen);
  conf.ap.ssid_len       = (uint8_t)ssidLen;
  conf.ap.channel        = (channel >= 1 && channel <= 13) ? channel : 1;
  conf.ap.ssid_hidden    = 0;
  conf.ap.max_connection = 4;
  conf.ap.beacon_interval = 100;

  if (passLen > 0) {
    memcpy(conf.ap.password, cfg_ap_pass.c_str(), passLen);
    conf.ap.password[passLen] = 0;
    conf.ap.authmode = WIFI_AUTH_WPA2_PSK;
    conf.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;
  } else {
    conf.ap.authmode = WIFI_AUTH_OPEN;
  }
  return true;
}

// Sauberer ERSTER WiFi-Start ohne kurz sichtbaren Default-AP:
//  1. Arduino-Low-Level initialisieren, aber Funk noch nicht starten
//  2. AP_STA-Modus und unsere AP-Konfiguration direkt in IDF setzen
//  3. erst danach WiFi.mode(AP_STA) aufrufen -> erster Beacon hat sofort AP3
//
// Nur fuer WIFI_MODE_NULL gedacht (Boot bzw. kompletter Stack-Neustart). Wenn
// der Treiber bereits laeuft, bleibt die normale Arduino-Logik aktiv.
static bool startApStaCleanFromOff(int channel) {
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    return WiFi.mode(WIFI_AP_STA);
  }

  wifi_config_t apConf;
  if (!buildConfiguredApWifiConfig(apConf, channel)) return false;

  if (!wifiLowLevelInit(false)) {
    dlog("AP preconfig: wifiLowLevelInit failed\n");
    return false;
  }

  esp_err_t err = esp_wifi_set_mode(WIFI_MODE_APSTA);
  if (err != ESP_OK) {
    dlog("AP preconfig: esp_wifi_set_mode failed (%d)\n", (int)err);
    return false;
  }

  err = esp_wifi_set_config(WIFI_IF_AP, &apConf);
  if (err != ESP_OK) {
    dlog("AP preconfig: esp_wifi_set_config failed (%d)\n", (int)err);
    return false;
  }

  // Protokoll (B/G/N) und Bandbreite (HT20) JETZT setzen — vor dem ersten
  // Treiber-Start. Diese beiden Calls starten unter IDF 5.5 ein bereits
  // laufendes AP-Interface neu; hier laeuft es aber noch nicht, also
  // entsteht kein Stop/Start. tuneApDhcp() erkennt spaeter, dass die Werte
  // schon korrekt sind, und ueberspringt sie -> kein Boot-Flackern.
  esp_wifi_set_protocol(WIFI_IF_AP,
      WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);

  // WiFi.mode() sieht intern noch "nicht gestartet", ruft deshalb den normalen
  // Arduino-Startpfad auf. Der IDF-Treiber besitzt zu diesem Zeitpunkt aber
  // bereits unsere SSID/Passwort-Konfiguration. Es entsteht nur EIN AP_START.
  if (!WiFi.mode(WIFI_AP_STA)) {
    dlog("AP preconfig: WiFi.mode(AP_STA) failed\n");
    return false;
  }

  dlog("AP preconfig: '%s' set before first WiFi start\n", cfg_ap_ssid.c_str());
  return true;
}

// ensureAP: garantiert, dass der AP läuft (sofern apWanted == true).
// force == true  -> AP wird in jedem Fall neu konfiguriert (softAP erneut)
// force == false -> AP wird nur (neu) gestartet, wenn er aktuell nicht läuft
// ── AP/DHCP-Feintuning ────────────────────────────────────────────────────────
// Der ESP-IDF-DHCP-Server hat per Default eine sehr lange Lease-Zeit und reagiert
// bei Neuverbindungen (Handy trennt + verbindet erneut) traege. Hier wird der
// DHCP-Server kurz gestoppt, die Lease-Zeit verkuerzt und wieder gestartet.
// Zusaetzlich Beacon-Intervall senken, damit Clients den AP schneller finden.
// Muss NACH einem erfolgreichen softAP() laufen (dann existiert das AP-netif).
static void tuneApDhcp() {
  esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (ap == nullptr) {
    dlog("AP tune: no AP netif found\n");
    return;
  }

  // DHCP-Server stoppen (Pflicht, bevor Optionen gesetzt werden duerfen).
  esp_netif_dhcps_stop(ap);

  // Lease-Zeit LANG setzen (120 Min). Grund: Das Handy erneuert den Lease bei
  // der Haelfte der Zeit im Hintergrund. Eine kurze Lease (frueher 2 Min ->
  // Renew jede Minute) bedeutet staendige DHCP-Pakete, und JEDER Renew ist eine
  // Gelegenheit fuer Paketverlust unter Funklast -> Abbruch / "Verbindungsfehler".
  // Eine lange Lease heisst: einmal IP holen, dann lange Ruhe -> deutlich weniger
  // DHCP-Verkehr und damit weniger Abbrueche. (Die Geschwindigkeit des ERSTEN
  // IP-Bezugs haengt nicht an der Lease-Dauer, sondern am Paketverlust selbst.)
  uint32_t leaseMinutes = 120;
  esp_netif_dhcps_option(ap, ESP_NETIF_OP_SET, ESP_NETIF_IP_ADDRESS_LEASE_TIME,
                         &leaseMinutes, sizeof(leaseMinutes));

  // DHCP-Server wieder starten.
  esp_err_t e = esp_netif_dhcps_start(ap);
  dlog("AP tune: DHCP lease=%lumin, dhcps_start=%d\n",
                (unsigned long)leaseMinutes, (int)e);

  // Beacon-Intervall senken (Default 100ms). 100ms ist schon gut; wir setzen es
  // explizit, damit Clients den AP zuegig sehen. Niedriger = haeufiger Beacons
  // = schnelleres Finden, aber mehr Funklast. 100 ist ein guter Kompromiss.
  wifi_config_t conf;
  if (esp_wifi_get_config(WIFI_IF_AP, &conf) == ESP_OK) {
    conf.ap.beacon_interval = 100;
    // Mehr gleichzeitige Verbindungsversuche zulassen (Default oft niedrig).
    if (conf.ap.max_connection < 4) conf.ap.max_connection = 4;
    esp_wifi_set_config(WIFI_IF_AP, &conf);
  }

  // WICHTIG: WiFi-Powersave NICHT auf WIFI_PS_NONE setzen! Auf dem ESP32-S3
  // teilen sich WiFi und Bluetooth denselben 2.4-GHz-Funk. Wenn beide aktiv
  // sind (hier: BLE zum VESC + WiFi-AP), VERLANGT die IDF Modem-Sleep -- sonst
  // abort() + Boot-Schleife ("Should enable WiFi modem sleep when both WiFi and
  // Bluetooth are enabled"). Deshalb der schonende Min-Modem-Sleep: spart nur
  // minimal, laesst aber BT genug Funkzeit und bleibt stabil.
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  // Sendeleistung explizit auf Maximum. 80 = 20 dBm (Einheit: 0.25 dBm),
  // verbessert Reichweite/Verbindungsqualitaet des AP. (War kurz im Verdacht,
  // den Update-Check zu stoeren -- Ursache war aber DNS, daher wieder aktiv.)
  esp_wifi_set_max_tx_power(80);

  // WiFi-Protokoll (B/G/N) und Kanalbreite (HT20) setzen — ABER nur, wenn sie
  // nicht ohnehin schon korrekt sind. Wichtig unter IDF 5.5 / Core 3.x:
  // esp_wifi_set_protocol() und esp_wifi_set_bandwidth() starten das AP-
  // Interface NEU, wenn sie auf einen laufenden AP angewendet werden (unter
  // IDF 4.4 war das ein stiller In-Place-Wechsel). Da tuneApDhcp() nach JEDEM
  // erfolgreichen ensureAP() laeuft, verursachte das ein sichtbares AP-Stop/
  // Start-Flackern bei jedem Aufruf. Die Werte aendern sich nie -> nur beim
  // ersten Mal (oder nach echtem Abweichen) setzen, danach ueberspringen.
  //
  // B als Basis macht Beacons/Management-Frames mit der robustesten Modulation
  // (manche Handys verbinden sich damit zuverlaessiger). HT20 ist auf 2,4 GHz
  // stabiler als HT40 (Repeater/Router-Vertraeglichkeit).
  const uint8_t wantProto = WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N;
  uint8_t curProto = 0;
  bool protoOk = (esp_wifi_get_protocol(WIFI_IF_AP, &curProto) == ESP_OK) && (curProto == wantProto);
  if (!protoOk) {
    esp_wifi_set_protocol(WIFI_IF_AP, wantProto);
  }

  wifi_bandwidth_t curBw = WIFI_BW_HT40;
  bool bwOk = (esp_wifi_get_bandwidth(WIFI_IF_AP, &curBw) == ESP_OK) && (curBw == WIFI_BW_HT20);
  if (!bwOk) {
    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
  }

  dlog("AP tune: ps=MIN_MODEM (BT coexistence), TX-power max, proto=%s, bw=%s\n",
       protoOk ? "BGN(ok)" : "BGN(set)", bwOk ? "HT20(ok)" : "HT20(set)");
}

bool ensureAP(bool force) {
  // force == true  -> AP-Start ERZWINGEN: Gesundheitscheck ueberspringen und
  //                   softAP() definitiv aufrufen. Noetig nach einem echten
  //                   Timeout-Off (Wake bei Bewegung, Modus-Wechsel, Safety,
  //                   Watchdog-Reparatur). Grund: nach WiFi.mode(WIFI_AP_STA)
  //                   sieht das AP-Interface sofort "gesund" aus (AP-Bit gesetzt,
  //                   IP 192.168.4.1, softAPSSID() kann noch die alte SSID aus
  //                   der gespeicherten Config melden), OBWOHL softAP() nach dem
  //                   Abschalten nie wieder lief -> Zombie-AP, der nichts sendet.
  //                   Der Gesundheitscheck wuerde dann faelschlich early-returnen
  //                   und der AP kaeme nach dem Timeout NIE zurueck.
  // force == false -> schonender Modus fuer periodische Aufrufer (Reconnect,
  //                   Roam, Channel-Absicherung): laeuft der AP nachweislich
  //                   korrekt, NICHT neu starten — ein softAP()-Neustart wirft
  //                   alle verbundenen Clients ab.
  if (!apWanted) return false;

  wifi_mode_t mode = WiFi.getMode();
  bool apBitSet  = (mode == WIFI_AP || mode == WIFI_AP_STA);
  bool apIpValid = (WiFi.softAPIP() != IPAddress(0,0,0,0));
  bool apLooksUp = apBitSet && apIpValid;

  // Ziel-Channel bestimmen: wenn STA verbunden ist, MUSS der AP auf dessen
  // Channel (Hardware-Zwang, eine Funkeinheit = ein Channel). Sonst Channel 1.
  int ch = (WiFi.status() == WL_CONNECTED) ? WiFi.channel() : 1;
  if (ch < 1 || ch > 13) ch = 1;

  // *** WICHTIG gegen "Verbindungsfehler" beim Verbinden ***
  // Wenn der AP bereits sauber laeuft UND schon auf dem richtigen Channel ist,
  // NICHT neu starten — aber NUR bei force==false. Ein softAP()-Neustart wirft
  // alle verbundenen Clients ab und laesst Verbindungsversuche scheitern. Der
  // Watchdog/Reconnect ruft ensureAP() haeufig auf; ohne diese Pruefung wuerde
  // der AP staendig neu gestartet und waere praktisch nicht verbindbar.
  // Pruefen ob der aktuell laufende AP WIRKLICH unserer ist (richtige SSID).
  // Die ESP-IDF zieht im AP-Modus einen Default-AP "ESP-XXXX" hoch, sobald das
  // AP-Bit gesetzt ist — der hat auch eine gueltige IP. Ohne SSID-Pruefung
  // wuerde ensureAP() diesen Default-AP faelschlich fuer "unseren" halten und
  // softAP() mit der eigenen SSID nie aufrufen -> es bleibt dauerhaft bei
  // "ESP-XXXX". Deshalb hier die laufende AP-SSID mit cfg_ap_ssid vergleichen.
  //
  // SONDERFALL leere SSID (Core 3.x / IDF 5.5): Direkt nach dem Preconfig-Pfad
  // (esp_wifi_set_config vor dem ersten Start) hat der Treiber unsere SSID zwar
  // schon, der Arduino-Wrapper-Cache liefert bei softAPSSID() aber noch "".
  // Das ist NICHT dasselbe wie ein fremder "ESP-XXXX"-AP — es ist unser eigener
  // AP im Hochlauf. Ein leerer String wird daher als "unserer" gewertet, sonst
  // wuerde ensureAP() einen ueberfluessigen softAP()-Neustart ausloesen (samt
  // irritierender "wrong SSID"-Logzeile). Die ESP-XXXX-Absicherung bleibt voll
  // erhalten, weil dieser Default-AP eine NICHT-leere SSID hat.
  String runningSsid = WiFi.softAPSSID();
  bool ssidOk = (runningSsid == cfg_ap_ssid) || (runningSsid.length() == 0);

  if (!force && apLooksUp && ssidOk && WiFi.softAPgetStationNum() >= 0) {
    int curCh = WiFi.channel();   // aktueller Betriebs-Channel
    bool channelOk = (curCh == ch) || (WiFi.status() != WL_CONNECTED);
    if (channelOk) {
      apActive = true;
      return true;   // AP laeuft korrekt (richtige SSID + Channel) -> nichts tun
    }
    // Channel weicht ab (STA hat den Channel geaendert) -> Neustart noetig.
    dlog("AP: channel changed (%d -> %d), restarting AP once\n", curCh, ch);
  } else if (apLooksUp && !ssidOk) {
    // Falsche SSID laeuft (z.B. IDF-Default "ESP-XXXX") -> softAP() erzwingen.
    dlog("AP: wrong SSID running ('%s', want '%s') -> starting correct AP\n",
                  runningSsid.c_str(), cfg_ap_ssid.c_str());
  }

  // Mode sicherstellen — niemals AP-Bit verlieren. Wenn der komplette WiFi-
  // Stack aus ist, AP-Konfiguration VOR dem Start setzen, damit kein ESP_XXXX
  // Beacon entsteht. Bei laufendem STA bleibt der normale Mode-Wechsel erhalten;
  // dessen AP-Konfiguration wurde beim Abschalten bewusst nicht mehr geloescht.
  if (mode == WIFI_MODE_NULL) {
    if (!startApStaCleanFromOff(ch)) return false;
    delay(20);
  } else if (mode != WIFI_AP_STA) {
    WiFi.mode(WIFI_AP_STA);
    delay(20);
  }

  const char *pass = cfg_ap_pass.length() > 0 ? cfg_ap_pass.c_str() : nullptr;

  // Retry-Schleife: softAP() schlaegt beim Boot sporadisch fehl (WLAN-Stack
  // noch nicht bereit). Bis zu 5 Versuche, und nach softAP() pruefen wir die
  // IP — nicht nur den Rueckgabewert, denn softAP() liefert manchmal true
  // obwohl noch keine gueltige IP da ist.
  bool ok = false;
  for (int attempt = 1; attempt <= 5; attempt++) {
    // AP-IP explizit setzen (sonst waere es der Arduino-Default 192.168.4.1).
    // Muss VOR softAP() aufgerufen werden.
    WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET);
    ok = WiFi.softAP(cfg_ap_ssid.c_str(), pass, ch, 0, 4);
    delay(150);  // Stack Zeit geben, den AP wirklich hochzufahren
    IPAddress ip = WiFi.softAPIP();
    bool ipOk = (ip != IPAddress(0,0,0,0));
    if (ok && ipOk) {
      isAPMode    = true;
      apActive    = true;
      apStartTime = millis();
      apLastClientGone = 0;   // frischer Start: Idle-Timer laeuft ab AP-Start
      apLastStationNum = 0;
      apOffByTimeout = false; // AP laeuft wieder -> Timeout-Flag loeschen
      dlog("AP (re)started: %s ch=%d ip=%s (try %d)\n",
                    cfg_ap_ssid.c_str(), ch, ip.toString().c_str(), attempt);
      tuneApDhcp();   // DHCP-Lease verkuerzen + Beacon/AP-Parameter optimieren
      return true;
    }
    dlog("AP start attempt %d failed (ok=%d ip=%s) — retrying\n",
                  attempt, ok ? 1 : 0, ip.toString().c_str());
    // Vor dem naechsten Versuch Mode neu setzen, Stack durchatmen lassen.
    WiFi.mode(WIFI_AP_STA);
    delay(200);
  }
  dlog("AP start FAILED after 5 attempts!\n");
  return false;
}

// ── Manueller AP-Start fuer Debug/API ───────────────────────────────────────
// Aktiviert den AP sofort, ohne cfg_ap_mode oder andere gespeicherte Werte zu
// veraendern. Im Auto-Modus beginnt der normale Idle-Timeout danach erneut.
bool startAccessPointManual() {
  dlog("AP manual: API start requested\n");

  apWanted       = true;
  apOffByTimeout = false;
  apLastClientGone = millis();
  apLastStationNum = WiFi.softAPgetStationNum();

  wifi_mode_t mode = WiFi.getMode();
  bool apModeEnabled = (mode == WIFI_AP || mode == WIFI_AP_STA);
  bool ssidOk = (WiFi.softAPSSID() == cfg_ap_ssid) || (WiFi.softAPSSID().length() == 0);
  bool ipOk = (WiFi.softAPIP() != IPAddress(0, 0, 0, 0));
  bool alreadyHealthy = apModeEnabled && apStartedByEvent && ssidOk && ipOk;

  // Einen bereits gesunden AP nicht neu starten: Sonst wuerde der HTTP-Client,
  // der diesen Endpunkt gerade ueber den AP aufruft, selbst getrennt.
  bool ok = alreadyHealthy ? ensureAP(false) : ensureAP(true);
  if (ok) {
    apActive = true;
    isAPMode = true;
    apOffByTimeout = false;
    apLastClientGone = millis();
    dlog("AP manual: AP active, ssid='%s', ip=%s\n",
         cfg_ap_ssid.c_str(), WiFi.softAPIP().toString().c_str());
  } else {
    dlog("AP manual: AP start FAILED\n");
  }
  return ok;
}

String accessPointStatusJson(bool operationOk) {
  wifi_mode_t arduinoMode = WiFi.getMode();
  bool arduinoApEnabled = (arduinoMode == WIFI_AP || arduinoMode == WIFI_AP_STA);

  wifi_mode_t idfMode = WIFI_MODE_NULL;
  esp_err_t idfModeResult = esp_wifi_get_mode(&idfMode);
  bool idfApEnabled = (idfModeResult == ESP_OK) &&
                      (idfMode == WIFI_MODE_AP || idfMode == WIFI_MODE_APSTA);

  String ssid = WiFi.softAPSSID();
  IPAddress ip = WiFi.softAPIP();
  bool running = (arduinoApEnabled || idfApEnabled) &&
                 apStartedByEvent &&
                 ssid == cfg_ap_ssid &&
                 ip != IPAddress(0, 0, 0, 0);

  String json = "{";
  json += "\"ok\":" + String(operationOk ? "true" : "false");
  json += ",\"running\":" + String(running ? "true" : "false");
  json += ",\"wanted\":" + String(apWanted ? "true" : "false");
  json += ",\"active_flag\":" + String(apActive ? "true" : "false");
  json += ",\"event_started\":" + String(apStartedByEvent ? "true" : "false");
  json += ",\"off_by_timeout\":" + String(apOffByTimeout ? "true" : "false");
  json += ",\"auto_mode\":" + String(cfg_ap_mode == 2 ? "true" : "false");
  json += ",\"ssid\":\"" + jsonEscapeDebug(ssid) + "\"";
  json += ",\"configured_ssid\":\"" + jsonEscapeDebug(cfg_ap_ssid) + "\"";
  json += ",\"ip\":\"" + ip.toString() + "\"";
  json += ",\"clients\":" + String(WiFi.softAPgetStationNum());
  json += ",\"arduino_mode\":" + String((int)arduinoMode);
  json += ",\"idf_mode\":" + String(idfModeResult == ESP_OK ? (int)idfMode : -1);
  json += "}";
  return json;
}

bool setupAccessPoint() {
  dlog("AP: %s\n", cfg_ap_ssid.c_str());
  apWanted = true;            // AP soll dauerhaft laufen

  bool ok = ensureAP(true);   // initial forciert starten (hat selbst 5 Retries)
  if (ok) return true;

  // Eskalation: AP kam trotz 5 Versuchen nicht hoch. Jetzt WLAN-Stack KOMPLETT
  // zuruecksetzen und von vorne. Das hilft gegen haengenden/halb-initialisierten
  // WLAN-Treiber, den ein blosses softAP()-Retry nicht loest.
  dlog("AP: hard reset of WiFi stack and retry...\n");
  WiFi.disconnect(true, true);   // STA trennen + Config loeschen
  WiFi.softAPdisconnect(true);   // AP stoppen + Config loeschen
  WiFi.mode(WIFI_OFF);
  delay(500);
  // Auch nach einem kompletten Stack-Reset zuerst die richtige SSID in den
  // noch gestoppten Treiber schreiben. Sonst waere hier erneut kurz ESP_XXXX.
  if (!startApStaCleanFromOff(1)) {
    dlog("AP: clean stack restart failed\n");
  }
  delay(300);
  ok = ensureAP(true);
  if (ok) { dlog("AP: recovered after stack reset\n"); return true; }

  dlog("AP: STILL failing after stack reset!\n");
  return false;
}

// ── WiFi setup ────────────────────────────────────────────────────────────────

// Setzt die IP-Konfiguration fuer eine SSID BEVOR verbunden wird.
// KERNPUNKT des Static-IP-Bugs: WiFi.config() MUSS vor WiFi.begin() kommen.
// Wird es danach (oder gar nicht) aufgerufen, verbindet der ESP per DHCP und
// die statische IP greift nicht -> IP springt bei jedem Reconnect (z.B. .210
// vs. .75). Fuer Netze OHNE statische IP wird hier explizit auf DHCP
// zurueckgesetzt, damit keine alte statische IP aus einem frueheren Netz
// haengen bleibt.
static void staApplyIpConfig(const String &ssid) {
  for (auto &w : cfg_wifi) {
    if (w.ssid == ssid) {
      if (w.staticIp && w.ip.length() > 0 &&
          applyStaticConfig(w.ip, w.gateway, w.subnet, w.dns)) {
        dlog("WiFi: static IP %s set for '%s' (before begin)\n",
                      w.ip.c_str(), ssid.c_str());
      } else {
        // kein/ungueltiges Static -> DHCP erzwingen (0.0.0.0 = DHCP-Client an)
        WiFi.config(IPAddress((uint32_t)0), IPAddress((uint32_t)0), IPAddress((uint32_t)0));
      }
      return;
    }
  }
  // SSID nicht in der Config gefunden -> sicherheitshalber DHCP
  WiFi.config(IPAddress((uint32_t)0), IPAddress((uint32_t)0), IPAddress((uint32_t)0));
}

// Einheitlicher STA-Verbindungsaufbau: erst IP-Config (static/DHCP), dann begin.
// IMMER diesen Helfer statt WiFi.begin() direkt verwenden, damit die statische
// IP in JEDEM Connect-Pfad (Boot, Reconnect, Roam) zuverlaessig greift.
static void staBegin(const String &ssid, const String &pass) {
  staApplyIpConfig(ssid);
  WiFi.begin(ssid.c_str(), pass.c_str());
  esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);   // 20 MHz auch fuer STA (Stabilitaet)
}

bool setupWiFiClient() {
  if (cfg_wifi.empty()) { dlog("WiFi: no networks configured\n"); return false; }
  dlog("WiFi Client: connecting...\n");
  // Modus NUR setzen, wenn er nicht ohnehin schon AP_STA ist. Unter Core 3.x
  // (IDF 5.5) loest ein WiFi.mode(WIFI_AP_STA) auch dann einen Netif-Durchlauf
  // aus, wenn der Modus bereits AP_STA ist — und das stoppt/startet den AP kurz
  // (sichtbares AP-Flackern beim Boot). Der AP wurde oben bereits via
  // startApStaCleanFromOff() + setupAccessPoint() korrekt auf AP_STA gebracht,
  // hier ist der erneute Aufruf also ueberfluessig. Unter IDF 4.4 war er ein
  // No-Op; unter Core 3.x nicht mehr, daher die Abfrage.
  if (WiFi.getMode() != WIFI_AP_STA) WiFi.mode(WIFI_AP_STA);
  WiFi.setHostname(cfg_hostname.c_str());
  esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);   // 20 MHz auch fuer STA (Stabilitaet)
  for (auto &n : cfg_wifi) { wifiMulti.addAP(n.ssid.c_str(), n.pass.c_str()); Serial.printf("  + %s\n", n.ssid.c_str()); }

  // Bis zu 2 Anlaeufe: erster normaler Versuch, bei Fehlschlag ein STA-Reset
  // (ohne den AP zu killen) und ein zweiter Versuch. Schuetzt gegen haengenden
  // STA-Teil des WLAN-Stacks beim Boot.
  for (int round = 1; round <= 2; round++) {
    unsigned long start = millis();
    bool connected = false;
    while (millis() - start < 17000) {
      if (wifiMulti.run(15000) == WL_CONNECTED) { connected = true; break; }
      delay(500); Serial.print(".");
    }
    if (connected) break;

    Serial.printf("\nWiFi: connect round %d failed", round);
    if (round == 1) {
      // STA-Teil zuruecksetzen, AP NICHT anfassen (disconnect(false,false)
      // laesst Funk+Mode erhalten). Danach zweiter Versuch.
      Serial.println(" — resetting STA and retrying");
      WiFi.disconnect(false, false);
      delay(300);
    } else {
      // Auch zweiter Versuch gescheitert -> aufgeben (Loop-Reconnect uebernimmt).
      Serial.println(" — giving up boot connect (loop will keep trying)");
      WiFi.disconnect(false, false);
      return false;
    }
  }
  String csid = WiFi.SSID();
  for (auto &n : cfg_wifi) {
    if (n.ssid==csid && n.staticIp && n.ip.length()>0) {
      // wifiMulti hat per DHCP verbunden. Static NACHtraeglich zu setzen ist
      // unzuverlaessig -> einmal sauber neu verbinden MIT config vor begin.
      dlog("WiFi: static IP -> clean reconnect (config before begin)\n");
      WiFi.disconnect(false, false);
      delay(50);
      staBegin(csid, n.pass);
      unsigned long t0 = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - t0 < 8000) delay(100);
      break;
    }
  }
  Serial.printf("\nWiFi: %s | IP: %s | RSSI: %d dBm\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
  return true;
}


// ── BLE Mode Handler (An / Aus / Auto) ────────────────────────────────────────
// Steuert das BLE-Advertising abhaengig vom Modus:
//   0 = Aus  : Advertising permanent abgeschaltet
//   1 = An   : Advertising permanent an (Default, klassisches Verhalten)
//   2 = Auto : Boot-Default an; |ERPM| > Schwelle setzt Timer zurueck;
//              nach off_sec ohne Bewegung und ohne Client -> aus;
//              bei naechster Bewegung wieder an.
// Eine aktive Verbindung (BLE / TCP / Web-UI) haelt den Timer pausiert.
// (bleIsAdvertising und lastMovementTime sind weiter oben deklariert, weil
//  MyServerCallbacks bereits auf bleIsAdvertising zugreift.)

// ── BLE Advertising-Intervall: WLAN-Airtime zurueckgewinnen ────────────────────
// BLE und WiFi teilen sich EIN 2,4-GHz-Radio; die Koexistenz erzwingt
// WIFI_PS_MIN_MODEM (WIFI_PS_NONE ist gesperrt, solange BLE laeuft). Wenn der ESP
// nur advertised (kein Client verbunden), frisst schnelles Advertising Airtime,
// die dann dem WLAN fehlt -> ueber Repeater die typische "verbunden, aber schlecht
// erreichbar"-Situation. Strategie ohne BLE abzuschalten:
//   - Frisch beim (Neu-)Advertising: kurz schnelles Intervall -> Phone findet den
//     ESP zuegig (Discovery-Fenster).
//   - Danach ohne Client: langes Intervall -> WLAN bekommt spuerbar mehr Airtime.
//   - Sobald ein Client verbunden ist: es wird i.d.R. nicht geadvertised -> egal;
//     beim naechsten Advertising beginnt das Discovery-Fenster erneut.
// Intervall-Einheit: 0.625 ms.
static const uint16_t      ADV_FAST_MIN       = 32;    //  20 ms  (schnelle Discovery)
static const uint16_t      ADV_FAST_MAX       = 64;    //  40 ms
static const uint16_t      ADV_SLOW_MIN       = 1216;  //  760 ms  (Apple-empfohlener Wert:
static const uint16_t      ADV_SLOW_MAX       = 1636;  // 1022 ms   zuverlaessig auffindbar, spart trotzdem Airtime)
static const unsigned long ADV_FAST_WINDOW_MS = 15000; // 15 s schnell, dann langsam
static bool                advSlowActive      = false; // aktuell im HW gesetztes Intervall (Quelle der Wahrheit)
static unsigned long       advStartedAt       = 0;     // seit wann ununterbrochen idle-geadvertised (0 = nicht)

// Setzt das Advertising-Intervall (schnell/langsam). Wirkt beim naechsten start().
static void applyAdvInterval(bool slow) {
  NimBLEAdvertising *pAdv = NimBLEDevice::getAdvertising();
  if (slow) { pAdv->setMinInterval(ADV_SLOW_MIN); pAdv->setMaxInterval(ADV_SLOW_MAX); }
  else      { pAdv->setMinInterval(ADV_FAST_MIN); pAdv->setMaxInterval(ADV_FAST_MAX); }
  advSlowActive = slow;
}

// Im Loop aufrufen: verwaltet das schnelle Discovery-Fenster und den Wechsel auf
// langes Intervall bei laengerem Idle-Advertising.
static void manageAdvInterval() {
  // ── Volle Leistung gewuenscht? -> Energiesparmodus komplett AUS ────────────
  // Der Haken "BLE-Energiesparmodus deaktivieren" stellt das alte Verhalten
  // wieder her: Advertising bleibt DAUERHAFT im schnellen Intervall (20-40ms),
  // es wird nie auf das langsame Airtime-Spar-Intervall gewechselt. Der ESP
  // ist damit jederzeit sofort auffindbar. Greift ohne Neustart: steht die
  // Hardware gerade auf "langsam" (Haken wurde soeben gesetzt), wird sofort
  // auf schnell zurueckgeschaltet.
  if (cfg_ble_full_power) {
    advStartedAt = 0;   // falls der Haken spaeter wieder entfernt wird:
                        // Fast-Fenster beginnt dann von vorn
    if (advSlowActive) {
      NimBLEDevice::stopAdvertising();
      applyAdvInterval(false);
      if (bleIsAdvertising) NimBLEDevice::startAdvertising();
      dlog("BLE adv: power-save OFF -> permanently fast interval\n");
    }
    return;
  }

  // Nur relevant, wenn geadvertised wird UND kein Client verbunden ist.
  if (!bleIsAdvertising || deviceConnected) {
    advStartedAt = 0;   // naechstes Idle-Advertising startet wieder im Fast-Fenster
    return;
  }
  unsigned long now = millis();
  if (advStartedAt == 0) {
    // Advertising hat gerade (wieder) begonnen -> schnelle Discovery-Phase.
    advStartedAt = now;
    if (advSlowActive) {   // stand noch auf langsam -> auf schnell zuruecksetzen
      NimBLEDevice::stopAdvertising();
      applyAdvInterval(false);
      NimBLEDevice::startAdvertising();
    }
    return;
  }
  if (!advSlowActive && (now - advStartedAt > ADV_FAST_WINDOW_MS)) {
    // Lange kein Client -> langes Intervall, WLAN bekommt Airtime zurueck.
    NimBLEDevice::stopAdvertising();
    applyAdvInterval(true);
    NimBLEDevice::startAdvertising();
    dlog("BLE adv: idle -> slow interval (WiFi airtime)\n");
  }
}

void handleBleMode() {
  static int lastMode = -1;
  static unsigned long lastCheck = 0;

  // Reagiere sofort auf Mode-Wechsel (z.B. durch Config-Speichern)
  if (cfg_ble_mode != lastMode) {
    lastMode = cfg_ble_mode;
    if (cfg_ble_mode == 0) {
      // Aus
      // NimBLE 2.x: alle verbundenen Clients ueber ihre echten Conn-Handles
      // trennen (disconnect(0) hat sich frueher auf Handle 0 verlassen).
      if (pServer && pServer->getConnectedCount() > 0) {
        for (uint16_t h : pServer->getPeerDevices()) pServer->disconnect(h);
        delay(50);
      }
      NimBLEDevice::stopAdvertising();
      bleIsAdvertising = false;
      dlog("BLE mode: OFF\n");
    } else if (cfg_ble_mode == 1) {
      // An
      NimBLEDevice::startAdvertising();
      bleIsAdvertising = true;
      dlog("BLE mode: ON\n");
    } else {
      // Auto: Default beim Boot/Wechsel -> an, Timer startet
      NimBLEDevice::startAdvertising();
      bleIsAdvertising = true;
      lastMovementTime = millis();
      dlog("BLE mode: AUTO (starting ON)\n");
    }
  }

  // Nur Auto-Modus braucht laufende Logik
  if (cfg_ble_mode != 2) return;

  if (millis() - lastCheck < 1000) return;
  lastCheck = millis();

  // Bewegung erkennen (absoluter ERPM-Wert ueber Schwelle)
  int32_t absErpm = vescStatus.erpm < 0 ? -vescStatus.erpm : vescStatus.erpm;
  if (vescStatus.connected && absErpm > cfg_ble_auto_erpm_on) {
    lastMovementTime = millis();
    // Falls BLE gerade aus war, wieder an
    if (!bleIsAdvertising) {
      NimBLEDevice::startAdvertising();
      bleIsAdvertising = true;
      dlog("BLE auto: movement (erpm=%d) -> ON\n", (int)vescStatus.erpm);
    }
    return;
  }

  // Aktive Verbindung haelt Timer pausiert UND weckt BLE auf falls es aus war.
  // (Web-UI/TCP laufen ueber WiFi, funktionieren auch bei ausgeschaltetem BLE.
  //  Wenn der Nutzer wieder am Geraet arbeitet, soll BLE erreichbar werden.)
  bool anyClient = deviceConnected
                || (wifiClient && wifiClient.connected())
                || webUiActive();
  if (anyClient) {
    lastMovementTime = millis();
    if (!bleIsAdvertising) {
      NimBLEDevice::startAdvertising();
      bleIsAdvertising = true;
      dlog("BLE auto: client active -> ON\n");
    }
    return;
  }

  // Keine Bewegung, keine Verbindung -> Timer pruefen
  if (bleIsAdvertising) {
    unsigned long idleMs = millis() - lastMovementTime;
    if (idleMs >= (unsigned long)cfg_ble_auto_off_sec * 1000UL) {
      dlog("BLE auto: idle %lus, no client -> OFF\n", idleMs/1000);
      NimBLEDevice::stopAdvertising();
      bleIsAdvertising = false;
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
    staApplyIpConfig(curSsid);                // static/DHCP VOR begin setzen
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
          applyStaticConfig(w.ip, w.gateway, w.subnet, w.dns);
          break;
        }
      }
    } else {
      Serial.println("ROAM: switch failed — falling back to wifiMulti");
      wifiMulti.run(8000);                    // Notfall: irgendeinen AP nehmen
    }
    lastRoamSwitch = millis();
    ensureAP(false);                          // AP nach dem Wechsel absichern
    if (cfg_ble_mode == 1 || (cfg_ble_mode == 2 && bleIsAdvertising)) {
      NimBLEDevice::startAdvertising();
    }
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

  unsigned long now = millis();

  // ── Verbunden? ────────────────────────────────────────────────────────────
  if (WiFi.status() == WL_CONNECTED) {
    staDownSince = 0;
    // Erst eine wirklich erfolgreiche Verbindung setzt das Backoff wieder auf
    // die erste Stufe. Solange das Heimnetz nur nicht gefunden wird, bleibt die
    // erreichte Stufe (bis dauerhaft 120s) erhalten.
    resetStaScanBackoff(now, true);
    if (staConnecting) {
      // Gerade erfolgreich (non-blocking) verbunden -> abschliessen.
      staConnecting   = false;
      staConnectFails = 0;
      String csid = WiFi.SSID();
      for (auto &w : cfg_wifi) {
        if (w.ssid==csid && w.staticIp && w.ip.length()>0) {
          applyStaticConfig(w.ip, w.gateway, w.subnet, w.dns);
          break;
        }
      }
      dlog("WiFi connected: %s | IP: %s\n",
                    WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
      if (cfg_ble_mode == 1 || (cfg_ble_mode == 2 && bleIsAdvertising)) {
        NimBLEDevice::startAdvertising();
      }
      ensureAP(false);
    }
    if (scanInProgress) {
      int16_t r = WiFi.scanComplete();
      if (r != WIFI_SCAN_RUNNING) { WiFi.scanDelete(); scanInProgress = false; }
    }
    return;
  }

  // ── AP-Client verbunden? -> STA-Suche komplett aussetzen ───────────────────
  // Sobald ein Client (z.B. dein Handy) am AP haengt, wird JEDE STA-Aktivitaet
  // (Scannen/Verbinden) gestoppt. Der STA-Scan laesst die Funkeinheit durch alle
  // Kanaele springen und stoert dabei den AP -> genau das killt sonst DHCP und
  // Verbindung. Solange also jemand am AP ist, bleibt der AP absolut ungestoert.
  // (Hinweis: Eine BESTEHENDE Heimnetz-Verbindung wird oben behandelt und bleibt
  //  erhalten - ein fertiger STA-Link stoert den AP nicht, nur das Suchen tut es.)
  // Sobald der letzte Client weg ist, laeuft die STA-Suche automatisch weiter.
  if (WiFi.softAPgetStationNum() > 0) {
    if (scanInProgress) { WiFi.scanDelete(); scanInProgress = false; }
    if (staConnecting) {
      // laufenden (non-blocking) Verbindungsversuch sauber abbrechen
      staConnecting = false;
      WiFi.disconnect(false, false);   // STA-Versuch stoppen, AP + Funk behalten
    }
    staDownSince     = 0;      // kein Haenger -> Notnagel-Neustart NICHT ausloesen
    lastReconnectTry = now;    // nach Client-Weggang erst wieder nach Intervall scannen
    apClientGoneAt   = 0;      // Nachlauf zuruecksetzen (laeuft erst NACH Weggang)
    return;
  }

  // Nachlauf-Sperre: der letzte AP-Client ist gerade weg. Fuer die naechsten
  // STA_SUPPRESS_AFTER_AP_MS NICHT scannen -> ein direkt erneut verbindendes
  // Geraet hat Ruhe, und es kommt kein sofortiger Stoerscan. Danach freigeben.
  if (apClientGoneAt != 0) {
    if (now - apClientGoneAt < STA_SUPPRESS_AFTER_AP_MS) {
      if (scanInProgress) { WiFi.scanDelete(); scanInProgress = false; }
      staDownSince     = 0;    // kein Haenger -> Notnagel nicht ausloesen
      lastReconnectTry = now;
      return;
    }
    apClientGoneAt = 0;        // Nachlauf abgelaufen -> normale Suche wieder frei
  }

  // Ab hier NICHT verbunden und KEIN Client am AP.
  // Ein laenger fehlendes Heimnetz ist ein normaler Zustand (z.B. unterwegs),
  // kein Beweis fuer einen haengenden WLAN-Stack. Darum gibt es hier bewusst
  // KEINEN zeitbasierten Hard-Reset mehr. Echte Fehler werden weiterhin dort
  // repariert, wo sie sicher erkennbar sind: Scan-Timeout und wiederholte
  // Verbindungs-Timeouts.
  staDownSince = 0;

  // ── Phase VERBINDET: non-blocking Verbindungsaufbau laeuft -> Status pollen ─
  // Kein blockierendes Warten! Der AP wird waehrenddessen normal weiter bedient.
  if (staConnecting) {
    if (now - staConnectStart > STA_CONNECT_TIMEOUT_MS) {
      // Timeout -> Versuch abbrechen.
      staConnecting = false;
      staConnectFails++;
      dlog("WiFi: connect timeout (fail #%d)\n", staConnectFails);
      lastReconnectTry = now;   // Pause bis zum naechsten Scan
      if (staConnectFails >= 3) {
        dlog("WiFi: hard STA reset after repeated failures\n");
        WiFi.disconnect(false, false);
        delay(200);
        wifiMulti = WiFiMulti();
        for (auto &n : cfg_wifi) wifiMulti.addAP(n.ssid.c_str(), n.pass.c_str());
        staConnectFails = 0;
      }
    }
    return;   // solange nicht verbunden/timeout -> weiter pollen (AP laeuft)
  }

  // ── Phase 1: kein Scan laeuft -> nach staScanInterval einen ASYNC-Scan ─────
  if (!scanInProgress) {
    if (now - lastReconnectTry < staScanInterval) return;
    lastReconnectTry = now;
    int16_t started = WiFi.scanNetworks(true, true);   // async, inkl. hidden
    if (started == WIFI_SCAN_RUNNING) {
      diagScanCount++;                 // Diagnose: gestarteten Scan zaehlen
      scanInProgress = true;
      scanStartTime  = now;
    } else {
      WiFi.scanDelete();
      scanInProgress = false;
    }
    return;
  }

  // ── Phase 2: Scan laeuft -> Ergebnis pollen (ohne zu blockieren) ──────────
  int16_t res = WiFi.scanComplete();

  if (res == WIFI_SCAN_RUNNING) {
    if (now - scanStartTime > 12000) {
      dlog("WiFi: scan timeout -> reset scan state\n");
      WiFi.scanDelete();
      scanInProgress = false;
      lastReconnectTry = now;
    }
    return;
  }

  // Scan fertig: das STAERKSTE bekannte Netz aus den Ergebnissen suchen.
  int bestIdx = -1;
  int32_t bestRssi = -1000;
  if (res > 0) {
    for (int i = 0; i < res; i++) {
      for (auto &w : cfg_wifi) {
        if (WiFi.SSID(i) == w.ssid) {
          int32_t r = WiFi.RSSI(i);
          if (r > bestRssi) { bestRssi = r; bestIdx = i; }
          break;
        }
      }
    }
  }
  String bestSsid = (bestIdx >= 0) ? WiFi.SSID(bestIdx) : String("");
  WiFi.scanDelete();
  scanInProgress = false;

  if (bestIdx < 0) {
    // Kein bekanntes Netz in Reichweite (typisch: unterwegs). Exakte Stufen:
    // 20s -> 40s -> 60s -> 120s -> 120s -> 120s ...
    advanceStaScanBackoff(now);
    dlog("WiFi: no known network -> next scan in %lus (stage %u/%u)\n",
         (unsigned long)(staScanInterval / 1000),
         (unsigned)(staScanBackoffStage + 1),
         (unsigned)(STA_SCAN_BACKOFF_LAST + 1));
    return;
  }

  // Bekanntes Netz gefunden -> NON-BLOCKING verbinden. Das Backoff wird erst
  // nach einer TATSAECHLICH erfolgreichen Verbindung auf 20s zurueckgesetzt.
  // Scheitert die Verbindung, bleibt die erreichte Warte-Stufe erhalten.
  String pass = "";
  for (auto &w : cfg_wifi) {
    if (w.ssid == bestSsid) { pass = w.pass; break; }
  }
  if (WiFi.getMode() != WIFI_AP_STA) WiFi.mode(WIFI_AP_STA);
  dlog("WiFi: known network '%s' (RSSI %d) -> connecting (nonblocking)\n",
                bestSsid.c_str(), (int)bestRssi);
  staBegin(bestSsid, pass);   // IP-Config (static/DHCP) VOR begin, dann verbinden
  staConnecting   = true;
  staConnectStart = now;
}

// ── Modul-Setup ───────────────────────────────────────────────────────────────
void wifiBleSetup() {
  if (cfg_ble_name.isEmpty()) cfg_ble_name = DEFAULT_BLE_NAME;

  // ── Einmalige Bond-Migration 1.x -> 2.x ─────────────────────────────────────
  // NimBLE 2.x speichert Bonds in einem NEUEN NVS-Format. Ohne Migration waeren
  // alle bestehenden Kopplungen (Windows-PCs, Handys) nach dem Firmware-Update
  // kaputt ("verbunden, aber keine Daten"). Die Bibliothek bringt dafuer einen
  // offiziellen Konverter mit, der VOR NimBLEDevice::init() laufen muss.
  // Der Lauf ist idempotent (bereits konvertierte/leere Eintraege werden
  // uebersprungen); ueber das NVS-Flag laeuft er trotzdem nur genau EINMAL.
  prefs.begin("vesccfg", false);
  if (!prefs.getBool("bondmig2", false)) {
    bool migOk = NimBLEBondMigration::migrateBondStoreToCurrent();
    prefs.putBool("bondmig2", true);
    dlog("BLE bond migration 1.x -> 2.x: %s\n", migOk ? "OK" : "FAILED (re-pairing required)");
  }
  prefs.end();

  NimBLEDevice::init(cfg_ble_name.c_str());
  // NimBLE 2.x: setPower nimmt direkt dBm (int8_t) statt esp_power_level_t.
  // 9 dBm entspricht dem bisherigen ESP_PWR_LVL_P9 (Maximalleistung).
  NimBLEDevice::setPower(9);
  // Pairing/Bonding fuer VESC Tool unter Windows. Je nach Config-Haken:
  //   - ohne BLE-PIN: "Just Works" -> Kopplung wird automatisch angenommen.
  //   - mit  BLE-PIN: statischer 6-stelliger Passkey muss eingegeben werden.
  // Handys/Apps, die wie bisher unverschluesselt verbinden, laufen unveraendert:
  // die Characteristics verlangen keine Verschluesselung, Pairing ist optional.
  applyBleSecurity();

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  // NimBLE 2.x: automatisches Re-Advertising nach Disconnect ist per Default
  // AUS (in 1.x war es an). Wir wollen es aus — die Advertising-Steuerung liegt
  // vollstaendig bei onDisconnect()/handleBleMode(). Explizit setzen, damit die
  // Absicht dokumentiert ist und ein kuenftiger Default-Wechsel nichts aendert.
  pServer->advertiseOnDisconnect(false);

  NimBLEService *pService = pServer->createService(VESC_SERVICE_UUID);
  // Characteristic-Properties abhaengig vom BLE-PIN-Haken:
  //   PIN AUS -> wie bisher: offene Properties, jede App kann ungekoppelt
  //              verbinden und kommunizieren (Just Works ist rein optional).
  //   PIN AN  -> Verschluesselung + Authentifizierung ERZWINGEN. Ungekoppelte
  //              Clients bekommen beim ersten Zugriff "insufficient
  //              authentication" -> Android/Windows starten dann AUTOMATISCH
  //              die Kopplung mit PIN-Abfrage. Ohne richtige PIN keine
  //              VESC-Kommunikation.
  // WICHTIG: Properties werden bei der Erstellung festgelegt -> ein Umschalten
  // des PIN-Hakens wirkt auf diesen Schutz erst nach einem NEUSTART (die
  // Kopplungs-PIN selbst gilt sofort). Die Oberflaeche weist darauf hin.
  uint32_t txProps = NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ;
  uint32_t rxProps = NIMBLE_PROPERTY::WRITE  | NIMBLE_PROPERTY::WRITE_NR;
  if (cfg_ble_pin_enabled) {
    txProps |= NIMBLE_PROPERTY::READ_ENC  | NIMBLE_PROPERTY::READ_AUTHEN;
    rxProps |= NIMBLE_PROPERTY::WRITE_ENC | NIMBLE_PROPERTY::WRITE_AUTHEN;
  }
  pCharacteristicVescTx = pService->createCharacteristic(VESC_CHARACTERISTIC_UUID_TX, txProps);
  pCharacteristicVescRx = pService->createCharacteristic(VESC_CHARACTERISTIC_UUID_RX, rxProps);
  pCharacteristicVescRx->setCallbacks(new MyCallbacks());

  // NimBLE 2.x: pService->start() entfaellt — Services starten automatisch mit
  // pServer->start(). Der fruehere Aufruf ist als wirkungsloses No-Op deprecated.
  pServer->start();

  NimBLEAdvertising *pAdv = NimBLEDevice::getAdvertising();
  pAdv->addServiceUUID(VESC_SERVICE_UUID);
  // NimBLE 2.x: Der Geraetename wird NICHT mehr automatisch advertised und die
  // Scan-Response ist per Default AUS (in 1.x war beides automatisch an).
  // Ohne diese zwei Zeilen sendet das Advertising nur Flags + Service-UUID —
  // die VESC-App findet das Geraet dann nicht, obwohl es advertised.
  // Reihenfolge wichtig: erst Scan-Response aktivieren, DANN den Namen setzen —
  // so landet der Name im Scan-Response-Paket (31 Bytes extra Platz) und
  // kollidiert auch bei langen Namen nie mit der 128-bit-UUID im Adv-Paket.
  pAdv->enableScanResponse(true);
  pAdv->setName(cfg_ble_name.c_str());
  applyAdvInterval(false);   // Start im schnellen Discovery-Intervall
  // Beim Boot Advertising nur starten wenn Modus nicht "Aus" ist.
  // Auto-Modus startet ebenfalls AN (laut Konfig-Wunsch).
  if (cfg_ble_mode != 0) {
    pAdv->start();
    bleIsAdvertising = true;
    dlog("BLE advertising: %s\n", cfg_ble_name.c_str());
  } else {
    bleIsAdvertising = false;
    dlog("BLE mode: OFF (no advertising at boot)\n");
  }
  // Initialer Bewegungs-Zeitstempel fuer Auto-Modus
  lastMovementTime = millis();

  // WiFi-Event-Handler registrieren BEVOR WiFi gestartet wird.
  WiFi.onEvent(onWiFiEvent);
  // WiFi-Config NICHT im Flash persistieren — die IDF speichert sonst bei jedem
  // Mode-/Connect-Wechsel ins NVS, was beim Boot zu korrupten/halben Zustaenden
  // fuehren kann ("manchmal kein WLAN"). Wir verwalten die Config selbst.
  WiFi.persistent(false);
  // Auto-Reconnect der IDF ausschalten — wir steuern Reconnect selbst und
  // kontrolliert, damit der AP dabei nie unbeabsichtigt fällt.
  WiFi.setAutoReconnect(false);
  // AP-Konfiguration setzen, BEVOR der WiFi-Treiber das erste Beacon sendet.
  // Ein direktes WiFi.mode(WIFI_AP_STA) wuerde zuerst die Default-SSID
  // ESP_XXXX starten und WiFi.softAP() wuerde sie erst danach ersetzen.
  // startApStaCleanFromOff() initialisiert deshalb ohne Start, schreibt AP3 und
  // startet den Treiber erst im letzten Schritt.
  if (!startApStaCleanFromOff(1)) {
    // Sicherheits-Fallback: sollte die interne Arduino-Initialisierung in einer
    // kuenftigen Core-Version geaendert werden, bleibt der AP trotzdem erreichbar.
    dlog("AP preconfig: fallback to normal WiFi start\n");
    WiFi.mode(WIFI_AP_STA);
  }
  // WiFi-Powersave: MIN_MODEM ist bei gleichzeitigem BLE (VESC) auf dem ESP32-S3
  // Pflicht (sonst abort + Boot-Schleife). NICHT WIFI_PS_NONE verwenden.
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  // AP IMMER zuerst fertig konfigurieren — er soll dauerhaft laufen,
  // unabhaengig von STA. Da die SSID bereits gesetzt ist, erzeugt ensureAP()
  // keinen Stop/Start-Wechsel mehr.
  setupAccessPoint();

  // Danach STA verbinden (blockierend beim Boot, das ist ok).
  bool staOK = setupWiFiClient();
  (void)staOK;

  // Der erste asynchrone Suchlauf beginnt 20 Sekunden NACH Abschluss des
  // Boot-Verbindungsversuchs. Danach gilt 20 -> 40 -> 60 -> 120 -> 120 ...
  resetStaScanBackoff(millis(), true);

  // AP nach STA-Connect nochmal absichern — ABER nur, wenn STA tatsaechlich
  // verbunden ist. Nur dann kann sich der AP-Channel verschoben haben (eine
  // Funkeinheit = ein Channel), was ein AP-Neustart auf dem STA-Channel
  // erfordert. Ohne STA-Verbindung laeuft der AP nach setupAccessPoint() bereits
  // korrekt auf Channel 1 — ein weiterer ensureAP()-Aufruf wuerde hier nur ein
  // ueberfluessiges AP-Stop/Start ausloesen (sichtbares Boot-Flackern).
  if (WiFi.status() == WL_CONNECTED) {
    ensureAP(false);
  }
}

// ── Modul-Loop ────────────────────────────────────────────────────────────────
void wifiBleLoop() {
  // ── SICHERHEIT: im Werkszustand (noch NICHTS gespeichert) AP hart erzwingen ──
  // Nach einem Werksreset ist der NVS leer -> cfg_configured = false. Dann ist der
  // AP der einzige Zugang und MUSS an bleiben, egal welcher Modus/Timeout. Sobald
  // der Nutzer einmal speichert (cfg_configured = true), gilt sein gewaehlter
  // AP-Modus — und Auto darf dann auch OHNE STA-Netz abschalten.
  // Zugleich der "darf der AP ueberhaupt aus?"-Check: nur wenn konfiguriert UND
  // Auto-Modus. Sonst niemals.
  bool apMayShutOff = cfg_configured && (cfg_ap_mode == 2);
  if (!cfg_configured) {
    apWanted       = true;
    apOffByTimeout = false;
    if (!apActive) {
      dlog("AP safety: factory state (unconfigured) -> forcing AP\n");
      if (WiFi.getMode() != WIFI_AP_STA) WiFi.mode(WIFI_AP_STA);
      ensureAP(true);
    }
  }

  // AP-Modus (analog BLE-Modus):
  //   1 = An   : AP immer an (klassisch), Timeout-Logik inaktiv.
  //   2 = Auto : verhaelt sich wie BLE-Auto — fahren haelt den AP wach, Stillstand
  //              ohne AP-Client schaltet ihn nach cfg_ap_timeout Sek. ab, naechste
  //              Bewegung holt ihn zurueck. KEIN "Aus" (AP ist der Fallback-Zugang).
  //
  // Modus-Wechsel zur Laufzeit sauber behandeln (ohne Reboot, wie BLE):
  static int lastApMode = -1;
  if (cfg_ap_mode != lastApMode) {
    if (lastApMode != -1) {
      if (cfg_ap_mode == 1 && !apActive) {
        // Auto -> An: AP sofort wieder hochziehen (STA bleibt unberuehrt).
        apWanted = true;
        apOffByTimeout = false;
        if (WiFi.getMode() != WIFI_AP_STA) WiFi.mode(WIFI_AP_STA);
        ensureAP(true);
        dlog("AP mode -> ON: AP restored\n");
      } else if (cfg_ap_mode == 2) {
        // An -> Auto: Idle-Timer frisch starten, sonst wuerde ref=apStartTime
        // (evtl. lange her) sofort einen Timeout ausloesen.
        apLastClientGone = millis();
        apLastStationNum = WiFi.softAPgetStationNum();
        dlog("AP mode -> AUTO: idle timer started\n");
      }
    }
    lastApMode = cfg_ap_mode;
  }

  // AP-Idle-Timeout (nur im Auto-Modus): schaltet den AP ab, wenn niemand am AP
  // haengt und nicht gefahren wird. Danach haelt der Watchdog ihn NICHT mehr am
  // Leben. BEWUSST NICHT an eine STA-Verbindung gekoppelt — auf dem fahrenden
  // Scooter gibt es meist kein Heim-WLAN, ein STA-Gate wuerde den Timeout dort
  // komplett aushebeln.
  // Rueckweg: naechste Bewegung holt den AP zurueck (siehe Wake-Block unten).
  // Steht der Scooter dauerhaft ohne STA-Netz, ist der ESP bis zur naechsten
  // Bewegung/Reboot nicht per WLAN erreichbar — im Auto-Modus so gewollt.
  if (apActive && apMayShutOff && cfg_ap_timeout > 0) {
    int stations = WiFi.softAPgetStationNum();
    // Flanke erkennen: ist gerade das letzte Geraet abgefallen?
    if (stations == 0 && apLastStationNum > 0) {
      apLastClientGone = millis();   // Timer ab JETZT neu starten
      dlog("AP: last client left — idle timer restarted\n");
    }
    apLastStationNum = stations;

    // Bewegung haelt den AP wach — 1:1 wie der BLE-Auto-Modus. Solange der
    // VESC verbunden ist und |ERPM| ueber der Schwelle liegt, wird die
    // Referenzzeit laufend aufgefrischt -> der Timer laeuft nie ab, solange
    // gefahren wird.
    if (vescStatus.connected) {
      int32_t absErpm = vescStatus.erpm < 0 ? -vescStatus.erpm : vescStatus.erpm;
      if (absErpm > cfg_ble_auto_erpm_on) {
        apLastClientGone = millis();   // Referenzzeit auffrischen = Timer reset
      }
    }

    // Referenzzeit fuer den Timeout:
    //  - wenn nie ein Client da war: ab AP-Start
    //  - wenn ein Client weg ist:    ab Trennung
    //  - bei Bewegung (s.o.):         laufend aufgefrischt
    // Solange ein Client verbunden ist, wird der Timer nicht ausgewertet.
    unsigned long ref = (apLastClientGone > 0) ? apLastClientGone : apStartTime;

    if (stations == 0 && millis() - ref > (unsigned long)cfg_ap_timeout * 1000UL) {
      dlog("AP auto: idle timeout — shutting down AP, keeping STA\n");
      apWanted = false;
      // Nur das AP-Interface deaktivieren, die korrekte AP-Konfiguration aber
      // im RAM behalten. softAPdisconnect(true) loescht sie und wuerde beim
      // spaeteren Wake zuerst wieder einen ESP_XXXX-Default-AP erzeugen.
      WiFi.mode(WIFI_STA);
      apActive = false;
      isAPMode = false;
      apOffByTimeout = true;   // fuer Wake-on-Move merken
    }
  }

  // AP-Rueckkehr bei Bewegung — 1:1 wie BT-Auto beim Aufwecken. Ist der AP per
  // Idle-Timeout aus und der Scooter faehrt wieder an (|ERPM| > Schwelle), wird
  // der AP wieder hochgezogen. WLAN/STA bleibt dabei unberuehrt.
  if (apOffByTimeout && cfg_ap_mode == 2 && !apActive) {
    int32_t absErpm = vescStatus.erpm < 0 ? -vescStatus.erpm : vescStatus.erpm;
    if (vescStatus.connected && absErpm > cfg_ble_auto_erpm_on) {
      dlog("AP wake: movement (erpm=%d) -> AP back on\n", (int)vescStatus.erpm);
      // apWanted MUSS gesetzt werden, sobald ERPM ueber der Schwelle liegt —
      // damit halten auch Watchdog/Safety den AP ab jetzt am Leben.
      apWanted = true;
      apLastClientGone = millis();   // Timer frisch starten, damit er nicht sofort wieder ablaeuft
      if (WiFi.getMode() != WIFI_AP_STA) WiFi.mode(WIFI_AP_STA);
      // force=true: echten softAP()-Start erzwingen (Zombie-AP-Schutz, s. ensureAP).
      // apOffByTimeout erst bei ERFOLG loeschen — schlaegt der Start fehl (z.B.
      // Kollision mit laufendem STA-Scan), feuert dieser Block im naechsten
      // Loop-Durchlauf erneut, bis der AP wirklich sendet.
      if (ensureAP(true)) {
        apOffByTimeout = false;
      } else {
        dlog("AP wake: start failed -> retry next loop\n");
      }
    }
  }

  // AP-Reconcile-Watchdog: gleicht alle 5 s SOLL (apWanted) und IST (sendet der
  // AP unsere SSID?) ab — und korrigiert in BEIDE Richtungen:
  //   soll AN, sendet nicht  -> anschalten (ensureAP)
  //   soll AUS, sendet noch   -> abschalten (softAPdisconnect)
  // apWanted ist die einzige Wahrheit fuer den Soll-Zustand: true ausser der AP
  // wurde im Auto-Modus per Idle-Timeout abgeschaltet. Im Werkszustand
  // (unkonfiguriert) ist apWanted oben bereits hart auf true gezwungen.
  static int apWatchdogFails = 0;
  if (millis() - lastApEnsure > 5000) {
    lastApEnsure = millis();

    // Zwei voneinander unabhaengige Modusquellen pruefen:
    //  1. Arduino-WiFi-Wrapper
    //  2. direkter ESP-IDF-Treiberzustand
    // Eine gespeicherte SSID oder AP-IP ist KEIN Beweis, dass der AP noch sendet;
    // beide Werte koennen nach WiFi.mode(WIFI_STA) im Treiber erhalten bleiben.
    wifi_mode_t arduinoMode = WiFi.getMode();
    bool arduinoApEnabled = (arduinoMode == WIFI_AP || arduinoMode == WIFI_AP_STA);

    wifi_mode_t idfMode = WIFI_MODE_NULL;
    esp_err_t idfModeResult = esp_wifi_get_mode(&idfMode);
    bool idfApEnabled = (idfModeResult == ESP_OK) &&
                        (idfMode == WIFI_MODE_AP || idfMode == WIFI_MODE_APSTA);

    bool apModeEnabled = arduinoApEnabled || idfApEnabled;
    String runningSsid = WiFi.softAPSSID();
    bool apSsidOk = (runningSsid == cfg_ap_ssid);

    // Wenn beide Modusquellen sagen, dass AP aus ist, ist ein eventuell noch
    // gesetztes AP_START-Merkbit nur veraltet. Synchronisieren, aber NICHT neu
    // abschalten oder loggen. Genau das verhindert die bisherige Log-Endlosschleife.
    if (!apModeEnabled) {
      apStartedByEvent = false;
    }

    if (apWanted) {
      // ── SOLL AN ──────────────────────────────────────────────────────────
      // Gesund ist der AP nur, wenn der AP-Modus wirklich aktiv ist, ein
      // AP_START-Event empfangen wurde UND die laufende SSID stimmt. Dadurch
      // repariert der Watchdog auch einen unerwartet gestoppten AP, obwohl
      // softAPSSID() weiterhin die alte, gespeicherte SSID zurückliefert.
      bool apReallyRunning = apModeEnabled && apStartedByEvent && apSsidOk;
      if (!apReallyRunning) {
        apWatchdogFails++;
        diagApWatchdogFires++;
        dlog("AP reconcile: should be ON, not healthy (mode=%d/%d event=%d ssid='%s', fail #%d) -> ensureAP\n",
             (int)arduinoMode,
             (idfModeResult == ESP_OK) ? (int)idfMode : -1,
             apStartedByEvent ? 1 : 0,
             runningSsid.c_str(),
             apWatchdogFails);
        ensureAP(true);
      } else {
        apActive = true;
        isAPMode = true;
        apWatchdogFails = 0;
      }
    } else {
      // ── SOLL AUS ─────────────────────────────────────────────────────────
      // Nur ein tatsächlich noch aktivierter AP-Modus bedeutet, dass der AP
      // noch senden KANN. Die weiterhin gespeicherte SSID wird bewusst nicht
      // berücksichtigt. So bleibt die Prüfung erhalten und repariert echte
      // Probleme, ohne bei korrekt ausgeschaltetem AP ständig erneut auszulösen.
      if (apModeEnabled) {
        diagApWatchdogFires++;
        dlog("AP reconcile: should be OFF, AP mode still active (arduino=%d idf=%d event=%d) -> shutting down AP\n",
             (int)arduinoMode,
             (idfModeResult == ESP_OK) ? (int)idfMode : -1,
             apStartedByEvent ? 1 : 0);

        // AP-Modus abschalten, Konfiguration aber behalten. Dadurch startet
        // ein spaeteres Aktivieren sofort wieder mit cfg_ap_ssid statt ESP_XXXX.
        WiFi.mode(WIFI_STA);
        apActive = false;
        isAPMode = false;
      } else {
        // Korrekt aus: interne Statusflags leise synchron halten.
        apActive = false;
        isAPMode = false;
        apWatchdogFails = 0;
      }
    }
  }

  // Non-blocking WiFi-Reconnect (async Scan, friert den Loop nicht ein)
  handleWiFiReconnect();

  // RSSI-basiertes Roaming: zu staerkerem AP gleicher SSID wechseln
  handleRoaming();

  // BLE-Modus (Aus / An / Auto)
  handleBleMode();

  // Advertising-Intervall an WLAN-Bedarf anpassen (Airtime sparen bei Idle)
  manageAdvInterval();

  // Auto reboot
  if (cfg_autoreboot && cfg_autoreboot_time > 0) {
    static unsigned long lastConnected = millis();
    bool anyConnected = deviceConnected || (wifiClient && wifiClient.connected());
    if (!cfg_autoreboot_no_wifi && WiFi.status() == WL_CONNECTED) anyConnected = true;
    if (WiFi.softAPgetStationNum() > 0) anyConnected = true;
    if (anyConnected) lastConnected = millis();
    else if (millis() - lastConnected > (unsigned long)cfg_autoreboot_time * 1000UL) {
      Serial.println("Auto reboot: no client connected");
      bootDiagMarkPlannedRestart("Auto reboot: no client connected");
      ledsOff();
      delay(500);
      ESP.restart();
    }
  }
}

#endif // VESC_BRIDGE_UNITY_BUILD