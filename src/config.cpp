// Konfiguration und NVS
// Diese Datei wird ueber main.cpp als Unity-Build eingebunden.
// Dadurch bleiben die bisherigen static-Sichtbarkeiten und Abhaengigkeiten exakt erhalten,
// waehrend der Quellcode logisch in einzelne Dateien aufgeteilt ist.
#if defined(VESC_BRIDGE_UNITY_BUILD)
#include "globals.h"
#include "config.h"


// Setzt die statische IP-Konfig inkl. DNS-Fallback. Gewuenschter DNS wird
// primaer gesetzt, ein fester oeffentlicher DNS sekundaer -> faellt der primaere
// aus, bleibt die Namensaufloesung (z.B. fuer den Update-Check) funktionsfaehig.
bool applyStaticConfig(const String &ipS, const String &gwS,
                              const String &subS, const String &dnsS) {
  IPAddress ip, gw, sub;
  if (!(ip.fromString(ipS) && gw.fromString(gwS) && sub.fromString(subS)))
    return false;
  IPAddress dns1;
  if (dnsS.length() > 0 && dns1.fromString(dnsS)) {
    IPAddress dns2 = (dns1 == FALLBACK_DNS_PRIMARY)
                       ? FALLBACK_DNS_SECONDARY : FALLBACK_DNS_PRIMARY;
    return WiFi.config(ip, gw, sub, dns1, dns2);
  }
  return WiFi.config(ip, gw, sub, FALLBACK_DNS_PRIMARY, FALLBACK_DNS_SECONDARY);
}

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
  // Reparatur bereits gespeicherter, durch \/ -Escaping beschaedigter URLs
  // (kamen von einer aelteren App-Version mit org.json). Eine gueltige URL hat
  // NIE einen Backslash -> \/ zurueck zu / wandeln. Ohne dies wuerde die alte
  // kaputte URL aus dem NVS weiter verwendet, bis manuell neu gespeichert wird.
  cfg_update_url.replace("\\/", "/");
  cfg_version_url.replace("\\/", "/");
  cfg_rx_pin      = prefs.getInt   ("rx_pin",      VESC_RX_PIN);
  cfg_tx_pin      = prefs.getInt   ("tx_pin",      VESC_TX_PIN);
  cfg_autoreboot         = prefs.getBool("autoreboot",       false);
  cfg_autoreboot_time    = prefs.getInt ("autoreboot_time",  300);
  cfg_autoreboot_no_wifi = prefs.getBool("autoreboot_nw",  false);
  cfg_debug              = prefs.getBool("debug",            false);
  cfg_log_size           = prefs.getInt ("log_size",         50);
  cfg_debug_filter       = prefs.getInt ("debug_filter",     7);
  cfg_roam_enabled       = prefs.getBool("roam_en",          false);
  cfg_roam_threshold     = prefs.getInt ("roam_thr",         -75);
  cfg_roam_hysteresis    = prefs.getInt ("roam_hyst",        12);
  cfg_autopoll_enabled   = prefs.getBool("autopoll_en",      false);
  cfg_autopoll_interval  = prefs.getInt ("autopoll_int",     5);
  cfg_ble_mode           = prefs.getInt ("ble_mode",         1);
  cfg_ble_auto_erpm_on   = prefs.getInt ("ble_erpm_on",      200);
  cfg_ap_mode            = prefs.getInt ("ap_mode",          1);
  cfg_configured         = prefs.getBool("configured",       false);
  cfg_ble_pin_enabled    = prefs.getBool("ble_pin_en",       false);
  cfg_ble_pin            = prefs.getInt ("ble_pin",          123456);
  cfg_ble_auto_off_sec   = prefs.getInt ("ble_off_sec",      120);
  cfg_ble_full_power     = prefs.getBool("ble_fullpwr",      false);
  cfg_leds_enabled       = prefs.getBool("leds_en",          false);
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
  // Migration fuer Bestandsgeraete: wer schon WLAN-Netze gespeichert hat, gilt als
  // konfiguriert (auch wenn der neue "configured"-Key noch fehlt) -> AP folgt dem
  // gewaehlten Modus statt zwangsweise "An".
  if (!cfg_configured && !cfg_wifi.empty()) cfg_configured = true;
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
  if (cfg_autopoll_interval < 1)   cfg_autopoll_interval = 1;
  if (cfg_autopoll_interval > 60)  cfg_autopoll_interval = 60;
  if (cfg_ble_mode < 0 || cfg_ble_mode > 2) cfg_ble_mode = 1;
  if (cfg_ble_auto_erpm_on < 10)    cfg_ble_auto_erpm_on = 10;
  if (cfg_ble_pin < 0 || cfg_ble_pin > 999999) cfg_ble_pin = 123456;  // 6-stelliger Passkey
  if (cfg_ble_auto_erpm_on > 50000) cfg_ble_auto_erpm_on = 50000;
  if (cfg_ble_auto_off_sec < 5)     cfg_ble_auto_off_sec = 5;
  if (cfg_ble_auto_off_sec > 3600)  cfg_ble_auto_off_sec = 3600;
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
  prefs.putBool  ("autoreboot_nw",  cfg_autoreboot_no_wifi);
  prefs.putBool  ("debug",       cfg_debug);
  prefs.putInt   ("log_size",    cfg_log_size);
  prefs.putInt   ("debug_filter",cfg_debug_filter);
  prefs.putBool  ("roam_en",     cfg_roam_enabled);
  prefs.putInt   ("roam_thr",    cfg_roam_threshold);
  prefs.putInt   ("roam_hyst",   cfg_roam_hysteresis);
  prefs.putBool  ("autopoll_en", cfg_autopoll_enabled);
  prefs.putInt   ("autopoll_int",cfg_autopoll_interval);
  prefs.putInt   ("ble_mode",    cfg_ble_mode);
  prefs.putInt   ("ble_erpm_on", cfg_ble_auto_erpm_on);
  prefs.putInt   ("ap_mode",     cfg_ap_mode);
  cfg_configured = true;                         // ab jetzt konfiguriert
  prefs.putBool  ("configured",  cfg_configured);
  prefs.putBool  ("ble_pin_en",  cfg_ble_pin_enabled);
  prefs.putInt   ("ble_pin",     cfg_ble_pin);
  prefs.putInt   ("ble_off_sec", cfg_ble_auto_off_sec);
  prefs.putBool  ("ble_fullpwr", cfg_ble_full_power);
  prefs.putBool  ("leds_en",     cfg_leds_enabled);
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

#endif // VESC_BRIDGE_UNITY_BUILD
