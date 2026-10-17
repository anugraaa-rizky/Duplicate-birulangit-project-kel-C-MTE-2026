#ifndef DATE_TIME_H
#define DATE_TIME_H

#include <time.h>

// Selisih WIB terhadap UTC. RTC menyimpan jam dinding WIB, jadi epoch UTC = epoch RTC - WIB_OFFSET.
const long WIB_OFFSET = 7L * 3600L;

// Indeks waktu satu slot (F4): dayKey YYYYMMDD, jam, menit, dan menit ke-n dalam sehari.
struct TimeIndex { String dayKey; int hour; int minute; int minOfDay; };

// Membentuk TimeIndex dari epoch lokal WIB; input epoch, output struct TimeIndex.
TimeIndex buildTimeIndex(time_t value) {
  struct tm t;
  gmtime_r(&value, &t);
  char key[9];
  snprintf(key, sizeof(key), "%04d%02d%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  TimeIndex idx;
  idx.dayKey = String(key);
  idx.hour = t.tm_hour;
  idx.minute = t.tm_min;
  idx.minOfDay = t.tm_hour * 60 + t.tm_min;
  return idx;
}

// Menghasilkan tanggal RTC saat ini dalam format YYYYMMDD; tanpa input, output String.
String getDayKey() {
  return buildTimeIndex(rtc.getEpoch()).dayKey;
}

// Memformat epoch lokal WIB sebagai YYYY-MM-DD HH:MM:SS; input epoch, output String.
String formatEpoch(time_t value) {
  struct tm valueTm;
  gmtime_r(&value, &valueTm);
  char out[20];
  strftime(out, sizeof(out), "%Y-%m-%d %H:%M:%S", &valueTm);
  return String(out);
}

// Menghasilkan timestamp RTC saat ini; tanpa input, output String.
String getDateTime() {
  return formatEpoch(rtc.getEpoch());
}

// Menghasilkan uptime HH:MM:SS (jam minimal dua digit, boleh > 24); output String.
String getUptime() {
  unsigned long seconds = millis() / 1000UL;
  char out[20];
  snprintf(out, sizeof(out), "%02lu:%02lu:%02lu", seconds / 3600UL,
           (seconds % 3600UL) / 60UL, seconds % 60UL);
  return String(out);
}

// Memvalidasi field datetime dari JSON server lalu menerapkannya ke RTC.
// Input payload JSON; output true bila valid, serverDateTime berisi teks waktu server.
bool setRtcFromJson(const String &payload, String &serverDateTime) {
  int key = payload.indexOf("\"datetime\"");
  if (key < 0) return false;
  int colon = payload.indexOf(':', key);
  int quote = payload.indexOf('"', colon + 1);
  if (colon < 0 || quote < 0 || quote + 20 > (int)payload.length()) return false;
  serverDateTime = payload.substring(quote + 1, quote + 20);
  if (serverDateTime.length() != 19 || serverDateTime.charAt(4) != '-' ||
      serverDateTime.charAt(7) != '-' || serverDateTime.charAt(10) != ' ' ||
      serverDateTime.charAt(13) != ':' || serverDateTime.charAt(16) != ':') return false;
  int year = serverDateTime.substring(0, 4).toInt();
  int month = serverDateTime.substring(5, 7).toInt();
  int day = serverDateTime.substring(8, 10).toInt();
  int hour = serverDateTime.substring(11, 13).toInt();
  int minute = serverDateTime.substring(14, 16).toInt();
  int second = serverDateTime.substring(17, 19).toInt();
  if (year < 2025 || month < 1 || month > 12 || day < 1 || day > 31 ||
      hour > 23 || minute > 59 || second > 59) return false;
  rtc.setTime(second, minute, hour, day, month, year);
  return true;
}

// Mengambil waktu WIB dari /api/get-time (F3); tanpa input, output true bila HTTP 200 dan parsing sah.
bool syncRtc() {
  if (WiFi.status() != WL_CONNECTED || secureClient == nullptr) return false;
  HTTPClient https;
  unsigned long started = millis();
  int httpCode = -1;
  String dateTime = "";
  bool ok = false;
  if (https.begin(*secureClient, api_time)) {
    https.setTimeout(5000);
    https.setUserAgent(group_name);
    httpCode = https.GET();
    if (httpCode == 200) ok = setRtcFromJson(https.getString(), dateTime);
    https.end();
  }
  Serial.printf("[RTC] HTTP=%d\n", httpCode);
  if (dateTime.length()) Serial.println("[RTC] server=" + dateTime);
  if (ok) Serial.println("[RTC] rtc=" + getDateTime());
  Serial.printf("[RTC] RTT=%lu ms status=%s\n", millis() - started, ok ? "OK" : "GAGAL");
  return ok;
}

#endif
