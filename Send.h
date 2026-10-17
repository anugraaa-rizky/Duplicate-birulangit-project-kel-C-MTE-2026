#ifndef SEND_H
#define SEND_H

// Isi satu kiriman TOR. Semua nilai sudah berupa string ("NULL" bila tidak sah).
struct Payload {
  String createdAt, co2, pm25, temp, humid;
  bool pm25Ok, shtOk;
};

// Mengubah float menjadi string dengan jumlah desimal TOR, atau "NULL"; input nilai, sah, desimal.
String valueOrNull(float value, bool valid, int decimals) {
  return (valid && !isnan(value)) ? String(value, decimals) : String("NULL");
}

// Escape minimal agar nilai aman sebagai JSON string; input teks, output teks ter-escape.
String jsonEscape(const String &value) {
  String out; out.reserve(value.length() + 4);
  for (unsigned int i = 0; i < value.length(); ++i) {
    char c = value.charAt(i);
    if (c == '"' || c == '\\') out += '\\';
    if (c == '\n') out += "\\n"; else if (c != '\r') out += c;
  }
  return out;
}

// Menambahkan satu pasangan key/value string ke body JSON; input body, key, value.
void addJsonField(String &body, const String &key, const String &value) {
  if (body.length() > 1) body += ',';
  body += "\"" + key + "\":\"" + jsonEscape(value) + "\"";
}

// Menyusun payload TOR; input jenis data dan Payload; output body JSON satu baris.
String buildPayload(const String &kind, const Payload &p) {
  String body = "{"; body.reserve(360);
  addJsonField(body, "device_1", kind + "-" + group_name);
  addJsonField(body, "RSSI_1", WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) : "0");
  addJsonField(body, "created_at_1", p.createdAt);
  addJsonField(body, "Uptime_1", getUptime());
  addJsonField(body, "SD_status_1", spiffsMounted ? "1" : "0");
  addJsonField(body, "PM25_status_1", p.pm25Ok ? "1" : "0");
  addJsonField(body, "SHT_status_1", p.shtOk ? "1" : "0");
  addJsonField(body, "CO2v_1", p.co2); addJsonField(body, "PM25v_1", p.pm25);
  addJsonField(body, "Tempinv_1", p.temp); addJsonField(body, "Humidinv_1", p.humid);
  body += '}';
  return body;
}

// POST JSON ke URL dengan maksimal maxAttempts percobaan; sukses hanya HTTP 200/201.
// Input label log, URL, body, dan apakah memakai HTTPS (WiFiClientSecure tunggal) atau HTTP biasa.
bool postJson(const String &label, const char *url, const String &body, bool secure, int maxAttempts) {
  for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
    if (WiFi.status() != WL_CONNECTED) return false;
    HTTPClient http;
    int code = -1; unsigned long started = millis();
    bool begun = secure ? http.begin(*secureClient, url) : http.begin(url);
    if (begun) {
      http.setTimeout(5000);
      http.setUserAgent(group_name);
      http.addHeader("Content-Type", "application/json");
      code = http.POST(body);
      http.end();
    }
    Serial.printf("[SEND] %s HTTP=%d attempt=%d RTT=%lums\n",
                  label.c_str(), code, attempt, millis() - started);
    if (code == 200 || code == 201) return true;
    if (attempt < maxAttempts) delay(500);
  }
  return false;
}

// Satu fungsi kirim untuk ketiga jenis data (2m_raw, 2m_validated, 1h_valid).
// Input jenis data dan Payload; output true bila server menerima. Gagal -> masuk antrean SPIFFS.
bool sendData(const String &kind, const Payload &p) {
  String body = buildPayload(kind, p);
  if (WiFi.status() == WL_CONNECTED && postJson(kind, api_post, body, true, 5)) return true;
  if (queuePush(QUEUE_TOR, body)) Serial.println("[QUEUE] " + kind + " ditunda");
  return false;
}

// Mengirim paket kontrak backend lokal (POST /api/readings, FastAPI) bila diaktifkan.
// Input epoch UTC awal slot, nilai mentah, dan flag; urutan flags: CO2, PM2.5, T, RH.
bool sendLocalReading(long tsUtc, float t, float h, float c, float p,
                      QualityFlag ft, QualityFlag fh, QualityFlag fc, QualityFlag fp) {
  if (!LOCAL_API_ENABLED) return true;
  String body = "{\"device_id\":\"" + String(local_device_id) + "\",\"ts\":" + String(tsUtc) +
    ",\"co2\":" + (isnan(c) ? String("null") : String(c, 0)) +
    ",\"pm25\":" + (isnan(p) ? String("null") : String(p, 1)) +
    ",\"temp\":" + (isnan(t) ? String("null") : String(t, 2)) +
    ",\"rh\":" + (isnan(h) ? String("null") : String(h, 2)) +
    ",\"flags\":[" + String((int)fc) + "," + String((int)fp) + "," + String((int)ft) + "," + String((int)fh) + "]}";
  if (WiFi.status() == WL_CONNECTED && postJson("local", api_local, body, false, 2)) return true;
  queuePush(QUEUE_LOCAL, body);
  return false;
}

// Bonus: kirim ulang antrean dari SPIFFS setelah online, paling banyak maxItems per pemanggilan.
// Baris dihapus hanya bila server membalas 200/201, sehingga tidak ada record ganda dari perangkat.
// Input nama antrean, URL, mode HTTPS, batas item; output jumlah record yang berhasil dikirim.
int flushQueue(const char *queue, const char *url, bool secure, int maxItems) {
  if (!spiffsMounted || WiFi.status() != WL_CONNECTED || !SPIFFS.exists(queue)) return 0;
  File in = SPIFFS.open(queue, FILE_READ);
  if (!in) return 0;
  const char *tmpName = "/q_tmp.txt";
  File out = SPIFFS.open(tmpName, FILE_WRITE);
  if (!out) { in.close(); return 0; }
  int sent = 0; bool stop = false;
  while (in.available()) {
    String line = in.readStringUntil('\n'); line.trim();
    if (!line.length()) continue;
    if (!stop && sent < maxItems && postJson("resend", url, line, secure, 2)) { sent++; continue; }
    stop = true;
    out.println(line);
  }
  in.close(); out.close();
  SPIFFS.remove(queue);
  File check = SPIFFS.open(tmpName, FILE_READ);
  bool empty = !check || check.size() == 0;
  if (check) check.close();
  if (empty) SPIFFS.remove(tmpName); else SPIFFS.rename(tmpName, queue);
  if (sent) Serial.printf("[QUEUE] %s terkirim ulang=%d sisa=%d\n", queue, sent, queueCount(queue));
  return sent;
}

#endif
