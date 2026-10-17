#ifndef SIMULASI_H
#define SIMULASI_H

// Simulator CO2 (meniru MH-Z19B, 0-5000 ppm) dan PM2.5 (meniru PMS5003, 0-1000 ug/m3).
enum SimMode { OUTDOOR_MODE, INDOOR_MODE };
SimMode currentMode = OUTDOOR_MODE;
const float MAX_STEP_CO2 = 40.0f;   // perubahan antarslot maksimum di luar outlier (TOR)
const float MAX_STEP_PM25 = 5.0f;
const int OUTLIER_PERMILLE = 20;    // 2% per sampel per parameter (TOR: 1-3%)
const int RANGE_PERMILLE = 5;       // 0,5% per sampel per parameter (TOR: maks 0,5%)
float baselineCO2 = 450.0f;
float baselinePM25 = 30.0f;
float injCO2 = NAN, injPM25 = NAN, injTemp = NAN, injRH = NAN;
String truthCO2 = "NORMAL", truthPM25 = "NORMAL";   // kunci jawaban, bukan masukan detektor

// Pola dasar CO2 harian; input jam dan menit RTC, output target ppm untuk mode aktif.
// Outdoor: 415-485 ppm, puncak dini hari. Indoor: 450 kosong, naik bertahap 08-17 hingga ~1400, lalu turun perlahan.
float baseCO2(int hour, int minute) {
  float h = hour + minute / 60.0f;
  if (currentMode == OUTDOOR_MODE) return 450.0f + 35.0f * cos((h - 3.0f) * PI / 12.0f);
  const float rise = 950.0f;
  float peak = rise * (1.0f - exp(-9.0f / 2.0f));
  if (h >= 8.0f && h <= 17.0f) return 450.0f + rise * (1.0f - exp(-(h - 8.0f) / 2.0f));
  if (h > 17.0f) return 450.0f + peak * exp(-(h - 17.0f) / 1.5f);
  return 450.0f + peak * exp(-(h + 7.0f) / 1.5f);   // sisa malam sebelumnya, ~450 menjelang pagi
}

// Pola dasar PM2.5 harian; input jam dan menit RTC, output target ug/m3.
// Outdoor 18-73 dengan puncak 06-09 dan 17-20; indoor 0,6 x outdoor.
float basePM25(int hour, int minute) {
  float h = hour + minute / 60.0f;
  float morning = exp(-pow(h - 7.5f, 2.0f) / 2.0f);
  float evening = exp(-pow(h - 18.5f, 2.0f) / 2.0f);
  float outdoor = 18.0f + 55.0f * max(morning, evening);
  return currentMode == OUTDOOR_MODE ? outdoor : 0.60f * outdoor;
}

// Faktor acak 3,00-5,00 untuk besar outlier; tanpa input, output float.
float outlierFactor() {
  return 3.0f + random(0, 201) / 100.0f;
}

// Menghasilkan CO2 satu slot dengan random walk di sekitar pola harian.
// Input jam dan menit slot; output ppm. Outlier/range hanya memengaruhi nilai balik, bukan baseline.
float simCO2(int hour, int minute) {
  float target = baseCO2(hour, minute);
  float noise = random(-60, 61) / 10.0f;     // +-6 ppm, outdoor tetap di 400-500 ppm
  float step = constrain((target - baselineCO2) * 0.20f + noise, -MAX_STEP_CO2, MAX_STEP_CO2);
  baselineCO2 = max(0.0f, baselineCO2 + step);
  truthCO2 = "NORMAL";
  if (random(1000) < RANGE_PERMILLE) {
    truthCO2 = "SIM_RANGE";
    float value = 5200.0f + random(0, 801);
    Serial.printf("[SIM] RANGE CO2 %.0f\n", value);
    return value;
  }
  if (random(1000) < OUTLIER_PERMILLE) {
    truthCO2 = "SIM_OUTLIER";
    float value = baselineCO2 * outlierFactor();
    Serial.printf("[SIM] OUTLIER CO2 %.0f\n", value);
    return value;
  }
  return baselineCO2;
}

// Menghasilkan PM2.5 satu slot; indoor memakai noise lebih kecil agar lebih halus.
// Input jam dan menit slot; output ug/m3.
float simPM25(int hour, int minute) {
  float target = basePM25(hour, minute);
  float noiseScale = currentMode == OUTDOOR_MODE ? 150.0f : 60.0f;   // +-1,5 atau +-0,6
  float noise = random(-(long)noiseScale, (long)noiseScale + 1) / 100.0f;
  float step = constrain((target - baselinePM25) * 0.20f + noise, -MAX_STEP_PM25, MAX_STEP_PM25);
  baselinePM25 = max(0.0f, baselinePM25 + step);
  truthPM25 = "NORMAL";
  if (random(1000) < RANGE_PERMILLE) {
    truthPM25 = "SIM_RANGE";
    float value = 1050.0f + random(0, 451);
    Serial.printf("[SIM] RANGE PM25 %.1f\n", value);
    return value;
  }
  if (random(1000) < OUTLIER_PERMILLE) {
    truthPM25 = "SIM_OUTLIER";
    float value = baselinePM25 * outlierFactor();
    Serial.printf("[SIM] OUTLIER PM25 %.1f\n", value);
    return value;
  }
  return baselinePM25;
}

// Memvalidasi teks angka penuh untuk perintah INJ; input teks, output true dan value bila sah.
bool parseInjectionValue(String text, float &value) {
  text.trim();
  if (!text.length()) return false;
  char *endPtr = NULL;
  value = strtof(text.c_str(), &endPtr);
  return endPtr != text.c_str() && *endPtr == '\0' && !isnan(value) && !isinf(value);
}

// Menangani "ENV indoor|outdoor" dan "INJ <co2|pm25|temp|rh> <nilai>".
// Input satu baris perintah; injeksi berlaku tepat untuk satu slot berikutnya.
void handleSimCommand(String command) {
  command.trim();
  if (command.startsWith("ENV ")) {
    String mode = command.substring(4); mode.trim(); mode.toLowerCase();
    if (mode == "indoor") currentMode = INDOOR_MODE;
    else if (mode == "outdoor") currentMode = OUTDOOR_MODE;
    else { Serial.println("[CMD] Gunakan ENV indoor|outdoor"); return; }
    TimeIndex now = buildTimeIndex(rtc.getEpoch());
    baselineCO2 = baseCO2(now.hour, now.minute);
    baselinePM25 = basePM25(now.hour, now.minute);
    Serial.println("[SIM] ENV=" + mode);
    return;
  }
  if (!command.startsWith("INJ ")) { Serial.println("[CMD] Perintah tidak dikenal"); return; }
  int separator = command.indexOf(' ', 4);
  if (separator < 0) { Serial.println("[CMD] Gunakan INJ <co2|pm25|temp|rh> <nilai>"); return; }
  String name = command.substring(4, separator); name.toLowerCase();
  float value;
  if (!parseInjectionValue(command.substring(separator + 1), value)) {
    Serial.println("[CMD] Nilai INJ tidak valid"); return;
  }
  if (name == "co2") injCO2 = value;
  else if (name == "pm25") injPM25 = value;
  else if (name == "temp") injTemp = value;
  else if (name == "rh") injRH = value;
  else { Serial.println("[CMD] Parameter INJ tidak dikenal"); return; }
  Serial.println("[CMD] INJ " + name + "=" + String(value) + " untuk satu slot");
}

#endif
