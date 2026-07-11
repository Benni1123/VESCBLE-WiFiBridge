#pragma once

#include <Arduino.h>
#include <esp_system.h>

// Boot-/Resetdiagnose
void captureBootDiagnostics();
void bootDiagMarkPlannedRestart(const char *reason);
void bootDiagClearPlannedRestart();

// Strukturierte Resetinformationen fuer WebUI und REST-API
esp_reset_reason_t bootGetResetReason();
const String &bootGetResetName();
const String &bootGetPlannedRestartReason();
bool bootIsWatchdog();

// Debug-/UART-Log
void uartLogAdd(const String &line);
void dlog(const char *fmt, ...);
String jsonEscapeDebug(String value);
String bootStatusJson();
String uartLogJson();
void uartLogClear();
