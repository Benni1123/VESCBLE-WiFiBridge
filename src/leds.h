#ifndef LEDS_H
#define LEDS_H

#include <WebServer.h>

// Registriert die /leds-Seite und die LED-API-Endpoints am uebergebenen
// Webserver. Wird einmal in setup() aus main.cpp aufgerufen.
void ledsSetup(WebServer *server);

// Wird im loop() aus main.cpp aufgerufen. Bekommt den aktuellen ERPM-Wert
// uebergeben (fuer spaetere bewegungsabhaengige Effekte). Treibt die
// non-blocking Animation (z.B. Knight Rider) voran.
void ledsLoop(int32_t erpm);

// Schaltet ALLE LED-Kanaele hart aus (alle Pixel schwarz). Muss vor jedem
// Neustart (ESP.restart) und vor einem OTA-Flash aufgerufen werden, damit die
// LEDs nicht im letzten Frame haengen bleiben.
void ledsOff();

#endif // LEDS_H