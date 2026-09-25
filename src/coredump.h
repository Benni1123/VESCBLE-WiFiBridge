#pragma once

#include <Arduino.h>
#include <WebServer.h>

// ── Absturzabbild aus dem Flash lesen ────────────────────────────────────────
//
// Die ESP-IDF schreibt bei einem Panic ein vollstaendiges Abbild des Zustands
// in eine eigene Flash-Partition: Register, Aufrufkette, Stapel aller Tasks.
// Genau die Angaben, die nach einem Neustart sonst fehlen — der Resetgrund
// sagt nur "Panic", nicht wo und warum.
//
// Der wichtigste Punkt: Das passiert BEREITS, ohne jede Aenderung. Die
// Arduino-Bibliotheken fuer den S3 sind mit CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
// gebaut, und die Partitionstabelle min_spiffs.csv enthaelt am Ende
//
//     coredump, data, coredump, 0x3F0000, 0x10000
//
// also 64 KB dafuer. Die Abbilder lagen dort schon die ganze Zeit, nur hat sie
// niemand ausgelesen. Dieses Modul holt sie ab.
//
// Zwei Wege:
//
//   1. Beim Start eine Kurzfassung ins Log: Abbild vorhanden, wie gross, und —
//      soweit die IDF es hergibt — Absturzadresse, betroffener Task und die
//      Aufrufkette. Das reicht meist schon, um die Stelle zu finden.
//
//   2. Das rohe Abbild per HTTP herunterladen und mit esp-coredump auswerten.
//      Damit gibt es die vollstaendige Analyse ohne USB-Kabel:
//
//        python -m esp_coredump info_corefile -c coredump.bin -t raw firmware.elf
//
//      Wichtig: firmware.elf muss aus GENAU dem Build stammen, der abgestuerzt
//      ist — sonst zeigen die Adressen auf falsche Zeilen.
//
// Geloescht wird ein Abbild nur auf ausdrueckliche Anforderung. Von selbst
// verschwindet es erst, wenn der naechste Absturz es ueberschreibt.

// Prueft beim Start, ob ein Abbild vorliegt, und schreibt die Kurzfassung ins
// Log. In setup() nach logShipSetup() aufrufen.
void coreDumpSetup();

// Registriert /api/coredump (GET, Download) und /api/coredump/clear (POST).
void coreDumpRegisterRoutes(WebServer &srv);

// Zustand fuer /api/info und den Debug-Tab.
String coreDumpStatusJson();
