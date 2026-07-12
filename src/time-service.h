#pragma once

#include <Arduino.h>
#include <time.h>

// Initialisiert Zeitzone und NTP. Ein externer RTC-Baustein wird nicht benutzt.
void timeServiceSetup();

// Non-blocking Zeitdienst. Im Haupt-loop aufrufen.
void timeServiceLoop();

// Setzt die Uhr per Unix-Epoch. Akzeptiert Sekunden oder Millisekunden.
// source kennzeichnet den Ausloeser: api, app, browser oder home_assistant.
bool timeServiceSetEpoch(uint64_t epochValue, String &error,
                         const String &source = "api");

// REST-Antwort fuer GET/POST /api/time.
String timeServiceJson();

// Zeitinformationen fuer /api/info.
bool timeServiceIsValid();
time_t timeServiceEpoch();
String timeServiceLocalString();
String timeServiceUtcString();
String timeServiceSource();
time_t timeServiceLastSyncEpoch();

// Zeitstempel fuer Debuglogs. Solange die Uhr unbekannt ist, wird die Uptime
// ausgegeben, danach lokale deutsche Zeit inklusive Datum.
String timeServiceLogStamp();
