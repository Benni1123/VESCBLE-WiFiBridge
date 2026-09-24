#pragma once

#include <Arduino.h>

// ── Stall-Waechter und Blackbox im Flash (NVS) ───────────────────────────────
//
// Warum es das gibt:
//
// Im Fehlerbild, das diesem Modul zugrunde liegt, bleibt BLE erreichbar,
// waehrend AP, STA, Weboberflaeche und Logversand komplett ausfallen. BLE
// laeuft im eigenen NimBLE-Task, alles andere haengt am Hauptloop. Lebt also
// nur noch BLE, steht der Hauptloop. Die Serverlogs stuetzen das: Heap,
// groesster freier Block und Loopdauer sind bis zur letzten Zeile voellig
// unauffaellig, dann bricht der Strom an Zeilen von einer Sekunde auf die
// naechste ab. Kein langsames Verhungern, sondern ein Stillstand.
//
// Ein stehender Hauptloop loest von allein KEINEN Neustart aus: der
// Task-Watchdog ueberwacht den Arduino-loopTask standardmaessig nicht. Das
// Geraet bleibt beliebig lange in diesem Zustand haengen.
//
// Zwei Aufgaben also:
//
//   1. Erkennen und beenden. Ein eigener Task auf Kern 0 prueft, ob der
//      Hauptloop noch Lebenszeichen setzt. Bleiben sie aus, wird neu
//      gestartet — das Geraet kommt von selbst zurueck, statt bis zum
//      naechsten Stromausfall tot dazuliegen.
//
//   2. Festhalten, WO er stehengeblieben ist. Der vorhandene RTC-Ringpuffer
//      taugt dafuer nicht: RTC_NOINIT ueberlebt Reset, Panic und Watchdog,
//      aber KEINEN Power-Cycle. Um an das Geraet zu kommen, muss der Scooter
//      stromlos gemacht werden — womit der RTC-Inhalt jedes Mal genau dann
//      verloren ist, wenn man ihn braucht. Die Blackbox liegt deshalb in
//      einem eigenen NVS-Namespace im Flash und ueberlebt beliebig langes
//      Stromlosliegen.
//
// Der Namespace "vescbb" ist bewusst NICHT Teil der Backupliste in
// backup.cpp: eine Absturzspur gehoert nicht in eine Konfigurationssicherung.

// Liest eine eventuell vorhandene Blackbox des vorherigen Laufs, schiebt sie
// in den Logversand und loescht sie danach. Aufruf in setup() nach
// logShipSetup() und vor captureBootDiagnostics().
void blackboxSetup();

// Startet den Waechter-Task. Aufruf am ENDE von setup() — vorher laeuft der
// Hauptloop noch nicht, und ein langes setup() (WLAN-Verbindungsaufbau)
// wuerde faelschlich als Stillstand gelten.
void blackboxStartTask();

// Lebenszeichen aus dem Hauptloop. Wird zu Beginn jedes Durchlaufs gesetzt.
extern volatile uint32_t blackboxHeartbeat;

// Grobe Position im Loop, damit die Blackbox nennen kann, welcher Abschnitt
// nicht zurueckgekehrt ist.
enum BlackboxPhase : uint8_t {
  BB_PHASE_IDLE   = 0,
  BB_PHASE_WEBUI  = 1,
  BB_PHASE_WIFI   = 2,
  BB_PHASE_TIME   = 3,
  BB_PHASE_VESC   = 4,
};
extern volatile uint8_t blackboxPhase;

// Feinere Ortsangabe INNERHALB einer Phase.
//
// Die Phase allein sagt "steht in wifiBleLoop" — das sind rund 370 Zeilen mit
// AP-Watchdog, Reconnect, Roaming, BLE-Modus und Heartbeat darin, und jeder
// dieser Abschnitte ruft WLAN-Funktionen auf, die blockieren koennen. Ohne
// eine feinere Angabe faengt die Suche bei neun Verdaechtigen an.
//
// Zeiger auf ein String-Literal: die Zuweisung ist auf dem S3 ein einzelner
// 32-Bit-Schreibzugriff, also unteilbar. Kein Kopieren, keine Allokation,
// nichts, was im Loop messbar waere.
extern volatile const char *blackboxStep;
#define BB_STEP(s) do { blackboxStep = (s); } while (0)

// Schreibt sofort eine Blackbox mit dem angegebenen Grund. Kann aus jedem
// Task gerufen werden; schlaegt der Schreibvorgang fehl, passiert nichts
// weiter (die Blackbox darf niemals selbst zum Problem werden).
void blackboxWrite(const char *reason);

// Zustand fuer den Debug-Tab.
String blackboxStatusJson();
