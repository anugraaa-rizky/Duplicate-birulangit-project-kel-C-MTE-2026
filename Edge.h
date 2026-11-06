// =====================================================================
//  Edge.h - FINAL (Strict IQR Window + Anti Lockup / Flag Tertahan)
// =====================================================================
#include <Arduino.h>

#define FLAG_VALID   0
#define FLAG_SENSOR  1
#define FLAG_RANGE   2
#define FLAG_OUTLIER 4

struct IQRWindow {
  float vals[30];
  int count;
  int head;
  int outlierStreak; // Pencatat streak outlier berturut-turut (anti lockup)
};
IQRWindow winTemp, winHumid, winCO2, winPM25;

void initIQRWindows() {
  for(int i=0; i<30; i++) {
    winTemp.vals[i]=0; winHumid.vals[i]=0; winCO2.vals[i]=0; winPM25.vals[i]=0;
  }
  winTemp.count=0; winTemp.head=0; winTemp.outlierStreak=0;
  winHumid.count=0; winHumid.head=0; winHumid.outlierStreak=0;
  winCO2.count=0; winCO2.head=0; winCO2.outlierStreak=0;
  winPM25.count=0; winPM25.head=0; winPM25.outlierStreak=0;
}

void addToWindow(IQRWindow& win, float val) {
  if (win.count < 30) {
    win.vals[win.count++] = val;
  } else {
    win.vals[win.head] = val;
    win.head = (win.head + 1) % 30;
  }
}

int validateParam(float val, float minV, float maxV, IQRWindow& win, float minIQR) {
  if (isnan(val)) {
    win.outlierStreak = 0;
    return FLAG_SENSOR;
  }
  if (val < minV || val > maxV) {
    win.outlierStreak = 0;
    return FLAG_RANGE;
  }

  int flag = FLAG_VALID;
  if (win.count >= 10) {
    float sorted[30];
    for (int i = 0; i < win.count; i++) sorted[i] = win.vals[i];
    for (int i = 0; i < win.count - 1; i++) {
      for (int j = 0; j < win.count - i - 1; j++) {
        if (sorted[j] > sorted[j + 1]) {
          float temp = sorted[j]; sorted[j] = sorted[j + 1]; sorted[j + 1] = temp;
        }
      }
    }
    int n = win.count;
    float q1 = sorted[n / 4];
    float q3 = sorted[(3 * n) / 4];
    float iqr = max(q3 - q1, minIQR);
    if (val < (q1 - 1.5 * iqr) || val > (q3 + 1.5 * iqr)) {
      flag = FLAG_OUTLIER;
    }
  }

  // Penanganan Jendela & Anti Flag Tertahan (Anti-Lockup):
  if (flag == FLAG_OUTLIER) {
    win.outlierStreak++;
    // Menurut TOR, outlier adalah lonjakan 1 sampel.
    // Jika 3 sampel berturut-turut berada di luar batas (misal karena perpindahan mode indoor/outdoor
    // atau kenaikan alami lingkungan), ini BUKAN outlier acak, melainkan pergeseran level nyata.
    // Kita adaptasikan jendela ke level baru agar flag tidak tertahan selamanya.
    if (win.outlierStreak >= 3) {
      win.count = 0;
      win.head = 0;
      addToWindow(win, val);
      win.outlierStreak = 0;
      flag = FLAG_VALID; // Adaptasi berhasil, kembali valid
    }
  } else {
    win.outlierStreak = 0;
    addToWindow(win, val);
  }

  return flag;
}

float sumTemp=0, sumHumid=0, sumCO2=0, sumPM25=0;
int countValid=0, lastHourAvg=-1;

void resetHourlyAvg() {
  sumTemp=0; sumHumid=0; sumCO2=0; sumPM25=0; countValid=0;
}

void updateHourlyAvg(int currentHour, int fT, int fH, int fC, int fP, float t, float h, float c, float p) {
  if (lastHourAvg == -1) lastHourAvg = currentHour;
  if (currentHour != lastHourAvg) {
    lastHourAvg = currentHour;
    resetHourlyAvg();
  }
  // Rata-rata jam hanya dari data berflag 0
  if (fT == 0 && fH == 0 && fC == 0 && fP == 0) {
    sumTemp += t;
    sumHumid += h;
    sumCO2 += c;
    sumPM25 += p;
    countValid++;
  }
}

void printIQRStats(IQRWindow& win, const char* name, float minIQR) {
  if (win.count < 10) { Serial.printf("[%s] Data < 10\n", name); return; }
  float sorted[30];
  for (int i = 0; i < win.count; i++) sorted[i] = win.vals[i];
  for (int i = 0; i < win.count - 1; i++) {
    for (int j = 0; j < win.count - i - 1; j++) {
      if (sorted[j] > sorted[j + 1]) { float temp = sorted[j]; sorted[j] = sorted[j + 1]; sorted[j + 1] = temp; }
    }
  }
  int n = win.count;
  float q1 = sorted[n / 4], q3 = sorted[(3 * n) / 4];
  float iqr = max(q3 - q1, minIQR);
  Serial.printf("[%s] n=%d | Q1=%.2f | Q3=%.2f | IQR=%.2f | Pagar=[%.2f, %.2f] | streak=%d\n",
                name, n, q1, q3, iqr, q1-1.5*iqr, q3+1.5*iqr, win.outlierStreak);
}

void printBoxplot() {
  Serial.println("\n=== BOXPLOT ===");
  printIQRStats(winTemp, "Suhu", 0.5); printIQRStats(winHumid, "RH", 2.5);
  printIQRStats(winCO2, "CO2", 10.0); printIQRStats(winPM25, "PM25", 2.0);
  Serial.println("=============\n");
}
