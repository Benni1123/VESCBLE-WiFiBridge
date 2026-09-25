// Absturzabbild aus dem Flash lesen
// Diese Datei wird ueber main.cpp als Unity-Build eingebunden.
// Dadurch bleiben die bisherigen static-Sichtbarkeiten und Abhaengigkeiten exakt erhalten,
// waehrend der Quellcode logisch in einzelne Dateien aufgeteilt ist.
#if defined(VESC_BRIDGE_UNITY_BUILD)
#include "globals.h"
#include "debuglog.h"
#include "logship.h"
#include "coredump.h"

#include <esp_partition.h>
#include <esp_app_desc.h>

#if defined(__has_include)
#  if __has_include(<esp_core_dump.h>)
#    include <esp_core_dump.h>
#    define CD_HAVE_API 1
#  endif
#endif
#ifndef CD_HAVE_API
#  define CD_HAVE_API 0
#endif

// ── Zustand ─────────────────────────────────────────────────────────────────
static bool     cdPresent   = false;   // gueltiges Abbild in der Partition
static size_t   cdAddr      = 0;
static size_t   cdSize      = 0;
static String   cdTask;                // Name des abgestuerzten Tasks
static uint32_t cdPc        = 0;       // Absturzadresse
static String   cdBacktrace;           // Aufrufkette als Adressliste
static String   cdElfSha;              // SHA256 des Firmware-Abbilds im Dump
static String   cdRunSha;              // SHA256 der gerade laufenden Firmware
static bool     cdSameBuild = false;   // stammt der Dump von DIESEM Build?

// ── Partition finden ────────────────────────────────────────────────────────
static const esp_partition_t *cdPartition() {
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                  ESP_PARTITION_SUBTYPE_DATA_COREDUMP, nullptr);
}

// ── Beim Start auswerten ────────────────────────────────────────────────────
void coreDumpSetup() {
#if !CD_HAVE_API
  Serial.println("[COREDUMP] API nicht verfuegbar - uebersprungen");
  return;
#else
  // image_check() prueft Pruefsumme und Format. Ohne diesen Schritt wuerde ein
  // halb geschriebenes oder von einer aelteren Firmware stammendes Abbild als
  // gueltig gelten, und die Adressen darin zeigten ins Leere.
  if (esp_core_dump_image_check() != ESP_OK) {
    return;                                   // nichts da oder unbrauchbar
  }

  size_t addr = 0, size = 0;
  if (esp_core_dump_image_get(&addr, &size) != ESP_OK || size == 0) {
    return;
  }
  cdPresent = true;
  cdAddr    = addr;
  cdSize    = size;

  logShipAdd("[COREDUMP] Absturzabbild gefunden: " + String((unsigned)size) +
             " Byte bei 0x" + String((unsigned long)addr, HEX));

  // ── Kurzfassung ───────────────────────────────────────────────────────────
  // Der Taskname ist hier das Wertvollste. Er sagt ohne jedes weitere
  // Werkzeug, WELCHER Teil der Firmware abgestuerzt ist — Hauptschleife,
  // WLAN-Ereignisse, BLE oder einer der eigenen Tasks. Die Adressen brauchen
  // zum Aufloesen die firmware.elf des betroffenen Builds, der Name nicht.
  esp_core_dump_summary_t *sum =
      (esp_core_dump_summary_t *)malloc(sizeof(esp_core_dump_summary_t));
  if (!sum) {
    logShipAdd("[COREDUMP] zu wenig Speicher fuer die Kurzfassung");
    return;
  }

  if (esp_core_dump_get_summary(sum) == ESP_OK) {
    char taskName[sizeof(sum->exc_task) + 1];
    memcpy(taskName, sum->exc_task, sizeof(sum->exc_task));
    taskName[sizeof(sum->exc_task)] = 0;
    cdTask = String(taskName);
    cdPc   = sum->exc_pc;

    logShipAdd("[COREDUMP] Task='" + cdTask + "' PC=0x" +
               String((unsigned long)cdPc, HEX));

    // Aufrufkette. Die IDF markiert sie als beschaedigt, wenn der Stapel beim
    // Absturz schon zerstoert war — dann sind die Adressen mit Vorsicht zu
    // lesen, und genau das gehoert in die Zeile.
    String bt;
    uint32_t depth = sum->exc_bt_info.depth;
    if (depth > (sizeof(sum->exc_bt_info.bt) / sizeof(sum->exc_bt_info.bt[0]))) {
      depth = sizeof(sum->exc_bt_info.bt) / sizeof(sum->exc_bt_info.bt[0]);
    }
    for (uint32_t i = 0; i < depth; i++) {
      if (bt.length()) bt += " ";
      bt += "0x" + String((unsigned long)sum->exc_bt_info.bt[i], HEX);
    }
    if (sum->exc_bt_info.corrupted) bt += " (beschaedigt)";
    cdBacktrace = bt;
    if (bt.length()) logShipAdd("[COREDUMP] Backtrace: " + bt);

    // Die Pruefsumme des Firmware-Abbilds: nur mit GENAU dieser firmware.elf
    // ergeben die Adressen oben die richtigen Zeilen.
    //
    // Die IDF legt sie bereits als lesbare Zeichen ab, nicht als rohe Bytes.
    // Sie noch einmal nach Hex zu wandeln machte aus "02b56d25" die Ziffernfolge
    // "3032623536643235" — die Hex-Werte der einzelnen Schriftzeichen. Deshalb
    // hier nur kopieren und am ersten Nullbyte abschneiden.
    char shaBuf[sizeof(sum->app_elf_sha256) + 1];
    memcpy(shaBuf, sum->app_elf_sha256, sizeof(sum->app_elf_sha256));
    shaBuf[sizeof(sum->app_elf_sha256)] = 0;
    for (size_t i = 0; i < sizeof(shaBuf); i++) {
      // Nicht druckbare Zeichen beenden die Kennung — sonst landet Datenmuell
      // in der Logzeile.
      if (shaBuf[i] != 0 && (shaBuf[i] < 32 || shaBuf[i] > 126)) { shaBuf[i] = 0; break; }
    }
    // ── Kennung des Firmware-Abbilds ────────────────────────────────────────
    //
    // Frueher wurde hier geraten, in welchem Format das Feld vorliegt: mal als
    // lesbarer Text, mal als rohe Bytes. Beides kam vor, und beide Male war
    // die Anzeige bei einem der Faelle falsch — einmal stand die Hex-Form der
    // Schriftzeichen da, einmal blieb der Vergleichswert leer.
    //
    // Deshalb jetzt ohne jede Annahme: der VERGLEICH laeuft ueber die rohen
    // Bytes und ist damit immer richtig. Fuer die ANZEIGE werden die ersten
    // acht Bytes einheitlich in Hex gewandelt — dieselbe Darstellung, die auch
    // das Ablege-Skript fuer die Dateinamen benutzt, sodass beide zueinander
    // passen.
    auto toHex8 = [](const uint8_t *raw) {
      String out;
      for (size_t i = 0; i < 8; i++) {
        char b[3];
        snprintf(b, sizeof(b), "%02x", raw[i]);
        out += b;
      }
      return out;
    };

    cdElfSha = toHex8(sum->app_elf_sha256);

    const esp_app_desc_t *desc = esp_app_get_description();
    if (desc) {
      cdRunSha    = toHex8(desc->app_elf_sha256);
      cdSameBuild = (memcmp(sum->app_elf_sha256, desc->app_elf_sha256,
                            sizeof(sum->app_elf_sha256)) == 0);
      logShipAdd("[COREDUMP] Firmware-Kennung: Abbild=" + cdElfSha +
                 " laufend=" + cdRunSha +
                 (cdSameBuild ? " -> GLEICHER Build, Adressen passen zur aktuellen firmware.elf"
                              : " -> ANDERER Build, das Abbild stammt aus einer frueheren Version"));
    } else {
      logShipAdd("[COREDUMP] Firmware-Kennung (Abbild): " + cdElfSha);
    }
  } else {
    logShipAdd("[COREDUMP] Kurzfassung nicht lesbar - rohes Abbild per /api/coredump holen");
  }
  free(sum);

  logShipAdd("[COREDUMP] Abbild bleibt erhalten, bis es geloescht oder vom naechsten Absturz ueberschrieben wird");
#endif
}

// ── Download des rohen Abbilds ──────────────────────────────────────────────
void coreDumpRegisterRoutes(WebServer &srv) {
  srv.on("/api/coredump", HTTP_GET, [&srv]() {
    const esp_partition_t *part = cdPartition();
    if (!part) {
      srv.send(404, "text/plain", "no coredump partition\n");
      return;
    }
    if (!cdPresent || cdSize == 0) {
      srv.send(404, "text/plain", "no valid core dump stored\n");
      return;
    }

    // Blockweise ausliefern. Das Abbild ist bis zu 64 KB gross — es am Stueck
    // in den Speicher zu holen waere bei rund 90 KB freiem Heap leichtsinnig.
    const size_t CHUNK = 1024;
    uint8_t *buf = (uint8_t *)malloc(CHUNK);
    if (!buf) {
      srv.send(500, "text/plain", "out of memory\n");
      return;
    }

    srv.setContentLength(cdSize);
    srv.sendHeader("Content-Disposition", "attachment; filename=\"coredump.bin\"");
    srv.send(200, "application/octet-stream", "");

    // Offset relativ zum Partitionsanfang: esp_core_dump_image_get() liefert
    // eine absolute Flash-Adresse, esp_partition_read() erwartet dagegen einen
    // Versatz innerhalb der Partition.
    size_t off = (cdAddr >= part->address) ? (cdAddr - part->address) : 0;
    size_t left = cdSize;
    while (left > 0) {
      size_t n = (left > CHUNK) ? CHUNK : left;
      if (esp_partition_read(part, off, buf, n) != ESP_OK) break;
      if (srv.client().write(buf, n) != n) break;
      off  += n;
      left -= n;
    }
    free(buf);
    srv.client().stop();
  });

  srv.on("/api/coredump/clear", HTTP_POST, [&srv]() {
#if CD_HAVE_API
    esp_err_t e = esp_core_dump_image_erase();
    if (e == ESP_OK) {
      cdPresent = false;
      cdSize    = 0;
      cdTask    = "";
      cdPc      = 0;
      cdBacktrace = "";
      srv.send(200, "application/json", "{\"ok\":true}");
    } else {
      srv.send(500, "application/json",
               String("{\"ok\":false,\"err\":\"erase failed (") + String((int)e) + ")\"}");
    }
#else
    srv.send(501, "application/json", "{\"ok\":false,\"err\":\"API nicht verfuegbar\"}");
#endif
  });
}

// ── Status ──────────────────────────────────────────────────────────────────
String coreDumpStatusJson() {
  String j = "{";
  j += "\"available\":" + String(CD_HAVE_API ? "true" : "false");
  j += ",\"present\":"  + String(cdPresent ? "true" : "false");
  j += ",\"size\":"     + String((unsigned)cdSize);
  j += ",\"task\":\""   + jsonEscapeDebug(cdTask) + "\"";
  j += ",\"pc\":\"0x"   + String((unsigned long)cdPc, HEX) + "\"";
  j += ",\"backtrace\":\"" + jsonEscapeDebug(cdBacktrace) + "\"";
  j += ",\"elf_sha\":\"" + jsonEscapeDebug(cdElfSha) + "\"";
  j += ",\"run_sha\":\"" + jsonEscapeDebug(cdRunSha) + "\"";
  j += ",\"same_build\":" + String(cdSameBuild ? "true" : "false");
  j += "}";
  return j;
}

#endif // VESC_BRIDGE_UNITY_BUILD
