#ifndef LEDS_H
#define LEDS_H

#include <WebServer.h>

// Registriert die /leds-Seite und die LED-API-Endpoints am uebergebenen
// Webserver. Wird einmal in setup() aus main.cpp aufgerufen.
void ledsSetup(WebServer *server);

// Blankt die LED-Strips so frueh wie moeglich in setup() (VOR WiFi/BLE), um
// den WS2812-Einschalt-Glitch zu minimieren. Registriert KEINE HTTP-Routes.
// Idempotent: mehrfach aufrufbar (No-Op nach dem 1. Mal). ledsSetup() ruft es
// spaeter ohnehin auf, falls es hier nicht schon geschehen ist.
void ledsInitStripsEarly();

// Wird im loop() aus main.cpp aufgerufen. Bekommt den aktuellen ERPM-Wert
// uebergeben (fuer spaetere bewegungsabhaengige Effekte). Treibt die
// non-blocking Animation (z.B. Knight Rider) voran.
void ledsLoop(int32_t erpm);

// Startet den LED-Render-Task auf Kern 1. EINMAL in setup() nach ledsSetup()
// aufrufen. Danach laeuft das LED-Rendering unabhaengig vom Haupt-loop().
void ledsStartTask();

// Wird im loop() aus main.cpp aufgerufen (billig): meldet dem LED-Task, ob die
// LED-Steuerung aktiv ist und den aktuellen ERPM-Wert. Ersetzt den frueheren
// direkten ledsLoop()-Aufruf aus dem loop().
void ledsUpdateState(bool enabled, int32_t erpm);

// Schaltet ALLE LED-Kanaele hart aus (alle Pixel schwarz). Muss vor jedem
// Neustart (ESP.restart) und vor einem OTA-Flash aufgerufen werden, damit die
// LEDs nicht im letzten Frame haengen bleiben.
void ledsOff();

#endif // LEDS_H