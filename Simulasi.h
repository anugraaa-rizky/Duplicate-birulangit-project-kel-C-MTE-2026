// =====================================================================
//  Simulasi.h - FINAL (Anti Outlier Beruntun + Cooldown Sesuai TOR)
// =====================================================================
#include <Arduino.h>

enum SimMode { OUTDOOR_MODE, INDOOR_MODE };
SimMode currentMode = OUTDOOR_MODE;

float injCO2 = NAN, injPM25 = NAN, injTemp = NAN, injRH = NAN;
String simAnomalyHint = "";

// Cooldown untuk menjamin outlier HANYA berupa "lonjakan satu sampel" (sesuai TOR)
int cooldownOutlierCO2 = 0;
int cooldownOutlierPM25 = 0;
int cooldownRangeCO2 = 0;
int cooldownRangePM25 = 0;

float rnd(float a, float b) {
  return a + (b - a) * (random(10001) / 10000.0);
}

// ===== CO2 =====
float simCO2(int hour, int minute) {
  float timeFactor = hour + minute / 60.0;
  float targetBase;

  if (currentMode == OUTDOOR_MODE) {
    // Outdoor: Cosinus, puncak malam/pagi
    targetBase = 450.0 + 30.0 * cos((timeFactor - 3.0) * PI / 12.0);
  } else {
    // Indoor: Naik bertahap selama jam aktivitas (08-17), lalu turun perlahan
    if (timeFactor >= 8.0 && timeFactor <= 17.0) {
      targetBase = 450.0 + 750.0 * sin((timeFactor - 8.0) * PI / 9.0);
    } else {
      targetBase = 450.0;
    }
  }

  // Fluktuasi halus (< 40 ppm per slot sesuai kontinuitas TOR)
  float val = targetBase + rnd(-6.0, 6.0);

  // Update Cooldown
  if (cooldownOutlierCO2 > 0) cooldownOutlierCO2--;
  if (cooldownRangeCO2 > 0) cooldownRangeCO2--;

  // Outlier: Lonjakan satu sampel (peluang 1-3%), 3-5x baseline, tanpa beruntun
  if (cooldownOutlierCO2 == 0 && cooldownRangeCO2 == 0 && random(100) < 2) {
    if (currentMode == OUTDOOR_MODE) {
      val = targetBase * rnd(3.0, 4.0); // 1350 - 1800 ppm
    } else {
      val = targetBase + rnd(900.0, 1500.0);
      if (val > 4800.0) val = 4800.0; // Tetap < 5000 agar masuk F_OUTLIER
    }
    simAnomalyHint = "OUTLIER_CO2";
    Serial.println("[SIM] OUTLIER CO2");
    cooldownOutlierCO2 = 15; // Cooldown 15 slot (30 menit) agar tidak beruntun
  }
  // Di luar rentang (F5): Peluang < 0.5%
  else if (cooldownRangeCO2 == 0 && random(1000) < 5) {
    val = 6000.0;
    simAnomalyHint = "RANGE_CO2";
    Serial.println("[SIM] RANGE CO2");
    cooldownRangeCO2 = 30;
  }

  // Injeksi manual (prioritas tertinggi)
  if (!isnan(injCO2)) { val = injCO2; injCO2 = NAN; simAnomalyHint = "INJ_CO2"; }
  return val;
}

// ===== PM2.5 =====
float simPM25(int hour, int minute) {
  float timeFactor = hour + minute / 60.0;
  float targetBase;

  if (currentMode == OUTDOOR_MODE) {
    // Outdoor: Gaussian, puncak jam sibuk pagi (06-09) & sore (17-20)
    float m = exp(-pow(timeFactor - 7.5, 2) / 2.0);
    float e = exp(-pow(timeFactor - 18.5, 2) / 2.0);
    targetBase = 25.0 + 55.0 * (m + e);
  } else {
    // Indoor: Lebih halus, 0.4-0.8x outdoor
    float m = exp(-pow(timeFactor - 7.5, 2) / 4.0);
    float e = exp(-pow(timeFactor - 18.5, 2) / 4.0);
    targetBase = 15.0 + 20.0 * (m + e);
  }

  // Fluktuasi halus (< 5 ug/m3 per slot sesuai kontinuitas TOR)
  float val = targetBase + rnd(-1.5, 1.5);

  // Update Cooldown
  if (cooldownOutlierPM25 > 0) cooldownOutlierPM25--;
  if (cooldownRangePM25 > 0) cooldownRangePM25--;

  // Outlier: Lonjakan satu sampel (peluang 1-3%), 3-5x baseline, tanpa beruntun
  if (cooldownOutlierPM25 == 0 && cooldownRangePM25 == 0 && random(100) < 2) {
    val = targetBase * rnd(3.5, 5.0);
    if (val > 950.0) val = 950.0; // Tetap < 1000 agar masuk F_OUTLIER
    simAnomalyHint = "OUTLIER_PM25";
    Serial.println("[SIM] OUTLIER PM25");
    cooldownOutlierPM25 = 15; // Cooldown 15 slot (30 menit) agar tidak beruntun
  }
  // Di luar rentang (F5): Peluang < 0.5%
  else if (cooldownRangePM25 == 0 && random(1000) < 5) {
    val = 1500.0;
    simAnomalyHint = "RANGE_PM25";
    Serial.println("[SIM] RANGE PM25");
    cooldownRangePM25 = 30;
  }

  // Injeksi manual (prioritas tertinggi)
  if (!isnan(injPM25)) { val = injPM25; injPM25 = NAN; simAnomalyHint = "INJ_PM25"; }
  return val;
}

// ===== Antarmuka Serial =====
void checkSimSerial(String cmd) {
  cmd.trim();
  if (cmd.startsWith("ENV")) {
    String mode = cmd.substring(3); mode.trim(); mode.toLowerCase();
    if (mode == "indoor") {
      currentMode = INDOOR_MODE;
      Serial.println("[SIM] Mode INDOOR diaktifkan.");
    }
    else if (mode == "outdoor") {
      currentMode = OUTDOOR_MODE;
      Serial.println("[SIM] Mode OUTDOOR diaktifkan.");
    }
  }
  else if (cmd.startsWith("INJ")) {
    int sp = cmd.indexOf(' ', 4);
    if (sp > 0) {
      String param = cmd.substring(4, sp); param.trim(); param.toLowerCase();
      float val = cmd.substring(sp + 1).toFloat();
      if (param == "co2") injCO2 = val;
      else if (param == "pm25") injPM25 = val;
      else if (param == "temp") injTemp = val;
      else if (param == "rh") injRH = val;
      Serial.println("[SIM] Injeksi: " + param + " = " + String(val));
    }
  }
}
