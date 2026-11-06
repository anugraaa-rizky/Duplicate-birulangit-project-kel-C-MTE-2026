// =====================================================================
//  Date_Time.h - FINAL (RTC + Sync + Format Waktu + DayKey TOR)
// =====================================================================
#include <time.h>

String get_date_time(int choose) {
  char buf[20];
  if (choose == 1)
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", rtc.getYear(), rtc.getMonth() + 1, rtc.getDay());
  else if (choose == 2)
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", rtc.getHour(true), rtc.getMinute(), rtc.getSecond());
  else
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", rtc.getYear(), rtc.getMonth() + 1, rtc.getDay(), rtc.getHour(true), rtc.getMinute(), rtc.getSecond());
  return String(buf);
}

String get_slot_time() {
  char buf[20];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:00",
           rtc.getYear(), rtc.getMonth() + 1, rtc.getDay(),
           rtc.getHour(true), rtc.getMinute());
  return String(buf);
}

// Format dayKey (YYYYMMDD) sesuai ketentuan TOR F4 & F7
String get_day_key() {
  char buf[12];
  snprintf(buf, sizeof(buf), "%04d%02d%02d", rtc.getYear(), rtc.getMonth() + 1, rtc.getDay());
  return String(buf);
}

// Menghitung timestamp awal jam (created_at) dan dayKey (YYYYMMDD) untuk rata-rata jam yang baru selesai.
// Karena rata-rata jam ditutup pada awal jam baru (HH:00) — misalnya jam 23:00-23:58 ditutup pada 00:00 keesokan harinya —
// mundur 1800 detik (30 menit) secara matematis selalu jatuh tepat di tengah jam yang dirata-rata.
// Penggunaan localtime_r menangani pergantian hari, bulan, tahun, dan tahun kabisat secara akurat tanpa bug tanggal.
void get_hourly_avg_time(String &outCreatedAt, String &outDayKey) {
  time_t pastEpoch = rtc.getEpoch() - 1800;
  struct tm ti;
  localtime_r(&pastEpoch, &ti);

  char timeBuf[25];
  snprintf(timeBuf, sizeof(timeBuf), "%04d-%02d-%02d %02d:00:00",
           ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour);
  outCreatedAt = String(timeBuf);

  char dayBuf[12];
  snprintf(dayBuf, sizeof(dayBuf), "%04d%02d%02d",
           ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
  outDayKey = String(dayBuf);
}

String get_uptime() {
  unsigned long totalSeconds = millis() / 1000;
  char buf[12];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
           totalSeconds / 3600, (totalSeconds % 3600) / 60, totalSeconds % 60);
  return String(buf);
}

bool setRtcFromJson(const String &payload) {
  int k = payload.indexOf("\"datetime\":");
  if (k == -1) return false;
  int a = payload.indexOf("\"", k + 11) + 1;
  String d = payload.substring(a, a + 19);
  if (d.length() != 19) return false;
  rtc.setTime(d.substring(17, 19).toInt(), d.substring(14, 16).toInt(), d.substring(11, 13).toInt(),
              d.substring(8, 10).toInt(), d.substring(5, 7).toInt(), d.substring(0, 4).toInt());
  return true;
}

bool Sync_RTC() {
  if (WiFi.status() != WL_CONNECTED) return false;
  if (client.connected()) client.stop();
  HTTPClient https;
  if (!https.begin(client, api_time)) return false;
  https.setUserAgent(group_name);
  https.setTimeout(8000);
  unsigned long t0 = millis(); int code = https.GET(); unsigned long rtt = millis() - t0;
  bool ok = false;
  if (code >= 200 && code <= 299) {
    ok = setRtcFromJson(https.getString());
  }
  https.end();
  Serial.printf("[RTC] %s kode=%d RTT=%lu ms\n", get_date_time(0).c_str(), code, rtt);
  return ok;
}
