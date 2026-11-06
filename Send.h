// =====================================================================
//  Send.h - FINAL (Non-Blocking Queue Batching + Validasi + Format TOR)
// =====================================================================
#include <HTTPClient.h>

String jsonString;
int totalSentSuccess = 0, totalSentFailed = 0;

void initSend() {
  jsonString.reserve(512);
  client.setTimeout(6);
}

void addJson(String key, String value) {
  if (jsonString.length() > 0) jsonString += ",";
  jsonString += "\"" + key + "\":\"" + value + "\"";
}

String getJson() { return "{" + jsonString + "}"; }

// ===== Queue System (Bonus TOR +10) =====
void enqueuePayload(const String& body) {
  if (!spiffsMounted) return;
  File f = SPIFFS.open("/queue.txt", FILE_APPEND);
  if (f) {
    f.println(body);
    f.close();
    Serial.println("[QUEUE] Data berhasil disimpan ke antrian SPIFFS (/queue.txt).");
  } else {
    Serial.println("[QUEUE] Error: Gagal membuka antrian /queue.txt!");
  }
}

// Memproses antrian secara bertahap (maksimal maxBatch data per panggilan)
// agar tidak memblokir CPU dan tidak melewatkan slot 2-menit berikutnya.
void processQueue(int maxBatch = 3) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!SPIFFS.exists("/queue.txt")) return;

  // Jangan proses jika slot menit genap berikutnya akan segera tiba (< 15 detik lagi)
  int curMin = rtc.getMinute();
  int curSec = rtc.getSecond();
  if (curMin % 2 == 1 && curSec >= 45) {
    return;
  }

  // 1. Hitung total data dalam antrian
  File fCount = SPIFFS.open("/queue.txt", FILE_READ);
  if (!fCount) return;
  int totalQueue = 0;
  while (fCount.available()) {
    String l = fCount.readStringUntil('\n');
    l.trim();
    if (l.length() > 10) totalQueue++;
  }
  fCount.close();

  if (totalQueue == 0) {
    SPIFFS.remove("/queue.txt");
    return;
  }

  Serial.println("\n--------------------------------------------------");
  Serial.printf("[QUEUE] Ada %d data tertunda. Memproses batch (maks %d)...\n", totalQueue, maxBatch);

  File fRead = SPIFFS.open("/queue.txt", FILE_READ);
  if (!fRead) return;

  File fTemp = SPIFFS.open("/queue.tmp", FILE_WRITE);
  if (!fTemp) {
    Serial.println("[QUEUE] Gagal membuat file temporary /queue.tmp!");
    fRead.close();
    return;
  }

  int processedInThisBatch = 0;
  int successCount = 0;
  int failCount = 0;
  unsigned long batchStart = millis();

  while (fRead.available()) {
    String line = fRead.readStringUntil('\n');
    line.trim();
    if (line.length() < 10) continue;

    // Cek apakah slot genap berikutnya sudah mendekati (< 15 detik lagi)
    curMin = rtc.getMinute();
    curSec = rtc.getSecond();
    bool nearNextSlot = (curMin % 2 == 1 && curSec >= 45);
    bool batchLimitReached = (processedInThisBatch >= maxBatch) || (millis() - batchStart > 10000);

    // Jika batas batch tercapai atau waktu hampir habis, simpan baris sisa ke file temporary
    if (batchLimitReached || nearNextSlot || WiFi.status() != WL_CONNECTED) {
      fTemp.println(line);
      failCount++;
      continue;
    }

    processedInThisBatch++;

    // Ekstrak info device_1 dan created_at_1 untuk log validasi ke Serial
    String dev = "";
    int dIdx = line.indexOf("\"device_1\":\"");
    if (dIdx != -1) {
      int dEnd = line.indexOf("\"", dIdx + 12);
      if (dEnd != -1) dev = line.substring(dIdx + 12, dEnd);
    }
    String tm = "";
    int tIdx = line.indexOf("\"created_at_1\":\"");
    if (tIdx != -1) {
      int tEnd = line.indexOf("\"", tIdx + 16);
      if (tEnd != -1) tm = line.substring(tIdx + 16, tEnd);
    }

    Serial.printf("[QUEUE] (%d/%d) Kirim [%s] @ %s ... ", processedInThisBatch, totalQueue, dev.c_str(), tm.c_str());

    bool ok = false;
    int lastCode = 0;

    for (int attempt = 1; attempt <= 2; attempt++) {
      if (client.connected()) client.stop();
      HTTPClient https;
      if (https.begin(client, api_post)) {
        https.setUserAgent(group_name);
        https.addHeader("Content-Type", "application/json");
        https.setTimeout(5000);
        lastCode = https.POST(line);
        https.end();
        if (lastCode == 200 || lastCode == 201) {
          ok = true;
          break;
        }
      }
      delay(250);
    }

    if (ok) {
      successCount++;
      totalSentSuccess++;
      Serial.printf("BERHASIL (HTTP %d)\n", lastCode);
    } else {
      failCount++;
      totalSentFailed++;
      Serial.printf("GAGAL (HTTP %d) -> Disimpan kembali\n", lastCode);
      fTemp.println(line); // Simpan kembali data yang gagal
    }
    delay(100);
  }

  fRead.close();
  fTemp.close();

  // Bersihkan file queue lama dan ganti dengan antrian sisa
  SPIFFS.remove("/queue.txt");
  if (failCount > 0) {
    SPIFFS.rename("/queue.tmp", "/queue.txt");
  } else {
    SPIFFS.remove("/queue.tmp");
  }

  Serial.printf("[QUEUE] Batch selesai: %d terkirim, %d sisa dalam antrian.\n", successCount, failCount);
  if (failCount == 0) {
    Serial.println("[QUEUE] VALIDASI: Semua antrian telah habis terkirim ke server!");
  }
  Serial.println("--------------------------------------------------\n");
}

// ===== Pengiriman JSON =====
bool postJson(const String &tag, const String &url) {
  bool success = false;
  String body = getJson();
  Serial.println("[" + tag + "] " + body);

  // Jika WiFi mati, langsung masukkan ke antrian SPIFFS tanpa menunggu timeout
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("   -> [OFFLINE] WiFi tidak terhubung. Disimpan otomatis ke antrian SPIFFS.");
    totalSentFailed++;
    enqueuePayload(body);
    jsonString = "";
    return false;
  }

  for (int attempt = 1; attempt <= 4 && !success; attempt++) {
    if (client.connected()) client.stop();
    HTTPClient https;
    if (https.begin(client, url)) {
      https.setUserAgent(group_name);
      https.addHeader("Content-Type", "application/json");
      https.setTimeout(6000);
      int code = https.POST(body);
      Serial.printf("   -> percobaan %d: kode %d\n", attempt, code);
      success = (code == 200 || code == 201);
      https.end();
    } else {
      Serial.printf("   -> percobaan %d: Gagal connect\n", attempt);
    }
    if (!success && attempt < 4) delay(500);
  }

  if (success) {
    totalSentSuccess++;
  } else {
    totalSentFailed++;
    Serial.println("   -> [GAGAL] Pengiriman gagal. Disimpan ke antrian.");
    enqueuePayload(body);
  }

  jsonString = "";
  return success;
}

void printSendStats() {
  Serial.println("\n=== STATISTIK PENGIRIMAN ===");
  Serial.printf("WiFi Status : %s (IP: %s, RSSI: %d dBm)\n",
                (WiFi.status() == WL_CONNECTED) ? "TERHUBUNG" : "TERPUTUS",
                (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString().c_str() : "-",
                (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0);
  Serial.printf("Total Sukses: %d | Total Gagal/Tertunda: %d\n", totalSentSuccess, totalSentFailed);
  if (SPIFFS.exists("/queue.txt")) {
    File f = SPIFFS.open("/queue.txt", FILE_READ);
    int lines = 0;
    Serial.println("Daftar Data Antrian di SPIFFS:");
    while(f.available()) {
      String l = f.readStringUntil('\n');
      l.trim();
      if(l.length() > 10) {
        lines++;
        int dIdx = l.indexOf("\"device_1\":\"");
        String dev = "";
        if (dIdx != -1) {
          int dEnd = l.indexOf("\"", dIdx + 12);
          if (dEnd != -1) dev = l.substring(dIdx + 12, dEnd);
        }
        int tIdx = l.indexOf("\"created_at_1\":\"");
        String t = "";
        if (tIdx != -1) {
          int tEnd = l.indexOf("\"", tIdx + 16);
          if (tEnd != -1) t = l.substring(tIdx + 16, tEnd);
        }
        if (lines <= 10) {
          Serial.printf("  %d. [%s] @ %s\n", lines, dev.c_str(), t.c_str());
        }
      }
    }
    f.close();
    if (lines > 10) Serial.printf("  ... dan %d data lainnya.\n", lines - 10);
    Serial.printf("Total Antrian: %d data tertunda.\n", lines);
  } else {
    Serial.println("Antrian     : 0 data tertunda (Semua data tersinkron ke server).");
  }
  Serial.println("============================\n");
}

// ===== Payload 2m_raw =====
void send2mRaw() {
  addJson("device_1", "2m_raw-" + group_name);
  addJson("RSSI_1", (WiFi.status() == WL_CONNECTED) ? String(WiFi.RSSI()) : "0");
  addJson("created_at_1", get_slot_time());
  addJson("Uptime_1", get_uptime());
  addJson("SD_status_1", spiffsMounted ? "1" : "0");
  addJson("PM25_status_1", isnan(PM25v) ? "0" : "1");
  addJson("SHT_status_1", SHT_status ? "1" : "0");
  addJson("CO2v_1", isnan(CO2v) ? "NULL" : String(CO2v, 0));
  addJson("PM25v_1", isnan(PM25v) ? "NULL" : String(PM25v, 1));
  addJson("Tempinv_1", isnan(Tempinv) ? "NULL" : String(Tempinv, 2));
  addJson("Humidinv_1", isnan(Humidinv) ? "NULL" : String(Humidinv, 2));
  postJson("RnD BiruLangit (Raw)", api_post);
}

// ===== Payload 2m_validated =====
void send2mValidated(int fT, int fH, int fC, int fP, float t, float h, float c, float p) {
  addJson("device_1", "2m_validated-" + group_name);
  addJson("RSSI_1", (WiFi.status() == WL_CONNECTED) ? String(WiFi.RSSI()) : "0");
  addJson("created_at_1", get_slot_time());
  addJson("Uptime_1", get_uptime());
  addJson("SD_status_1", spiffsMounted ? "1" : "0");
  addJson("PM25_status_1", (fP == 0 && !isnan(p)) ? "1" : "0");
  addJson("SHT_status_1", (fT == 0 && fH == 0 && !isnan(t)) ? "1" : "0");
  addJson("CO2v_1", (fC == 0 && !isnan(c)) ? String(c, 0) : "NULL");
  addJson("PM25v_1", (fP == 0 && !isnan(p)) ? String(p, 1) : "NULL");
  addJson("Tempinv_1", (fT == 0 && !isnan(t)) ? String(t, 2) : "NULL");
  addJson("Humidinv_1", (fH == 0 && !isnan(h)) ? String(h, 2) : "NULL");
  postJson("RnD BiruLangit (Valid)", api_post);
}

// ===== Payload 1h_valid =====
void send1hValid(int countValid, bool isValid, const String &createdAt) {
  addJson("device_1", "1h_valid-" + group_name);
  addJson("RSSI_1", (WiFi.status() == WL_CONNECTED) ? String(WiFi.RSSI()) : "0");
  addJson("created_at_1", createdAt);
  addJson("Uptime_1", get_uptime());
  addJson("SD_status_1", spiffsMounted ? "1" : "0");
  addJson("PM25_status_1", isValid ? "1" : "0");
  addJson("SHT_status_1", isValid ? "1" : "0");
  if (isValid) {
    addJson("CO2v_1", String(sumCO2/countValid, 0));
    addJson("PM25v_1", String(sumPM25/countValid, 1));
    addJson("Tempinv_1", String(sumTemp/countValid, 2));
    addJson("Humidinv_1", String(sumHumid/countValid, 2));
  } else {
    addJson("CO2v_1", "NULL");
    addJson("PM25v_1", "NULL");
    addJson("Tempinv_1", "NULL");
    addJson("Humidinv_1", "NULL");
  }
  postJson("RnD BiruLangit (1h)", api_post);
}
