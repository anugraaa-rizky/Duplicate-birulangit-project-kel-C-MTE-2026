// AQMS ESP32: controller slot dua menit, edge processing, storage, dan HTTPS (TOR Tubes P3-P4).
// Target: ESP32 Dev Module, arduino-esp32 v1.0.6, partisi "Default 4MB with spiffs".
// Library: ESP32Time (fbiego), Adafruit SHT31.
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ESP32Time.h>
#include <Wire.h>
#include <Adafruit_SHT31.h>

// ---------- Konfigurasi (ditulis sebelum #include .h yang memakainya) ----------
const String group_name = "Kelompok_C";
// Isi hanya saat testing lokal. Kembalikan ke placeholder sebelum dikumpulkan.
const char* ssid = "NAMA_WIFI";
const char* password = "PASSWORD_WIFI";
const char* api_time = "https://rndbirulangit.pythonanywhere.com/api/get-time";
const char* api_post = "https://rndbirulangit.pythonanywhere.com/api/post/telkom_group";
// Backend lokal FastAPI (folder backend/). Pakai IP LAN laptop, bukan localhost.
const bool LOCAL_API_ENABLED = false;
const char* api_local = "http://192.168.1.10:8000/api/readings";
const char* local_device_id = "aqms-01";

const unsigned long SLOT_SECONDS = 120UL;
const unsigned long RTC_RETRY_INVALID_MS = 30000UL;   // F3: tiap 30 s selama tahun RTC < 2025
const unsigned long RTC_RETRY_DAILY_MS = 300000UL;    // retry sinkron harian bila gagal
const unsigned long WIFI_TIMEOUT_MS = 15000UL;        // F9: batas sambung 15 s
const int RESEND_PER_SLOT = 10;                       // bonus: batas kirim ulang per slot

// ---------- Variabel global ----------
WiFiClientSecure *secureClient = nullptr;   // satu objek, dibuat di setup(), dipakai ulang semua POST HTTPS
ESP32Time rtc(0);                           // RTC menyimpan jam dinding WIB
Adafruit_SHT31 sht31 = Adafruit_SHT31();
bool shtInitialized = false;
unsigned long lastRtcAttemptMs = 0;
String lastDailySyncKey = "";
long lastSlotKey = -1;

#include "Date_Time.h"
#include "Simulasi.h"
#include "Edge.h"
#include "Storage.h"
#include "Send.h"

// Menyambung WiFi STA dengan batas 15 detik; tanpa input, output true bila terhubung.
bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  Serial.println("[WiFi] connecting");
  WiFi.mode(WIFI_STA); WiFi.disconnect(); delay(50); WiFi.begin(ssid, password);
  unsigned long started = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - started < WIFI_TIMEOUT_MS) delay(250);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WiFi] connected IP=" + WiFi.localIP().toString() + " RSSI=" + String(WiFi.RSSI()));
    return true;
  }
  Serial.println("[WiFi] timeout; edge processing tetap aktif");
  return false;
}

// Inisialisasi SHT31 fisik pada I2C 0x44 (SDA 21, SCL 22); tanpa input/output.
void initSensor() {
  Wire.begin(21, 22);
  shtInitialized = sht31.begin(0x44);
  Serial.println(shtInitialized ? "[SHT31] ready at 0x44" : "[SHT31] tidak terdeteksi");
}

// Membaca SHT31 satu kali (F1). CRC salah atau tanpa respons -> NAN.
// Output true bila sah; temperature/humidity berisi nilai atau NAN.
bool readSht(float &temperature, float &humidity) {
  temperature = NAN; humidity = NAN;
  if (!shtInitialized) {
    shtInitialized = sht31.begin(0x44);
    if (!shtInitialized) return false;
  }
  if (!sht31.readBoth(&temperature, &humidity) || isnan(temperature) || isnan(humidity)) {
    temperature = NAN; humidity = NAN; shtInitialized = false;
    return false;
  }
  return true;
}

// Sinkron RTC: tiap 30 s selama RTC belum sah, dan sekali sehari mulai 00:01 WIB (F3).
void maintainRtc() {
  bool valid = rtc.getYear() >= 2025;
  unsigned long interval = valid ? RTC_RETRY_DAILY_MS : RTC_RETRY_INVALID_MS;
  bool due = !valid;
  if (valid) {
    TimeIndex now = buildTimeIndex(rtc.getEpoch());
    due = lastDailySyncKey != now.dayKey && now.minOfDay >= 1;
  }
  if (due && (lastRtcAttemptMs == 0 || millis() - lastRtcAttemptMs >= interval)) {
    lastRtcAttemptMs = millis();
    if (connectWiFi() && syncRtc()) lastDailySyncKey = getDayKey();
  }
}

// Mencetak ringkasan kondisi perangkat (perintah STATUS); tanpa input/output.
void printStatus() {
  Serial.println("[STATUS] rtc=" + getDateTime() + " uptime=" + getUptime());
  Serial.println(String("[STATUS] mode=") + (currentMode == INDOOR_MODE ? "indoor" : "outdoor") +
                 " wifi=" + (WiFi.status() == WL_CONNECTED ? "1" : "0") +
                 " spiffs=" + (spiffsMounted ? "1" : "0") + " sht=" + (shtInitialized ? "1" : "0"));
  Serial.printf("[STATUS] queue tor=%d local=%d\n", queueCount(QUEUE_TOR), queueCount(QUEUE_LOCAL));
}

// Menangani ENV, INJ, DUMP, LS, dan STATUS dari satu baris Serial.
void handleSerial() {
  if (!Serial.available()) return;
  String command = Serial.readStringUntil('\n'); command.trim();
  if (command.startsWith("ENV ") || command.startsWith("INJ ")) handleSimCommand(command);
  else if (command.startsWith("DUMP")) handleDumpCommand(command);
  else if (command == "LS") handleListCommand();
  else if (command == "STATUS") printStatus();
  else if (command.length()) Serial.println("[CMD] Gunakan ENV, INJ, DUMP, LS, atau STATUS");
}

// Menyimpan hasil jam ke /h<dayKey>.csv, memperbarui rata-rata 24 jam, dan mengirim 1h_valid.
// Input HourlyResult jam yang baru ditutup.
void publishHour(const HourlyResult &r) {
  Moving24Result m = updateMoving24(r);
  TimeIndex idx = buildTimeIndex(r.startEpoch);
  String row = formatEpoch(r.startEpoch) + "," +
    valueOrNull(r.temp, true, 2) + "," + valueOrNull(r.humid, true, 2) + "," +
    valueOrNull(r.co2, true, 0) + "," + valueOrNull(r.pm25, true, 1) + "," +
    String(r.countTemp) + "," + String(r.countHumid) + "," +
    String(r.countCO2) + "," + String(r.countPM25) + "," +
    valueOrNull(m.temp, true, 2) + "," + valueOrNull(m.humid, true, 2) + "," +
    valueOrNull(m.co2, true, 0) + "," + valueOrNull(m.pm25, true, 1);
  appendCsv("/h" + idx.dayKey + ".csv", HOURLY_HEADER, row);
  Serial.println("[1H] " + row);

  Payload p;
  p.createdAt = formatEpoch(r.startEpoch);
  p.co2 = valueOrNull(r.co2, true, 0); p.pm25 = valueOrNull(r.pm25, true, 1);
  p.temp = valueOrNull(r.temp, true, 2); p.humid = valueOrNull(r.humid, true, 2);
  p.pm25Ok = !isnan(r.pm25); p.shtOk = !isnan(r.temp) && !isnan(r.humid);
  sendData("1h_valid", p);
}

// Pipeline lengkap satu slot RTC, mengikuti urutan alur data TOR.
// Input epoch awal slot (menit genap, detik 0, WIB).
void processSlot(time_t slotEpoch) {
  TimeIndex idx = buildTimeIndex(slotEpoch);
  String timestamp = formatEpoch(slotEpoch);
  time_t hourStart = slotEpoch - (time_t)idx.minute * 60;
  Serial.printf("[SLOT] %s dayKey=%s jam=%d menit=%d minOfDay=%d\n", timestamp.c_str(),
                idx.dayKey.c_str(), idx.hour, idx.minute, idx.minOfDay);
  bool online = connectWiFi();   // F9: cek ulang tiap slot sebelum kirim

  // 1. Akuisisi SHT31 dan simulator CO2/PM2.5; injeksi INJ hanya untuk slot ini.
  float temp, humid;
  readSht(temp, humid);
  float co2 = simCO2(idx.hour, idx.minute);
  float pm25 = simPM25(idx.hour, idx.minute);
  if (!isnan(injCO2)) { co2 = injCO2; injCO2 = NAN; truthCO2 = "INJ"; }
  if (!isnan(injPM25)) { pm25 = injPM25; injPM25 = NAN; truthPM25 = "INJ"; }
  if (!isnan(injTemp)) { temp = injTemp; injTemp = NAN; }
  if (!isnan(injRH)) { humid = injRH; injRH = NAN; }
  Serial.printf("[DATA] T=%.2f RH=%.2f CO2=%.0f PM25=%.1f truth=%s/%s\n",
                temp, humid, co2, pm25, truthCO2.c_str(), truthPM25.c_str());

  // 2. Kirim data mentah apa adanya. Status = gagal baca (F_SENSOR).
  if (online) flushQueue(QUEUE_TOR, api_post, true, RESEND_PER_SLOT);
  Payload raw;
  raw.createdAt = timestamp;
  raw.co2 = valueOrNull(co2, true, 0); raw.pm25 = valueOrNull(pm25, true, 1);
  raw.temp = valueOrNull(temp, true, 2); raw.humid = valueOrNull(humid, true, 2);
  raw.pm25Ok = !isnan(pm25); raw.shtOk = !isnan(temp) && !isnan(humid);
  sendData("2m_raw", raw);

  // 3. Validasi rentang ukur dan outlier IQR per parameter.
  QualityFlag ft = validateParam(temp, -40.0f, 125.0f, winTemp, MIN_IQR_TEMP, 0.0f);
  QualityFlag fh = validateParam(humid, 0.0f, 100.0f, winHumid, MIN_IQR_RH, 0.0f);
  QualityFlag fc = validateParam(co2, 0.0f, 5000.0f, winCO2, MIN_IQR_CO2, MIN_IQR_REL_GAS);
  QualityFlag fp = validateParam(pm25, 0.0f, 1000.0f, winPM25, MIN_IQR_PM25, MIN_IQR_REL_GAS);
  Serial.printf("[FLAG] T=%d RH=%d CO2=%d PM25=%d\n", ft, fh, fc, fp);

  // 4. Simpan record 2 menit (dengan kunci jawaban) lalu kirim data tervalidasi.
  String row = timestamp + "," + String(idx.minOfDay) + "," +
    valueOrNull(temp, true, 2) + "," + valueOrNull(humid, true, 2) + "," +
    valueOrNull(co2, true, 0) + "," + valueOrNull(pm25, true, 1) + "," +
    String((int)ft) + "," + String((int)fh) + "," + String((int)fc) + "," + String((int)fp) + "," +
    truthCO2 + "," + truthPM25;
  appendCsv("/m" + idx.dayKey + ".csv", MINUTE_HEADER, row);
  checkStorageRotation();

  Payload val;
  val.createdAt = timestamp;
  val.co2 = valueOrNull(co2, fc == F_CLEAN, 0); val.pm25 = valueOrNull(pm25, fp == F_CLEAN, 1);
  val.temp = valueOrNull(temp, ft == F_CLEAN, 2); val.humid = valueOrNull(humid, fh == F_CLEAN, 2);
  val.pm25Ok = fp == F_CLEAN; val.shtOk = ft == F_CLEAN && fh == F_CLEAN;
  sendData("2m_validated", val);

  if (online) flushQueue(QUEUE_LOCAL, api_local, false, RESEND_PER_SLOT);
  sendLocalReading((long)slotEpoch - WIB_OFFSET, temp, humid, co2, pm25, ft, fh, fc, fp);

  // 5. Akumulator jam. Saat jam berganti: tutup jam lama, kirim 1h_valid, hitung 24 jam.
  if (hourly.startEpoch == 0) resetHourly(hourStart);
  else if (hourStart != hourly.startEpoch) {
    HourlyResult done = finishHourly();
    resetHourly(hourStart);
    publishHour(done);
  }
  accumulateHourly(ft, fh, fc, fp, temp, humid, co2, pm25);
}

// Inisialisasi hardware, storage, network, dan RTC; dijalankan sekali saat boot.
void setup() {
  Serial.begin(115200); Serial.setTimeout(100);
  Serial.println("\n[AQMS] boot " + group_name);
  randomSeed(esp_random());
  secureClient = new WiFiClientSecure();
  secureClient->setInsecure();
  WiFi.setAutoReconnect(true);
  initIQRWindows(); initSensor(); initStorage();
  if (connectWiFi() && syncRtc()) lastDailySyncKey = getDayKey();
  lastRtcAttemptMs = millis();
}

// Scheduler non-blocking (F4): sampling dipicu pergantian slot 120 s RTC, bukan delay().
// Slot yang terdeteksi terlambat (loop sempat terblokir) tetap dicap awal slot HH:MM genap:00.
void loop() {
  handleSerial();
  maintainRtc();
  if (rtc.getYear() >= 2025) {   // T7: tidak pernah memproses/mengirim data bertahun 1970
    unsigned long epoch = rtc.getEpoch();
    long slotKey = (long)(epoch / SLOT_SECONDS);
    if (lastSlotKey < 0) lastSlotKey = (epoch % SLOT_SECONDS == 0) ? slotKey - 1 : slotKey;
    if (slotKey > lastSlotKey) {
      lastSlotKey = slotKey;
      processSlot((time_t)slotKey * SLOT_SECONDS);
    }
  }
  delay(20);
}
