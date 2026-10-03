#pragma once

#include <Arduino.h>
#include <WebServer.h>

// ── Griffheizung, ein Kanal ──────────────────────────────────────────────────
//
// Ein GPIO treibt per PWM einen MOSFET, der die Heizwendel schaltet. Leistung,
// PWM-Frequenz, GPIO und die ERPM-Schwelle sind alle auf der eigenen Seite
// /heat einstellbar — gerade die Frequenz will man beim ersten Aufbau
// ausprobieren, weil sie von der Treiberstufe abhaengt.
//
// Der Sinn der ERPM-Kopplung: im Stand soll nicht geheizt werden. Nicht wegen
// der Griffe, sondern wegen des Akkus — eine Griffheizung zieht dauerhaft
// Strom, und ein Scooter, der eine Stunde vor dem Laden steht, soll ihn nicht
// leer heizen. Gefahren wird mit Nachlauf: an der Ampel bleibt es warm.
//
// ── WICHTIG ZUR SCHALTUNG ───────────────────────────────────────────────────
//
// Am Gate des MOSFET MUSS ein Pulldown-Widerstand gegen Masse sitzen
// (10 kOhm). Grund: waehrend eines Neustarts, eines Absturzes und in den ersten
// Millisekunden nach dem Einschalten ist der GPIO hochohmiger Eingang. Ohne
// Pulldown ist das Gate dann offen, und ob der MOSFET leitet, entscheidet
// Streukapazitaet — also nichts, worauf man sich verlassen kann. Mit Pulldown
// ist die Heizung in genau diesen Momenten garantiert aus.
//
// Aus demselben Grund wird hier KEIN "aus vor dem Neustart" durch den Code
// erzwungen: das waere eine Zusage, die bei einem Absturz oder Brownout nicht
// einzuhalten ist. Der Widerstand kann das, der Code nicht.

// Laedt die Einstellungen aus dem NVS, richtet die PWM ein und registriert
// /heat sowie /api/heat. In setup() aufrufen, nach loadConfig().
void heatSetup(WebServer *server);

// Aus dem loop() aufrufen. Bekommt den aktuellen VESC-Zustand uebergeben und
// entscheidet daraus, ob und mit welcher Leistung geheizt wird. Blockiert
// nicht und rechnet nur, wenn sich etwas geaendert hat.
void heatUpdateState(bool vescConnected, int32_t erpm, float voltage);

// Meldet, ob die Heizung ERPM-Werte BRAUCHT (Auto-Modus). pollVesc() fragt das
// ab und pollt dann selbst, genau wie fuer BLE-Auto und AP-Auto.
//
// Ohne diese Kopplung gab es eine stille Falle: steht BLE auf "An" statt
// "Auto", der AP auf "An", und ist "VESC pollen wenn WebUI zu" aus, kamen beim
// Fahren gar keine ERPM-Werte. Die Heizung meldete dauerhaft "keine frischen
// VESC-Daten" und heizte nie — ausser man hatte die Weboberflaeche offen.
bool heatNeedsErpm();

// Meldet, ob eine eigene Statusabfrage in den Brueckenverkehr eingestreut
// werden darf, waehrend VESC Tool oder die App verbunden ist.
//
// Getrennt von heatNeedsErpm(), weil es zwei verschiedene Dinge sind: ERPM
// braucht die Heizung immer im Auto-Modus, das Einstreuen ist nur EIN Weg,
// daran zu kommen — und der einzige, der fremden Datenverkehr beruehrt.
// Deshalb abschaltbar (API-Tab), ohne alles andere zu verlieren.
bool heatInjectWanted();

// Meldet, ob gerade tatsaechlich Leistung an der Heizung liegt. Gedacht fuer
// Aufrufer, die etwas aufschieben wollen, solange geheizt wird.
bool heatIsOn();

// Setzt die Ausgangsleistung sofort auf 0. Der Zustand (Modus, Leistung)
// bleibt erhalten — beim naechsten heatUpdateState() geht es normal weiter.
void heatOff();

// Zustand fuer /api/info und die Startseite.
String heatStatusJson();
