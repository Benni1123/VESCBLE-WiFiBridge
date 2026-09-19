#pragma once

#include <Arduino.h>

// ── Log-Versand an einen HTTP-Server (NDJSON) ────────────────────────────────
// Sammelt Logzeilen in einem grossen Ringpuffer (PSRAM) und schiebt sie, sobald
// das Heimnetz erreichbar ist, als NDJSON-Batch per HTTP POST an einen Server.
// Sinn: WLAN-Aussetzer lassen sich nicht live mitlesen — der Puffer ueberbrueckt
// die Ausfallzeit und alles wird nach dem Reconnect nachgeliefert.
//
// Der Versand laeuft in einem EIGENEN Task (Kern 0). Er darf niemals aus dem
// Hauptloop heraus blockieren: ein TLS-Handshake kann mehrere Sekunden dauern.

// Legt den Ringpuffer an. MUSS vor captureBootDiagnostics() laufen, damit die
// Boot-/Resetdiagnose bereits mit erfasst wird. Idempotent.
void logShipSetup();

// Startet den Sende-Task. EINMAL in setup() aufrufen, nachdem WiFi/BLE stehen.
void logShipStartTask();

// Uebernimmt cfg_logship_enabled/-url/-token in den modul-eigenen Schnappschuss,
// den der Sende-Task benutzt. Nach JEDER Aenderung dieser Werte aufrufen (die
// Webhandler tun das). Ohne diesen Schnappschuss wuerde der Task auf Kern 0 in
// Strings lesen, die ein Webhandler auf Kern 1 gerade neu allokiert.
void logShipApplyConfig();

// Haengt eine Zeile in den Sendepuffer. Thread-safe, nicht blockierend,
// unabhaengig von cfg_debug. Wird aus dlog(), uartLogAdd(), wifiEventLog()
// und der Bootdiagnose aufgerufen.
void logShipAdd(const String &line);

// Verwirft alle gepufferten Zeilen (API/Debug-Tab).
void logShipClear();

// Stoesst einen Sendeversuch sofort an, statt auf den naechsten Zyklus zu
// warten (Button "Jetzt senden" im Debug-Tab).
void logShipRequestFlush();

// Schreibt eine Testzeile in den Puffer und erzwingt den Versand.
void logShipSendTestLine();

// Zustand fuer /api/logship/status und den Debug-Tab.
String logShipStatusJson();
