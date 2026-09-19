#pragma once

#include <Arduino.h>
#include <WebServer.h>

// ── Vollstaendige Sicherung und Wiederherstellung der Konfiguration ──────────
//
// Gesichert wird NICHT eine von Hand gepflegte Feldliste, sondern der rohe
// Inhalt der NVS-Namespaces. Grund: Jede Aufzaehlung veraltet beim naechsten
// neuen Konfigurationsfeld, und man merkt es erst, wenn nach einem Werksreset
// ausgerechnet dieses eine Feld fehlt. Die generische Variante nimmt alles
// mit, auch kuenftige Schluessel, ohne dass hier etwas nachgezogen werden muss.
//
// Erfasste Namespaces:
//   vesccfg  - Geraetekonfiguration (WLAN, BLE, AP, VESC, Log-Upload, ...)
//   leds     - LED-Kanaele (Pins, Effekte, Farben, Tempo, ...)
//   ledpat   - die 12 gespeicherten Muster inklusive der Pixeldaten (Blobs)
//
// Format: NDJSON, ein Eintrag pro Zeile. Zeilenweise ist Absicht — so laesst
// sich die Datei beim Einspielen Zeile fuer Zeile abarbeiten, ohne sie
// komplett in den Speicher zu legen. Bei 12 vollen Mustern kommen rund 20 KB
// zusammen, und der groesste zusammenhaengende Heap-Block liegt im Betrieb
// bei 30 bis 60 KB — ein String dieser Groesse waere ein unnoetiges Risiko.
//
//   {"vesc_backup":1,"fw":"1.2.7","dev":"G30","uptime":123}
//   {"ns":"vesccfg","k":"ble_name","t":"str","v":"G30"}
//   {"ns":"vesccfg","k":"port","t":"i32","v":65101}
//   {"ns":"ledpat","k":"p0d","t":"blob","v":"<base64>"}

// Registriert /api/backup (GET) und /api/restore (POST) am Webserver.
// Aus webUiSetup() aufzurufen.
void backupRegisterRoutes(WebServer &srv);
