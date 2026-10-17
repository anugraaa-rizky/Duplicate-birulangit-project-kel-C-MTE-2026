#ifndef EDGE_H
#define EDGE_H

// Flag kualitas per parameter. Urutan nilai dipakai juga di CSV dan backend lokal.
// F_CLEAN (bukan F_OK): F_OK sudah dipakai makro POSIX unistd.h di core ESP32.
enum QualityFlag { F_CLEAN = 0, F_SENSOR = 1, F_RANGE = 2, F_OUTLIER = 3 };
const int IQR_SIZE = 30;           // jendela 30 sampel per parameter
const int IQR_MIN_SAMPLES = 10;    // uji outlier aktif setelah 10 sampel
const float IQR_K = 1.5f;
const int HOURLY_MIN_VALID = 23;   // rata-rata jam sah bila >= 23 dari 30 sampel
const int MOVING24_MIN_VALID = 18; // rata-rata 24 jam sah bila >= 18 dari 24 jam

// IQR minimum = max(batas absolut, fraksi x median jendela) agar sinyal stabil atau tren naik
// (jam sibuk PM2.5, CO2 indoor mulai 08:00) tidak salah tanda. Fraksi relatif hanya untuk gas
// simulasi; SHT31 cukup batas absolut. Hasil uji simulasi 3 hari x 5 seed: presisi ~1,00, recall 1,00.
const float MIN_IQR_TEMP = 0.5f, MIN_IQR_RH = 2.0f, MIN_IQR_CO2 = 20.0f, MIN_IQR_PM25 = 2.0f;
const float MIN_IQR_REL_GAS = 0.30f;

struct IQRWindow { float values[IQR_SIZE]; int count; int head; };
IQRWindow winTemp, winHumid, winCO2, winPM25;

struct HourlyAccumulator {
  double sumTemp, sumHumid, sumCO2, sumPM25;
  int countTemp, countHumid, countCO2, countPM25;
  time_t startEpoch;
};

struct HourlyResult {
  float temp, humid, co2, pm25;
  int countTemp, countHumid, countCO2, countPM25;
  time_t startEpoch;
};
HourlyAccumulator hourly;

struct Moving24Window { float values[24]; int count; int head; };
Moving24Window dayTemp, dayHumid, dayCO2, dayPM25;

struct Moving24Result { float temp, humid, co2, pm25; };

// Mengosongkan semua window IQR dan 24 jam saat boot; tanpa input/output.
void initIQRWindows() {
  memset(&winTemp, 0, sizeof(winTemp)); memset(&winHumid, 0, sizeof(winHumid));
  memset(&winCO2, 0, sizeof(winCO2)); memset(&winPM25, 0, sizeof(winPM25));
  memset(&dayTemp, 0, sizeof(dayTemp)); memset(&dayHumid, 0, sizeof(dayHumid));
  memset(&dayCO2, 0, sizeof(dayCO2)); memset(&dayPM25, 0, sizeof(dayPM25));
  memset(&hourly, 0, sizeof(hourly));
}

// Menambahkan satu nilai ke circular buffer IQR; input window dan nilai.
void addIQR(IQRWindow &window, float value) {
  if (window.count < IQR_SIZE) window.values[window.count++] = value;
  else { window.values[window.head] = value; window.head = (window.head + 1) % IQR_SIZE; }
}

// Mengambil kuantil dengan interpolasi linear dari array terurut; output nilai kuantil.
float percentile(const float sorted[], int count, float fraction) {
  float position = (count - 1) * fraction;
  int lower = (int)floor(position), upper = (int)ceil(position);
  if (lower == upper) return sorted[lower];
  return sorted[lower] + (sorted[upper] - sorted[lower]) * (position - lower);
}

// Validasi satu parameter: F_SENSOR (NAN), F_RANGE (di luar rentang ukur), F_OUTLIER (boxplot IQR).
// Input nilai, rentang, window, IQR minimum absolut dan relatif terhadap median; output flag.
// Sesuai F6, hanya F_RANGE/F_SENSOR yang tidak masuk jendela; nilai F_CLEAN dan F_OUTLIER masuk
// agar jendela tetap mengikuti perubahan level yang nyata.
QualityFlag validateParam(float value, float minimum, float maximum, IQRWindow &window,
                          float minIqr, float minIqrRel) {
  if (isnan(value) || isinf(value)) return F_SENSOR;
  if (value < minimum || value > maximum) return F_RANGE;
  QualityFlag flag = F_CLEAN;
  if (window.count >= IQR_MIN_SAMPLES) {
    float sorted[IQR_SIZE];
    for (int i = 0; i < window.count; ++i) sorted[i] = window.values[i];
    for (int i = 1; i < window.count; ++i) {
      float key = sorted[i]; int j = i - 1;
      while (j >= 0 && sorted[j] > key) { sorted[j + 1] = sorted[j]; --j; }
      sorted[j + 1] = key;
    }
    float q1 = percentile(sorted, window.count, 0.25f);
    float q3 = percentile(sorted, window.count, 0.75f);
    float median = percentile(sorted, window.count, 0.50f);
    float iqr = max(q3 - q1, max(minIqr, minIqrRel * fabsf(median)));
    float lower = q1 - IQR_K * iqr, upper = q3 + IQR_K * iqr;
    if (value < lower || value > upper) flag = F_OUTLIER;
  }
  addIQR(window, value);
  return flag;
}

// Memulai akumulator untuk jam yang ditunjuk; input epoch awal jam.
void resetHourly(time_t startEpoch) {
  memset(&hourly, 0, sizeof(hourly)); hourly.startEpoch = startEpoch;
}

// Mengakumulasi tiap parameter berflag 0 secara independen; input flag dan nilai.
void accumulateHourly(QualityFlag ft, QualityFlag fh, QualityFlag fc, QualityFlag fp,
                      float t, float h, float c, float p) {
  if (ft == F_CLEAN) { hourly.sumTemp += t; hourly.countTemp++; }
  if (fh == F_CLEAN) { hourly.sumHumid += h; hourly.countHumid++; }
  if (fc == F_CLEAN) { hourly.sumCO2 += c; hourly.countCO2++; }
  if (fp == F_CLEAN) { hourly.sumPM25 += p; hourly.countPM25++; }
}

// Menutup satu jam dan menerapkan kelengkapan per parameter; output HourlyResult (NAN bila tidak sah).
HourlyResult finishHourly() {
  HourlyResult r;
  r.startEpoch = hourly.startEpoch;
  r.countTemp = hourly.countTemp; r.countHumid = hourly.countHumid;
  r.countCO2 = hourly.countCO2; r.countPM25 = hourly.countPM25;
  r.temp = r.countTemp >= HOURLY_MIN_VALID ? hourly.sumTemp / r.countTemp : NAN;
  r.humid = r.countHumid >= HOURLY_MIN_VALID ? hourly.sumHumid / r.countHumid : NAN;
  r.co2 = r.countCO2 >= HOURLY_MIN_VALID ? hourly.sumCO2 / r.countCO2 : NAN;
  r.pm25 = r.countPM25 >= HOURLY_MIN_VALID ? hourly.sumPM25 / r.countPM25 : NAN;
  return r;
}

// Menambahkan hasil jam (termasuk NAN) ke window 24 jam; input window dan nilai.
void addMovingHour(Moving24Window &window, float value) {
  if (window.count < 24) window.values[window.count++] = value;
  else { window.values[window.head] = value; window.head = (window.head + 1) % 24; }
}

// Rata-rata 24 jam bila sedikitnya 18 nilai jam sah; output float atau NAN.
float moving24Average(const Moving24Window &window) {
  double sum = 0; int valid = 0;
  for (int i = 0; i < window.count; ++i) if (!isnan(window.values[i])) { sum += window.values[i]; valid++; }
  return valid >= MOVING24_MIN_VALID ? sum / valid : NAN;
}

// Memasukkan satu hasil jam, mencetak, dan mengembalikan rata-rata bergerak 24 jam.
Moving24Result updateMoving24(const HourlyResult &result) {
  addMovingHour(dayTemp, result.temp); addMovingHour(dayHumid, result.humid);
  addMovingHour(dayCO2, result.co2); addMovingHour(dayPM25, result.pm25);
  Moving24Result m;
  m.temp = moving24Average(dayTemp); m.humid = moving24Average(dayHumid);
  m.co2 = moving24Average(dayCO2); m.pm25 = moving24Average(dayPM25);
  Serial.println("[24H] TEMP=" + (isnan(m.temp) ? String("NULL") : String(m.temp, 2)) +
                 " RH=" + (isnan(m.humid) ? String("NULL") : String(m.humid, 2)) +
                 " CO2=" + (isnan(m.co2) ? String("NULL") : String(m.co2, 0)) +
                 " PM25=" + (isnan(m.pm25) ? String("NULL") : String(m.pm25, 1)));
  return m;
}

#endif
