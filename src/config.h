#pragma once

#include <Arduino.h>

bool applyStaticConfig(const String &ipS, const String &gwS,
                       const String &subS, const String &dnsS);
void loadConfig();
void saveConfig();
