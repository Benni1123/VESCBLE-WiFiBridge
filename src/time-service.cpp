// Uhrzeit per NTP oder REST-API, ohne externen RTC-Baustein.
// Diese Datei wird ueber main.cpp als Unity-Build eingebunden.
#if defined(VESC_BRIDGE_UNITY_BUILD)
#include "globals.h"
#include "debuglog.h"
#include "time-service.h"

#include <time.h>
#include <sys/time.h>

#if __has_include(<esp_sntp.h>)
  #include <esp_sntp.h>
  #define VESC_TIME_HAS_SNTP_CALLBACK 1
#else
  #define VESC_TIME_HAS_SNTP_CALLBACK 0
#endif

// Deutschland inklusive automatischer Sommer-/Winterzeit.
// POSIX-TZ: CET = UTC+1, CEST = UTC+2, Umschaltung am letzten Sonntag
// im Maerz und Oktober.
static const char *TIME_TZ_POSIX = "CET-1CEST,M3.5.0,M10.5.0/3";
static const char *TIME_TZ_NAME  = "Europe/Berlin";

static const char *TIME_NTP_1 = "pool.ntp.org";
static const char *TIME_NTP_2 = "time.cloudflare.com";
static const char *TIME_NTP_3 = "time.google.com";

// Plausibilitaetsgrenzen: 2020-01-01 bis 2100-01-01.
// WICHTIG: Nicht als time_t speichern. Bei Core-Versionen mit 32-Bit-time_t
// wuerde 4102444800 (Jahr 2100) negativ ueberlaufen. Dann waere jede aktuelle
// Uhrzeit trotz erfolgreichem settimeofday()/NTP angeblich ungueltig.
static const int64_t TIME_VALID_MIN_SEC = 1577836800LL;
static const int64_t TIME_VALID_MAX_SEC = 4102444800LL;

static uint64_t timeServiceRepresentableMaxEpoch() {
  // Ein vorzeichenbehaftetes 32-Bit-time_t reicht nur bis 2038-01-19.
  // Neuere ESP-IDF/Core-Versionen verwenden 64 Bit und koennen bis 2099.
  if (sizeof(time_t) <= 4) return 2147483647ULL;
  return (uint64_t)TIME_VALID_MAX_SEC - 1ULL;
}

static String timeSourceName = "unknown";
// Moegliche Quellen: unknown | api | app | browser | home_assistant | ntp

static String timeNormalizeSource(const String &rawSource) {
  String source = rawSource;
  source.trim();
  source.toLowerCase();

  if (source == "browser") return "browser";
  if (source == "app") return "app";
  if (source == "home_assistant" || source == "home-assistant" || source == "ha")
    return "home_assistant";
  if (source == "ntp") return "ntp";
  return "api";
}

static const char *timeSourceLogLabel(const String &source) {
  if (source == "browser") return "Browser";
  if (source == "app") return "App";
  if (source == "home_assistant") return "Home Assistant";
  return "API";
}
static time_t timeLastSync = 0;
static bool timeWasWifiConnected = false;
static bool timeNtpConfigured = false;
static bool timeNtpAwaitingFirstValid = false;
static bool timePreviouslyValid = false;
static unsigned long timeLastLoopCheck = 0;

#if VESC_TIME_HAS_SNTP_CALLBACK
static volatile bool timeNtpCallbackPending = false;
static volatile uint32_t timeNtpCallbackEpoch = 0;

static void timeServiceNtpCallback(struct timeval *tv) {
  if (tv && tv->tv_sec > 0) {
    timeNtpCallbackEpoch = (uint32_t)tv->tv_sec;
  } else {
    timeNtpCallbackEpoch = 0;
  }
  timeNtpCallbackPending = true;
}
#endif

static bool timeEpochPlausible(time_t value) {
  // Erst auf 64 Bit erweitern, bevor die Grenzen verglichen werden. So bleibt
  // die Pruefung sowohl mit 32-Bit- als auch mit 64-Bit-time_t korrekt.
  int64_t epoch = (int64_t)value;
  return epoch >= TIME_VALID_MIN_SEC && epoch < TIME_VALID_MAX_SEC;
}

bool timeServiceIsValid() {
  return timeEpochPlausible(time(nullptr));
}

time_t timeServiceEpoch() {
  time_t now = time(nullptr);
  return timeEpochPlausible(now) ? now : 0;
}

static String timeFormat(time_t epoch, bool local) {
  if (!timeEpochPlausible(epoch)) return "";

  struct tm tmValue;
  memset(&tmValue, 0, sizeof(tmValue));
  if (local) localtime_r(&epoch, &tmValue);
  else       gmtime_r(&epoch, &tmValue);

  char buffer[32];
  if (local) {
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tmValue);
  } else {
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tmValue);
  }
  return String(buffer);
}

String timeServiceLocalString() {
  return timeFormat(timeServiceEpoch(), true);
}

String timeServiceUtcString() {
  return timeFormat(timeServiceEpoch(), false);
}

String timeServiceSource() {
  return timeSourceName;
}

time_t timeServiceLastSyncEpoch() {
  return timeLastSync;
}

String timeServiceLogStamp() {
  // Die Uptime bleibt auch nach erfolgreicher Zeit-Synchronisierung sichtbar.
  // Dadurch kann man Ereignisse sowohl einer echten Uhrzeit als auch der
  // Laufzeit seit dem letzten Boot eindeutig zuordnen.
  unsigned long uptimeSec = millis() / 1000UL;
  time_t now = timeServiceEpoch();

  if (now > 0) {
    return timeFormat(now, true) + " [uptime " + String(uptimeSec) + "s]";
  }

  return String(uptimeSec) + "s";
}

static void timeStartNtp() {
  // configTzTime arbeitet non-blocking. Die SNTP-Komponente synchronisiert nach
  // erfolgreicher Verbindung automatisch weiter; der loop wartet nie darauf.
  configTzTime(TIME_TZ_POSIX, TIME_NTP_1, TIME_NTP_2, TIME_NTP_3);
  timeNtpConfigured = true;
  timeNtpAwaitingFirstValid = true;
  dlog("Time: NTP started (%s, %s, %s)\n", TIME_NTP_1, TIME_NTP_2, TIME_NTP_3);
}

void timeServiceSetup() {
  setenv("TZ", TIME_TZ_POSIX, 1);
  tzset();

#if VESC_TIME_HAS_SNTP_CALLBACK
  sntp_set_time_sync_notification_cb(timeServiceNtpCallback);
#endif

  timePreviouslyValid = timeServiceIsValid();
  timeWasWifiConnected = (WiFi.status() == WL_CONNECTED);

  if (timeWasWifiConnected) {
    timeStartNtp();
  }
}

bool timeServiceSetEpoch(uint64_t epochValue, String &error, const String &source) {
  // Smartphone-APIs liefern oft Millisekunden. Ab 10^11 wird deshalb sicher
  // als Millisekunden interpretiert und auf Sekunden reduziert.
  if (epochValue >= 100000000000ULL) epochValue /= 1000ULL;

  uint64_t maxEpoch = timeServiceRepresentableMaxEpoch();
  if (epochValue < (uint64_t)TIME_VALID_MIN_SEC || epochValue > maxEpoch) {
    if (sizeof(time_t) <= 4) {
      error = "epoch out of range (this core supports 2020-01-01 to 2038-01-19)";
    } else {
      error = "epoch out of range (expected 2020-01-01 to 2099-12-31)";
    }
    return false;
  }

  struct timeval tv;
  tv.tv_sec = (time_t)epochValue;
  tv.tv_usec = 0;
  if (settimeofday(&tv, nullptr) != 0) {
    error = "settimeofday failed";
    return false;
  }

  timeSourceName = timeNormalizeSource(source);
  // Ein manueller POST darf sich nicht als NTP ausgeben. NTP wird ausschliesslich
  // vom echten SNTP-Callback bzw. dessen Fallback gesetzt.
  if (timeSourceName == "ntp") timeSourceName = "api";
  timeLastSync = (time_t)epochValue;
  timePreviouslyValid = true;
  timeNtpAwaitingFirstValid = false;
  error = "";

  dlog("Time: set via %s: %s\n",
       timeSourceLogLabel(timeSourceName),
       timeServiceLocalString().c_str());
  return true;
}

void timeServiceLoop() {
  unsigned long nowMs = millis();
  if (nowMs - timeLastLoopCheck < 250UL) return;
  timeLastLoopCheck = nowMs;

  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiConnected && !timeWasWifiConnected) {
    // Nach jeder neuen Heimnetzverbindung NTP erneut anstossen. Das ist
    // non-blocking und sorgt auch nach langer Fahrt ohne Internet fuer Sync.
    timeStartNtp();
  }
  timeWasWifiConnected = wifiConnected;

#if VESC_TIME_HAS_SNTP_CALLBACK
  if (timeNtpCallbackPending) {
    uint32_t syncedEpoch = timeNtpCallbackEpoch;
    timeNtpCallbackPending = false;

    time_t now = time(nullptr);
    if (syncedEpoch > 0 && timeEpochPlausible((time_t)syncedEpoch)) {
      timeLastSync = (time_t)syncedEpoch;
    } else if (timeEpochPlausible(now)) {
      timeLastSync = now;
    }

    if (timeLastSync > 0) {
      timeSourceName = "ntp";
      timeNtpAwaitingFirstValid = false;
      timePreviouslyValid = true;
      dlog("Time: NTP synchronized: %s\n", timeServiceLocalString().c_str());
    }
  }
#else
  // Fallback fuer Core-Versionen ohne SNTP-Callback: Wenn die Uhr vor dem
  // NTP-Start ungueltig war und anschliessend plausibel wird, stammt sie von NTP.
  bool validNow = timeServiceIsValid();
  if (timeNtpAwaitingFirstValid && !timePreviouslyValid && validNow) {
    timeLastSync = time(nullptr);
    timeSourceName = "ntp";
    timeNtpAwaitingFirstValid = false;
    dlog("Time: NTP synchronized: %s\n", timeServiceLocalString().c_str());
  }
  timePreviouslyValid = validNow;
#endif

  // Falls die Zeit bereits per API gesetzt war, bleibt "api" als Quelle stehen,
  // bis ein echter SNTP-Sync gemeldet wird. Ohne Callback wird nichts erfunden.
  (void)timeNtpConfigured;
}

String timeServiceJson() {
  bool valid = timeServiceIsValid();
  time_t now = valid ? time(nullptr) : 0;

  String json = "{";
  json += "\"valid\":" + String(valid ? "true" : "false") + ",";
  json += "\"epoch\":" + String((unsigned long)now) + ",";
  json += "\"local\":\"" + jsonEscapeDebug(valid ? timeFormat(now, true) : String("")) + "\",";
  json += "\"utc\":\"" + jsonEscapeDebug(valid ? timeFormat(now, false) : String("")) + "\",";
  json += "\"source\":\"" + jsonEscapeDebug(timeSourceName) + "\",";
  json += "\"last_sync\":" + String((unsigned long)timeLastSync) + ",";
  json += "\"timezone\":\"" + String(TIME_TZ_NAME) + "\",";
  json += "\"timezone_posix\":\"" + String(TIME_TZ_POSIX) + "\",";
  json += "\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"ntp_configured\":" + String(timeNtpConfigured ? "true" : "false") + ",";
  json += "\"ntp_servers\":[\"" + String(TIME_NTP_1) + "\",\"" + String(TIME_NTP_2) + "\",\"" + String(TIME_NTP_3) + "\"]";
  json += "}";
  return json;
}

#endif // VESC_BRIDGE_UNITY_BUILD