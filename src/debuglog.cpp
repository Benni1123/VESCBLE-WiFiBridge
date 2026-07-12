// Debug-, Boot- und UART-Log
// Diese Datei wird ueber main.cpp als Unity-Build eingebunden.
// Dadurch bleiben die bisherigen static-Sichtbarkeiten und Abhaengigkeiten exakt erhalten,
// waehrend der Quellcode logisch in einzelne Dateien aufgeteilt ist.
#if defined(VESC_BRIDGE_UNITY_BUILD)
#include "globals.h"
#include "debuglog.h"
#include "time-service.h"


#ifndef RTC_NOINIT_ATTR
#define RTC_NOINIT_ATTR
#endif

// ── UART-Log ──────────────────────────────────────────────────────────────────
static std::vector<String> uartLog;

static String logSanitize(String line) {
  // uartLog wird 1:1 in ein JSON-Array eingebettet. Zeichen entschaerfen,
  // die sonst das JSON zerstoeren koennten.
  line.replace("\\", "/");
  line.replace("\"", "'");
  line.replace("\r", " ");
  line.replace("\n", " ");
  return line;
}

void uartLogAdd(const String &line) {
  if (!cfg_debug) return;
  String clean = logSanitize(line);
  if (clean.isEmpty()) return;
  uartLog.push_back(timeServiceLogStamp() + " " + clean);
  while ((int)uartLog.size() > cfg_log_size) uartLog.erase(uartLog.begin());
}

// ── Geplanter Neustart: Grund ueber den Reset hinweg behalten ─────────────────
// RTC_NOINIT ueberlebt ESP.restart(), Panic und Watchdog-Resets, wird aber nur
// ausgewertet, wenn esp_reset_reason() wirklich einen Software-Reset meldet.
// Dadurch kann ein alter Marker einen Brownout/Panic niemals falsch beschriften.
struct PlannedRestartMarker {
  uint32_t magic;
  uint32_t checksum;
  char reason[96];
};

static const uint32_t RESTART_MARKER_MAGIC = 0x56425247UL; // "VBRG"
RTC_NOINIT_ATTR static PlannedRestartMarker rtcRestartMarker;

static uint32_t restartMarkerChecksum(const char *text) {
  uint32_t hash = 2166136261UL; // FNV-1a
  if (!text) return hash;
  for (size_t i = 0; i < sizeof(rtcRestartMarker.reason) && text[i]; i++) {
    hash ^= (uint8_t)text[i];
    hash *= 16777619UL;
  }
  return hash;
}

void bootDiagClearPlannedRestart() {
  rtcRestartMarker.magic = 0;
  rtcRestartMarker.checksum = 0;
  rtcRestartMarker.reason[0] = 0;
}

void bootDiagMarkPlannedRestart(const char *reason) {
  // magic zuerst loeschen und zuletzt setzen: so ist ein halb geschriebener
  // Marker nach einem sehr ungluecklichen Reset automatisch ungueltig.
  rtcRestartMarker.magic = 0;
  memset(rtcRestartMarker.reason, 0, sizeof(rtcRestartMarker.reason));
  snprintf(rtcRestartMarker.reason, sizeof(rtcRestartMarker.reason), "%s",
           (reason && reason[0]) ? reason : "Software-Neustart");
  rtcRestartMarker.checksum = restartMarkerChecksum(rtcRestartMarker.reason);
  rtcRestartMarker.magic = RESTART_MARKER_MAGIC;

  String line = String("[SYSTEM] Geplanter Neustart: ") + rtcRestartMarker.reason;
  Serial.println(timeServiceLogStamp() + " " + line);
  uartLogAdd(line); // unabhaengig vom Status-Filter, sofern Debug aktiv ist
}

static String consumePlannedRestartReason(esp_reset_reason_t resetReason) {
  String result;
  bool valid = rtcRestartMarker.magic == RESTART_MARKER_MAGIC &&
               rtcRestartMarker.reason[0] != 0 &&
               rtcRestartMarker.checksum == restartMarkerChecksum(rtcRestartMarker.reason);

  // Nur ein echter Software-Reset darf den Marker verwenden. Bei Brownout,
  // Panic oder Watchdog ist der Resetgrund wichtiger und der Marker wird verworfen.
  if (valid && resetReason == ESP_RST_SW) result = String(rtcRestartMarker.reason);
  bootDiagClearPlannedRestart();
  return result;
}

// ── Reset-/Boot-Diagnose ──────────────────────────────────────────────────────
static esp_reset_reason_t bootResetReason = ESP_RST_UNKNOWN;
static String bootResetName = "UNKNOWN";
static String bootResetDescription = "Unbekannter Resetgrund";
static String bootPlannedReason;
static std::vector<String> bootDiagnosticLines;

esp_reset_reason_t bootGetResetReason() {
  return bootResetReason;
}

const String &bootGetResetName() {
  return bootResetName;
}

const String &bootGetPlannedRestartReason() {
  return bootPlannedReason;
}

static const char *resetReasonName(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:   return "POWER_ON";
    case ESP_RST_EXT:       return "EXTERNAL_RESET";
    case ESP_RST_SW:        return "SOFTWARE_RESTART";
    case ESP_RST_PANIC:     return "PANIC_EXCEPTION";
    case ESP_RST_INT_WDT:   return "INTERRUPT_WATCHDOG";
    case ESP_RST_TASK_WDT:  return "TASK_WATCHDOG";
    case ESP_RST_WDT:       return "OTHER_WATCHDOG";
    case ESP_RST_DEEPSLEEP: return "DEEP_SLEEP_WAKEUP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO_RESET";
    case ESP_RST_UNKNOWN:
    default:                return "UNKNOWN_OTHER";
  }
}

static const char *resetReasonDescription(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:   return "Normales Einschalten / Versorgung angelegt";
    case ESP_RST_EXT:       return "Externer Reset-Pin oder Reset-Taster";
    case ESP_RST_SW:        return "Software-Neustart durch Firmware, API oder Update";
    case ESP_RST_PANIC:     return "Absturz durch Panic, Exception oder abort()";
    case ESP_RST_INT_WDT:   return "Interrupt-Watchdog: Interrupts/CPU zu lange blockiert";
    case ESP_RST_TASK_WDT:  return "Task-Watchdog: Task oder Loop zu lange blockiert";
    case ESP_RST_WDT:       return "Anderer Watchdog-Reset";
    case ESP_RST_DEEPSLEEP: return "Aufgewacht aus Deep Sleep";
    case ESP_RST_BROWNOUT:  return "Spannungseinbruch / Versorgung unter Brownout-Schwelle";
    case ESP_RST_SDIO:      return "Reset ueber SDIO";
    case ESP_RST_UNKNOWN:
    default:                return "Unbekannter oder von dieser Core-Version nicht aufgeloester Reset";
  }
}

bool bootIsWatchdog() {
  return bootResetReason == ESP_RST_INT_WDT ||
         bootResetReason == ESP_RST_TASK_WDT ||
         bootResetReason == ESP_RST_WDT;
}

static String bootWatchdogType() {
  if (bootResetReason == ESP_RST_INT_WDT)  return "interrupt";
  if (bootResetReason == ESP_RST_TASK_WDT) return "task";
  if (bootResetReason == ESP_RST_WDT)      return "other";
  return "";
}

String jsonEscapeDebug(String s) {
  s.replace("\\", "\\\\");
  s.replace("\"", "\\\"");
  s.replace("\r", "\\r");
  s.replace("\n", "\\n");
  return s;
}

static void bootDiagAdd(const String &message) {
  String line = "[BOOT] " + message;
  bootDiagnosticLines.push_back(line);
  Serial.println(line);
}

void captureBootDiagnostics() {
  bootDiagnosticLines.clear();
  bootResetReason = esp_reset_reason();
  bootResetName = resetReasonName(bootResetReason);
  bootResetDescription = resetReasonDescription(bootResetReason);
  bootPlannedReason = consumePlannedRestartReason(bootResetReason);

  bootDiagAdd("Resetgrund: " + bootResetName + " (Code " + String((int)bootResetReason) + ") - " + bootResetDescription);

  if (bootResetReason == ESP_RST_BROWNOUT) {
    bootDiagAdd("!!! BROWNOUT erkannt: Versorgungsspannung ist eingebrochen. Akku/DC-DC, Kabel, Stecker und Kondensatoren pruefen.");
  } else if (bootResetReason == ESP_RST_PANIC) {
    bootDiagAdd("!!! PANIC/EXCEPTION erkannt. Exakte Exception und Backtrace sind nach dem Neustart ohne Core-Dump nicht mehr abrufbar.");
  } else if (bootIsWatchdog()) {
    bootDiagAdd("!!! WATCHDOG erkannt: Typ=" + bootWatchdogType() + ". Hohe Loop-Zeiten und blockierende Netzwerk-/BLE-Aufrufe pruefen.");
  } else if (bootResetReason == ESP_RST_SW) {
    if (!bootPlannedReason.isEmpty())
      bootDiagAdd("Geplanter Software-Neustart: " + bootPlannedReason);
    else
      bootDiagAdd("Software-Neustart ohne Bridge-Marker: moeglich durch Bibliothek, Update-System oder fremden esp_restart()-Aufruf.");
  } else if (bootResetReason == ESP_RST_POWERON) {
    bootDiagAdd("Power-On: normales Einschalten; kein vorheriger Software-Absturz erkannt.");
  } else if (bootResetReason == ESP_RST_DEEPSLEEP) {
    bootDiagAdd("Deep-Sleep-Wakeup-Code: " + String((int)esp_sleep_get_wakeup_cause()));
  }

  bootDiagAdd("Firmware: " + String(FIRMWARE_VERSION) + " | Build: " + String(__DATE__) + " " + String(__TIME__) + " | ESP-IDF: " + String(ESP.getSdkVersion()));
  bootDiagAdd("Chip: " + String(ESP.getChipModel()) + " Rev " + String(ESP.getChipRevision()) +
              " | Kerne: " + String(ESP.getChipCores()) + " | CPU: " + String(ESP.getCpuFreqMHz()) + " MHz");

  uint64_t chipId = ESP.getEfuseMac();
  char idBuf[24];
  snprintf(idBuf, sizeof(idBuf), "%04X%08X",
           (uint16_t)(chipId >> 32), (uint32_t)chipId);
  bootDiagAdd("Chip-ID: " + String(idBuf) + " | Heap frei/min/maxBlock: " +
              String(ESP.getFreeHeap()) + "/" + String(ESP.getMinFreeHeap()) + "/" + String(ESP.getMaxAllocHeap()) + " B");

  bootDiagAdd("Flash: " + String(ESP.getFlashChipSize()) + " B @ " + String(ESP.getFlashChipSpeed()) +
              " Hz | Sketch: " + String(ESP.getSketchSize()) + " B | OTA frei: " + String(ESP.getFreeSketchSpace()) + " B");

  if (ESP.getPsramSize() > 0) {
    bootDiagAdd("PSRAM: " + String(ESP.getFreePsram()) + " / " + String(ESP.getPsramSize()) + " B frei");
  }

  // Boot-Meldungen bleiben separat gespeichert und werden von /api/uart/log
  // immer vor den laufenden Meldungen ausgegeben. Dadurch koennen sie auch bei
  // kleinem Log-Limit nicht von spaeteren WiFi-/BLE-Meldungen verdraengt werden.
}

// Rechnet einen alten, nur mit Uptime gespeicherten Logeintrag auf eine echte
// lokale Uhrzeit zurueck, sobald die Systemzeit gueltig ist.
//
// Beispiel:
//   gespeichert:  "18s [evt] AP started"
//   angezeigt:    "2026-07-12 04:51:53 [uptime 18s] [evt] AP started"
//
// Die Uptime bleibt die unveraenderte Referenz. Die Kalenderzeit wird erst beim
// Abruf von /api/uart/log berechnet. Dadurch werden auch Bootmeldungen mit 0s
// nach einer spaeteren NTP-/Browser-/App-Synchronisierung sinnvoll datiert.
static String uartLogResolveUptimeStamp(const String &line) {
  if (!timeServiceIsValid()) return line;

  const int len = line.length();
  int pos = 0;
  while (pos < len && line.charAt(pos) >= '0' && line.charAt(pos) <= '9') pos++;

  // Nur das von uns erzeugte Format "<Sekunden>s <Text>" umwandeln.
  if (pos == 0 || pos >= len || line.charAt(pos) != 's') return line;
  if (pos + 1 < len && line.charAt(pos + 1) != ' ') return line;

  uint64_t entryUptimeSec = 0;
  for (int i = 0; i < pos; i++) {
    entryUptimeSec = entryUptimeSec * 10ULL + (uint64_t)(line.charAt(i) - '0');
  }

  const uint64_t currentUptimeSec = (uint64_t)millis() / 1000ULL;
  const time_t currentEpoch = timeServiceEpoch();
  if (currentEpoch <= 0 || entryUptimeSec > currentUptimeSec) return line;

  const uint64_t ageSec = currentUptimeSec - entryUptimeSec;
  const time_t entryEpoch = currentEpoch - (time_t)ageSec;

  struct tm localTm;
  memset(&localTm, 0, sizeof(localTm));
  localtime_r(&entryEpoch, &localTm);

  char dateTime[24];
  if (strftime(dateTime, sizeof(dateTime), "%Y-%m-%d %H:%M:%S", &localTm) == 0)
    return line;

  String result = String(dateTime) + " [uptime " + String((unsigned long)entryUptimeSec) + "s]";

  // Das urspruengliche "18s" entfernen, den folgenden Text aber beibehalten.
  if (pos + 1 < len) result += line.substring(pos + 1);
  return result;
}

String uartLogJson() {
  String json = "[";
  bool first = true;
  auto appendLine = [&](String line) {
    if (!first) json += ",";
    first = false;
    line = uartLogResolveUptimeStamp(line);
    json += "\"" + jsonEscapeDebug(logSanitize(line)) + "\"";
  };

  if (cfg_debug) {
    // Boot-/Resetdiagnose ist absichtlich nicht Teil des begrenzten Ringpuffers.
    // 0s wird nach einer Zeitsynchronisierung auf die ungefaehre Bootzeit
    // zurueckgerechnet; ohne gueltige Uhr bleibt weiterhin "0s" sichtbar.
    for (const String &line : bootDiagnosticLines) appendLine("0s " + line);
    for (const String &line : uartLog) appendLine(line);
  }
  json += "]";
  return json;
}

void uartLogClear() {
  uartLog.clear();
}

String bootStatusJson() {
  String json = "{";
  json += "\"reset_reason_code\":" + String((int)bootResetReason) + ",";
  json += "\"reset_reason\":\"" + jsonEscapeDebug(bootResetName) + "\",";
  json += "\"description\":\"" + jsonEscapeDebug(bootResetDescription) + "\",";
  json += "\"planned_restart\":\"" + jsonEscapeDebug(bootPlannedReason) + "\",";
  json += "\"power_on\":" + String(bootResetReason == ESP_RST_POWERON ? "true" : "false") + ",";
  json += "\"software_restart\":" + String(bootResetReason == ESP_RST_SW ? "true" : "false") + ",";
  json += "\"panic_exception\":" + String(bootResetReason == ESP_RST_PANIC ? "true" : "false") + ",";
  json += "\"watchdog\":" + String(bootIsWatchdog() ? "true" : "false") + ",";
  json += "\"watchdog_type\":\"" + bootWatchdogType() + "\",";
  json += "\"brownout\":" + String(bootResetReason == ESP_RST_BROWNOUT ? "true" : "false");
  json += "}";
  return json;
}

// dlog(): BT/WLAN-Statusmeldungen. Gibt IMMER auf Serial aus (wie bisher) und
// schreibt zusaetzlich ins UART-Log auf dem API-Tab, wenn der Debug-Modus an
// ist und der "Status"-Filter (Bit 8) gesetzt ist.
void dlog(const char *fmt, ...) {
  char buf[200];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  Serial.print(buf);                                   // Serial IMMER
  if (!cfg_debug || !(cfg_debug_filter & 8)) return;   // UI nur bei Debug+Status
  size_t len = strlen(buf);
  while (len && (buf[len-1] == '\n' || buf[len-1] == '\r')) buf[--len] = 0;
  if (!len) return;
  uartLogAdd(String(buf));
}

#endif // VESC_BRIDGE_UNITY_BUILD