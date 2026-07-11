#pragma once

#include <Arduino.h>

void applyBleSecurity();
void wifiBleSetup();
void wifiBleLoop();
bool bleAdvertisingActive();


// Vollstaendige ESP-IDF-WLAN-Trennungsdiagnose.
const char *wifiDisconnectReasonName(uint8_t reason);
String wifiDisconnectReasonsJson();

// Debug/API: AP manuell einschalten, ohne die gespeicherte AP-Konfiguration
// oder den gewaehlten AP-Modus dauerhaft zu veraendern. Im Auto-Modus beginnt
// danach lediglich der normale Idle-Timeout erneut.
bool startAccessPointManual();

// Liefert den aktuellen AP-Zustand als JSON fuer den REST-Endpunkt.
String accessPointStatusJson(bool operationOk);
