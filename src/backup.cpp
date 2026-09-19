// Sicherung und Wiederherstellung der kompletten Konfiguration
// Diese Datei wird ueber main.cpp als Unity-Build eingebunden.
#if defined(VESC_BRIDGE_UNITY_BUILD)
#include "globals.h"
#include "debuglog.h"
#include "backup.h"

#include <nvs.h>
#include <mbedtls/base64.h>

// Die drei Namespaces, in denen die Firmware ihre Einstellungen ablegt.
// vesccfg: config.cpp, leds: leds.cpp, ledpat: die gespeicherten Muster.
static const char *BACKUP_NS[] = { "vesccfg", "leds", "ledpat" };
static const size_t BACKUP_NS_COUNT = sizeof(BACKUP_NS) / sizeof(BACKUP_NS[0]);

// Groesster Blob, den wir sichern bzw. einspielen. Ein Muster sind
// 300 Pixel * 3 Byte = 900 Byte; 2048 laesst Luft fuer kuenftige Daten,
// ohne dass ein einzelner Puffer den Heap belastet.
#define BACKUP_BLOB_MAX 2048

// ── Hilfen ───────────────────────────────────────────────────────────────────

static String backupJsonEscape(const String &in) {
  String out;
  out.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); i++) {
    char c = in.charAt(i);
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        // Steuerzeichen wuerden das JSON zerlegen -> weglassen.
        if ((uint8_t)c >= 0x20) out += c;
        break;
    }
  }
  return out;
}

static String backupTypeName(nvs_type_t t) {
  switch (t) {
    case NVS_TYPE_U8:   return "u8";
    case NVS_TYPE_I8:   return "i8";
    case NVS_TYPE_U16:  return "u16";
    case NVS_TYPE_I16:  return "i16";
    case NVS_TYPE_U32:  return "u32";
    case NVS_TYPE_I32:  return "i32";
    case NVS_TYPE_U64:  return "u64";
    case NVS_TYPE_I64:  return "i64";
    case NVS_TYPE_STR:  return "str";
    case NVS_TYPE_BLOB: return "blob";
    default:            return "";
  }
}

// ── Sicherung: NVS durchgehen und zeilenweise senden ─────────────────────────

// Liefert die NDJSON-Zeile fuer einen Eintrag, oder einen leeren String, wenn
// der Eintrag nicht lesbar ist. Nicht lesbare Eintraege werden uebersprungen
// statt die ganze Sicherung scheitern zu lassen.
static String backupEntryLine(nvs_handle_t h, const nvs_entry_info_t &info) {
  String head = "{\"ns\":\"" + String(info.namespace_name) +
                "\",\"k\":\"" + String(info.key) +
                "\",\"t\":\"" + backupTypeName(info.type) + "\",\"v\":";

  switch (info.type) {
    case NVS_TYPE_U8:  { uint8_t  v = 0; if (nvs_get_u8 (h, info.key, &v)) return ""; return head + String((unsigned)v) + "}"; }
    case NVS_TYPE_I8:  { int8_t   v = 0; if (nvs_get_i8 (h, info.key, &v)) return ""; return head + String((int)v)      + "}"; }
    case NVS_TYPE_U16: { uint16_t v = 0; if (nvs_get_u16(h, info.key, &v)) return ""; return head + String((unsigned)v) + "}"; }
    case NVS_TYPE_I16: { int16_t  v = 0; if (nvs_get_i16(h, info.key, &v)) return ""; return head + String((int)v)      + "}"; }
    case NVS_TYPE_U32: { uint32_t v = 0; if (nvs_get_u32(h, info.key, &v)) return ""; return head + String((unsigned long)v) + "}"; }
    case NVS_TYPE_I32: { int32_t  v = 0; if (nvs_get_i32(h, info.key, &v)) return ""; return head + String((long)v)     + "}"; }
    case NVS_TYPE_U64: { uint64_t v = 0; if (nvs_get_u64(h, info.key, &v)) return ""; return head + String((unsigned long long)v) + "}"; }
    case NVS_TYPE_I64: { int64_t  v = 0; if (nvs_get_i64(h, info.key, &v)) return ""; return head + String((long long)v) + "}"; }

    case NVS_TYPE_STR: {
      size_t len = 0;
      if (nvs_get_str(h, info.key, nullptr, &len) != ESP_OK) return "";
      if (len == 0 || len > BACKUP_BLOB_MAX) return "";
      char *buf = (char *)malloc(len);
      if (!buf) return "";
      String line;
      if (nvs_get_str(h, info.key, buf, &len) == ESP_OK) {
        line = head + "\"" + backupJsonEscape(String(buf)) + "\"}";
      }
      free(buf);
      return line;
    }

    case NVS_TYPE_BLOB: {
      size_t len = 0;
      if (nvs_get_blob(h, info.key, nullptr, &len) != ESP_OK) return "";
      if (len == 0 || len > BACKUP_BLOB_MAX) return "";
      uint8_t *raw = (uint8_t *)malloc(len);
      if (!raw) return "";
      String line;
      if (nvs_get_blob(h, info.key, raw, &len) == ESP_OK) {
        size_t b64len = 0;
        // Erst die benoetigte Groesse erfragen, dann genau so viel belegen.
        mbedtls_base64_encode(nullptr, 0, &b64len, raw, len);
        uint8_t *b64 = (uint8_t *)malloc(b64len + 1);
        if (b64) {
          size_t written = 0;
          if (mbedtls_base64_encode(b64, b64len, &written, raw, len) == 0) {
            b64[written] = 0;
            line = head + "\"" + String((char *)b64) + "\"}";
          }
          free(b64);
        }
      }
      free(raw);
      return line;
    }

    default:
      return "";
  }
}

static void backupHandleDownload(WebServer &srv) {
  srv.sendHeader("Content-Disposition",
                 "attachment; filename=\"vesc-backup-" + cfg_ble_name + ".ndjson\"");
  srv.sendHeader("Cache-Control", "no-store");
  // Gestreamt statt am Stueck: die Datei kann rund 20 KB gross werden, und
  // ein String dieser Groesse waere bei fragmentiertem Heap riskant.
  srv.setContentLength(CONTENT_LENGTH_UNKNOWN);
  srv.send(200, "application/x-ndjson", "");

  srv.sendContent("{\"vesc_backup\":1,\"fw\":\"" + String(FIRMWARE_VERSION) +
                  "\",\"dev\":\"" + backupJsonEscape(cfg_ble_name) +
                  "\",\"uptime\":" + String(millis() / 1000UL) + "}\n");

  uint32_t total = 0, skipped = 0;
  for (size_t n = 0; n < BACKUP_NS_COUNT; n++) {
    nvs_handle_t h;
    if (nvs_open(BACKUP_NS[n], NVS_READONLY, &h) != ESP_OK) {
      // Namespace existiert noch nicht (z.B. nie LEDs benutzt) -> ueberspringen.
      continue;
    }
    nvs_iterator_t it = nullptr;
    esp_err_t res = nvs_entry_find("nvs", BACKUP_NS[n], NVS_TYPE_ANY, &it);
    while (res == ESP_OK && it != nullptr) {
      nvs_entry_info_t info;
      nvs_entry_info(it, &info);
      String line = backupEntryLine(h, info);
      if (line.length() > 0) { srv.sendContent(line + "\n"); total++; }
      else                   { skipped++; }
      res = nvs_entry_next(&it);
    }
    if (it) nvs_release_iterator(it);
    nvs_close(h);
  }

  srv.sendContent("");   // schliesst die Chunked-Uebertragung ab
  dlog("Backup: %lu entries sent, %lu skipped\n",
       (unsigned long)total, (unsigned long)skipped);
}

// ── Wiederherstellung: zeilenweise waehrend des Uploads ──────────────────────

static String   restoreLine;        // aktuell einlaufende Zeile
static uint32_t restoreApplied = 0;
static uint32_t restoreFailed  = 0;
static bool     restoreHeaderOk = false;
static bool     restoreTooLong  = false;

// Holt einen String-Wert aus der Zeile: "key":"...".
// Nimmt Escapes zurueck, kennt also \" \\ \n \r \t.
static String restoreStr(const String &line, const String &key) {
  String pat = "\"" + key + "\":\"";
  int st = line.indexOf(pat);
  if (st < 0) return "";
  st += pat.length();
  String out;
  for (int i = st; i < (int)line.length(); i++) {
    char c = line.charAt(i);
    if (c == '"') break;
    if (c == '\\' && i + 1 < (int)line.length()) {
      char e = line.charAt(++i);
      if      (e == 'n')  out += '\n';
      else if (e == 'r')  out += '\r';
      else if (e == 't')  out += '\t';
      else                out += e;      // deckt \" und \\ ab
      continue;
    }
    out += c;
  }
  return out;
}

// Holt einen Zahlenwert aus der Zeile: "v":123 (ohne Anfuehrungszeichen).
static bool restoreNum(const String &line, long long &out) {
  int st = line.indexOf("\"v\":");
  if (st < 0) return false;
  st += 4;
  if (st < (int)line.length() && line.charAt(st) == '"') return false;  // ist ein String
  int en = st;
  if (en < (int)line.length() && (line.charAt(en) == '-' || line.charAt(en) == '+')) en++;
  int digits = 0;
  while (en < (int)line.length() && line.charAt(en) >= '0' && line.charAt(en) <= '9') { en++; digits++; }
  if (digits == 0) return false;
  out = atoll(line.substring(st, en).c_str());
  return true;
}

static void restoreApplyLine(const String &line) {
  if (line.length() < 5) return;

  // Kopfzeile: nur pruefen, nichts schreiben.
  if (line.indexOf("\"vesc_backup\"") >= 0) {
    restoreHeaderOk = true;
    dlog("Restore: header ok (%s)\n", line.c_str());
    return;
  }
  // Ohne gueltige Kopfzeile wird nichts uebernommen. Schutz davor, dass eine
  // beliebige hochgeladene Datei in den NVS geschrieben wird.
  if (!restoreHeaderOk) { restoreFailed++; return; }

  String ns = restoreStr(line, "ns");
  String k  = restoreStr(line, "k");
  String t  = restoreStr(line, "t");
  if (ns.isEmpty() || k.isEmpty() || t.isEmpty()) { restoreFailed++; return; }

  // Nur die bekannten Namespaces annehmen.
  bool nsOk = false;
  for (size_t i = 0; i < BACKUP_NS_COUNT; i++) {
    if (ns == BACKUP_NS[i]) { nsOk = true; break; }
  }
  if (!nsOk) { restoreFailed++; return; }

  nvs_handle_t h;
  if (nvs_open(ns.c_str(), NVS_READWRITE, &h) != ESP_OK) { restoreFailed++; return; }

  esp_err_t err = ESP_FAIL;
  if (t == "str") {
    err = nvs_set_str(h, k.c_str(), restoreStr(line, "v").c_str());
  } else if (t == "blob") {
    String b64 = restoreStr(line, "v");
    size_t outLen = 0;
    // Erst Groesse erfragen (liefert MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL und
    // setzt outLen), dann genau so viel belegen.
    mbedtls_base64_decode(nullptr, 0, &outLen,
                          (const uint8_t *)b64.c_str(), b64.length());
    if (outLen > 0 && outLen <= BACKUP_BLOB_MAX) {
      uint8_t *raw = (uint8_t *)malloc(outLen);
      if (raw) {
        size_t written = 0;
        if (mbedtls_base64_decode(raw, outLen, &written,
                                  (const uint8_t *)b64.c_str(), b64.length()) == 0) {
          err = nvs_set_blob(h, k.c_str(), raw, written);
        }
        free(raw);
      }
    }
  } else {
    long long v = 0;
    if (restoreNum(line, v)) {
      if      (t == "u8")  err = nvs_set_u8 (h, k.c_str(), (uint8_t)v);
      else if (t == "i8")  err = nvs_set_i8 (h, k.c_str(), (int8_t)v);
      else if (t == "u16") err = nvs_set_u16(h, k.c_str(), (uint16_t)v);
      else if (t == "i16") err = nvs_set_i16(h, k.c_str(), (int16_t)v);
      else if (t == "u32") err = nvs_set_u32(h, k.c_str(), (uint32_t)v);
      else if (t == "i32") err = nvs_set_i32(h, k.c_str(), (int32_t)v);
      else if (t == "u64") err = nvs_set_u64(h, k.c_str(), (uint64_t)v);
      else if (t == "i64") err = nvs_set_i64(h, k.c_str(), (int64_t)v);
    }
  }

  if (err == ESP_OK) { nvs_commit(h); restoreApplied++; }
  else               { restoreFailed++; }
  nvs_close(h);
}

// Upload-Handler: bekommt die Datei in Haeppchen. Es wird IMMER nur eine Zeile
// im Speicher gehalten, die Dateigroesse spielt also keine Rolle.
static void backupHandleRestoreUpload(WebServer &srv) {
  HTTPUpload &up = srv.upload();

  if (up.status == UPLOAD_FILE_START) {
    restoreLine = "";
    restoreApplied = 0;
    restoreFailed  = 0;
    restoreHeaderOk = false;
    restoreTooLong  = false;
    restoreLine.reserve(600);
    dlog("Restore: upload started (%s)\n", up.filename.c_str());
    return;
  }

  if (up.status == UPLOAD_FILE_WRITE) {
    for (size_t i = 0; i < up.currentSize; i++) {
      char c = (char)up.buf[i];
      if (c == '\n') {
        String l = restoreLine;
        restoreLine = "";
        l.trim();
        if (restoreTooLong) {
          // Diese Zeile war unvollstaendig -> nicht anwenden, sonst landet
          // ein halber Wert im NVS.
          restoreTooLong = false;
          restoreFailed++;
        } else if (l.length() > 0) {
          restoreApplyLine(l);
        }
      } else if (c != '\r') {
        // Obergrenze gegen eine Datei ohne Zeilenumbrueche: sonst waechst der
        // String bis der Speicher ausgeht. 4096 deckt auch den groessten
        // base64-Blob (2048 Byte -> rund 2732 Zeichen) mit Reserve ab.
        if (restoreLine.length() < 4096) restoreLine += c;
        else                             restoreTooLong = true;
      }
    }
    return;
  }

  if (up.status == UPLOAD_FILE_END) {
    // Letzte Zeile ohne abschliessenden Umbruch nicht verlieren.
    String l = restoreLine;
    restoreLine = "";
    l.trim();
    if (!restoreTooLong && l.length() > 0) restoreApplyLine(l);
    dlog("Restore: finished, %lu applied, %lu failed\n",
         (unsigned long)restoreApplied, (unsigned long)restoreFailed);
  }
}

static void backupHandleRestoreFinish(WebServer &srv) {
  String json = "{\"header_ok\":" + String(restoreHeaderOk ? "true" : "false") +
                ",\"applied\":" + String(restoreApplied) +
                ",\"failed\":"  + String(restoreFailed) + "}";

  if (!restoreHeaderOk || restoreApplied == 0) {
    // Nichts uebernommen -> auch nicht neu starten.
    srv.send(400, "application/json", json);
    return;
  }

  srv.send(200, "application/json", json);
  // Neustart, damit alles frisch aus dem NVS geladen wird. Ohne ihn liefen
  // die alten Werte im RAM weiter und ein spaeteres Speichern wuerde die
  // gerade eingespielten wieder ueberschreiben.
  bootDiagMarkPlannedRestart("Config restored from backup");
  ledsOff();
  delay(500);
  ESP.restart();
}

void backupRegisterRoutes(WebServer &srv) {
  srv.on("/api/backup", HTTP_GET, [&srv]() { backupHandleDownload(srv); });
  srv.on("/api/restore", HTTP_POST,
         [&srv]() { backupHandleRestoreFinish(srv); },
         [&srv]() { backupHandleRestoreUpload(srv); });
}

#endif // VESC_BRIDGE_UNITY_BUILD
