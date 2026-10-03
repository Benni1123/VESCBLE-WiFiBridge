#pragma once

#include <Arduino.h>

void vescSetup();
void vescTcpSetup();
void vescLoop();
bool webUiActive();

// ── ERPM-Simulation (nur Debug) ─────────────────────────────────────────────
// Setzt einen simulierten ERPM-Wert fuer sec Sekunden (max. 300). sec <= 0
// beendet sie sofort. Gedacht zum Pruefen der bewegungsabhaengigen Funktionen
// ohne Fahrt; der Wert laeuft immer von selbst ab.
void   vescSimSet(int32_t erpm, int sec);
bool   vescSimActive();
String vescSimStatusJson();
