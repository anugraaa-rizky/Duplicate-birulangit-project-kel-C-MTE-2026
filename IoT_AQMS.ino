// =====================================================================
//  IoT_AQMS.ino - FINAL (Anti-Loss + Non-Blocking Queue + Accurate Time Index)
// =====================================================================
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESP32Time.h>
#include <Wire.h>
#include "Adafruit_SHT31.h"

// ===== 1. Identitas & Konfigurasi =====
const String group_name = "Kelompok_C";
const char* NAMA_WIFI     = "NAMA_WIFI";
const char* PASSWORD_WIFI = "PASSWORD_WIFI";
const char* api_time = "https://rndbirulangit.pythonanywhere.com/api/get-time";
const char* api_post = "https://rndbirulangit.pythonanywhere.com/api/post/telkom_group";

// ===== 2. Variabel Global =====
WiFiClientSecure client;
ESP32Time rtc(0);
Adafruit_SHT31 sht31;

float Tempinv = NAN, Humidinv = NAN;
bool SHT_status = false;
float CO2v = NAN, PM25v = NAN;

int lastSyncDay = -1;
unsigned long lastTrySync = 0;
unsigned long lastIdleQueueCheck = 0;
String lastExecutedSlotTime = "";  // Kunci timer: cegah slot ganda & menit ganjil
bool wasConnected = false;

// ===== 3. Include Modules (urutan WAJIB seperti ini) =====
#include "Date_Time.h"
#include "Simulasi.h"
#include "Edge.h"
#include "Storage.h"
#include "Send.h"

// ===== 4. Akuisisi Sensor =====
void initSensors() {
  Wire.begin(21, 22);
  if (sht31.begin(0x44)) { SHT_status = true; }
  else { SHT_status = false; }
}

void readSensors() {
  simAnomalyHint = "";
  float t = sht31.readTemperature();
  float h = sht31.readHumidity();
  if (!isnan(t) && !isnan(h)) { Tempinv = t; Humidinv = h; SHT_status = true; }
  else { Tempinv = NAN; Humidinv = NAN; SHT_status = false; }

  int currentHour = rtc.getHour(true);
  int currentMinute = rtc.getMinute();
  CO2v = simCO2(currentHour, currentMinute);
  PM25v = simPM25(currentHour, currentMinute);

  // Injeksi Manual (prioritas tertinggi)
  if (!isnan(injCO2))  { CO2v = injCO2; injCO2 = NAN; simAnomalyHint = "INJ_CO2"; }
  if (!isnan(injPM25)) { PM25v = injPM25; injPM25 = NAN; simAnomalyHint = "INJ_PM25"; }
  if (!isnan(injTemp)) { Tempinv = injTemp; injTemp = NAN; simAnomalyHint = "INJ_TEMP"; }
  if (!isnan(injRH))   { Humidinv = injRH; injRH = NAN; simAnomalyHint = "INJ_RH"; }
}

// ===== 5. Koneksi WiFi (F9 TOR: Batas Waktu 15 detik) =====
bool checkWiFiConnection(int timeoutSec = 15) {
  if (WiFi.status() == WL_CONNECTED) return true;

  Serial.print("[WiFi] Menghubungkan ke " + String(NAMA_WIFI));
  WiFi.disconnect(true); // Reset state machine WiFi station
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.begin(NAMA_WIFI, PASSWORD_WIFI);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0 < timeoutSec * 1000)) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" OK, IP " + WiFi.localIP().toString());
    return true;
  } else {
    Serial.println(" GAGAL (Offline)");
    return false;
  }
}

// Penjagaan waktu RTC & deteksi pemulihan WiFi tanpa memblokir CPU
void maintainWiFiAndTime() {
  bool isConnected = (WiFi.status() == WL_CONNECTED);

  // Otomatis deteksi saat WiFi kembali menyala di antara slot
  if (isConnected && !wasConnected) {
    Serial.println("\n[WiFi] Terhubung kembali! Memproses antrian data tertunda...");
    processQueue(3); // Kirim bertahap 3 data
  }
  wasConnected = isConnected;

  // Sinkronisasi Waktu RTC (Hanya jika WiFi nyala)
  if (!isConnected) return;
  if (millis() - lastTrySync < 30000) return;

  bool yearValid = rtc.getYear() >= 2025;
  bool dailySync = yearValid && rtc.getHour(true) == 0 && rtc.getMinute() == 1 && rtc.getDay() != lastSyncDay;
  if (!yearValid || dailySync) {
    lastTrySync = millis();
    if (Sync_RTC()) lastSyncDay = rtc.getDay();
  }
}

// ===== 6. Setup =====
void setup() {
  Serial.begin(115200);
  randomSeed(esp_random());
  client.setInsecure();
  client.setTimeout(6);

  // Aktifkan mode STA dan Auto-Reconnect hardware ESP32
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);

  initSensors();
  initStorage();
  initIQRWindows();
  initSend();

  checkWiFiConnection(15);
  if (WiFi.status() == WL_CONNECTED) {
    wasConnected = true;
    Sync_RTC();
    processQueue(3);
  }
}

// ===== 7. Loop Utama =====
void loop() {
  maintainWiFiAndTime();

  // Perintah Serial Monitor
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd.startsWith("ENV") || cmd.startsWith("INJ")) {
      checkSimSerial(cmd);
      if (cmd.startsWith("ENV")) {
        initIQRWindows(); // Reset jendela IQR agar langsung beradaptasi dengan mode baru
        Serial.println("[IQR] Jendela IQR di-reset untuk mode baru.");
      }
    }
    else if (cmd.startsWith("DUMP")) handleDumpCommand(cmd);
    else if (cmd == "LIST") printFileList();
    else if (cmd == "BOXPLOT") printBoxplot();
    else if (cmd == "STAT") printSendStats();
    else if (cmd == "CLEAR") clearStorage();
    else if (cmd == "FIX") fixMisplaced23Hour();
    else if (cmd == "QUEUE" || cmd == "SENDQUEUE") processQueue(5);
  }

  // Pemrosesan antrian tertunda secara bertahap saat waktu idle (di sela-sela slot)
  // Hanya jika WiFi terhubung dan BUKAN 15 detik sebelum slot genap tiba
  if (WiFi.status() == WL_CONNECTED && (millis() - lastIdleQueueCheck > 8000)) {
    lastIdleQueueCheck = millis();
    int curMin = rtc.getMinute();
    int curSec = rtc.getSecond();
    if (!(curMin % 2 == 1 && curSec >= 45) && !(curMin % 2 == 0 && curSec < 10)) {
      processQueue(3); // Kirim bertahap maksimal 3 data per idle tick
    }
  }

  int currentMinute = rtc.getMinute();
  int currentHour = rtc.getHour(true);
  String currentSlotKey = String(currentHour) + ":" + String(currentMinute);

  // TIMER ROBUST: Menit genap dan slot belum diproses (Tanpa dibatasi currentSecond < 5 agar slot tidak pernah hilang!)
  if (rtc.getYear() >= 2025 &&
      currentMinute % 2 == 0 &&
      currentSlotKey != lastExecutedSlotTime) {

    lastExecutedSlotTime = currentSlotKey;

    // 1. Akuisisi Sensor
    readSensors();

    // 2. Validasi Edge Computing (F5 & F6)
    int fT = validateParam(Tempinv, -40, 125, winTemp, 0.5);
    int fH = validateParam(Humidinv, 0, 100, winHumid, 2.5);
    int fC = validateParam(CO2v, 0, 5000, winCO2, 10.0);
    int fP = validateParam(PM25v, 0, 1000, winPM25, 2.0);

    // 3. PRIORITAS TERTINGGI: Simpan record 2 menit ke SPIFFS langsung di awal!
    // Format nama file: /m<dayKey>.csv dengan dayKey YYYYMMDD sesuai TOR F4 & F7
    String mFile = "/m" + get_day_key() + ".csv";
    String mContent = get_slot_time() + "," + String(Tempinv,2) + "," + String(Humidinv,2) + "," +
                      String(CO2v,0) + "," + String(PM25v,1) + "," +
                      String(fT) + "," + String(fH) + "," + String(fC) + "," + String(fP) + "," + simAnomalyHint;
    appendCSV(mFile, mContent);
    checkStorageRotation();

    // 4. Tutup rata-rata jam lama saat jam berganti (HH:00)
    if (currentHour != lastHourAvg && lastHourAvg != -1) {
      bool isValid = (countValid >= 23);
      Serial.printf("[JAM %02d] Selesai. Valid: %d/30. Status: %s\n",
                    lastHourAvg, countValid, isValid ? "DIHITUNG" : "NULL");

      // Dapatkan timestamp awal jam yang dirata-rata dan dayKey yang tepat (menangani pergantian hari/bulan/tahun)
      String avgCreatedAt, avgDayKey;
      get_hourly_avg_time(avgCreatedAt, avgDayKey);

      // Kirim 1h_valid ke server (jika offline, otomatis masuk antrian SPIFFS)
      send1hValid(countValid, isValid, avgCreatedAt);

      // Simpan ke /h<dayKey>.csv yang sesuai dengan hari data tersebut
      String hFile = "/h" + avgDayKey + ".csv";
      String avgT = isValid ? String(sumTemp/countValid, 2) : "NULL";
      String avgH = isValid ? String(sumHumid/countValid, 2) : "NULL";
      String avgC = isValid ? String(sumCO2/countValid, 0) : "NULL";
      String avgP = isValid ? String(sumPM25/countValid, 1) : "NULL";

      String hContent = avgCreatedAt + "," + avgT + "," + avgH + "," + avgC + "," + avgP + "," + String(countValid);
      appendCSV(hFile, hContent);
      resetHourlyAvg();
    }

    if (lastHourAvg == -1) lastHourAvg = currentHour;
    updateHourlyAvg(currentHour, fT, fH, fC, fP, Tempinv, Humidinv, CO2v, PM25v);

    // 5. Cek koneksi WiFi sebelum kirim (sesuai TOR F9: "cek ulang setiap slot sebelum kirim")
    if (WiFi.status() != WL_CONNECTED) {
      checkWiFiConnection(15);
    }

    // 6. Kirim data slot saat ini TERLEBIH DAHULU (Raw & Validated)
    send2mRaw();
    send2mValidated(fT, fH, fC, fP, Tempinv, Humidinv, CO2v, PM25v);

    // 7. Jika ada sisa antrian lama di SPIFFS, proses bertahap maksimal 3 data (Bonus TOR +10)
    if (WiFi.status() == WL_CONNECTED) {
      processQueue(3);
    }
  }
  delay(50);
}
