#ifndef STORAGE_H
#define STORAGE_H

#include <SPIFFS.h>
#include <FS.h>

bool spiffsMounted = false;
const float SPIFFS_ROTATION_THRESHOLD = 0.75f; // Ubah sementara menjadi 0.05f khusus uji T8.
const char *MINUTE_HEADER = "created_at,min_of_day,temp,rh,co2,pm25,flag_temp,flag_rh,flag_co2,flag_pm25,truth_co2,truth_pm25";
const char *HOURLY_HEADER = "created_at,temp,rh,co2,pm25,count_temp,count_rh,count_co2,count_pm25,avg24_temp,avg24_rh,avg24_co2,avg24_pm25";
// Antrean kirim ulang (bonus): satu body JSON per baris. Nama sengaja tidak berpola /m|/h + tanggal.
const char *QUEUE_TOR = "/q_tor.txt";
const char *QUEUE_LOCAL = "/q_loc.txt";
const size_t QUEUE_MAX_BYTES = 200000;

// Mount SPIFFS dan format bila belum pernah diformat; output true bila storage siap.
bool initStorage() {
  spiffsMounted = SPIFFS.begin(true);
  if (spiffsMounted) Serial.printf("[SPIFFS] mounted used=%u/%u bytes\n", SPIFFS.usedBytes(), SPIFFS.totalBytes());
  else Serial.println("[SPIFFS] mount gagal; sistem tetap berjalan");
  return spiffsMounted;
}

// Menambah satu baris CSV dan header bila file baru/kosong; input nama, header, baris; output sukses.
bool appendCsv(const String &filename, const String &header, const String &row) {
  if (!spiffsMounted) return false;
  bool needsHeader = !SPIFFS.exists(filename);
  if (!needsHeader) {
    File existing = SPIFFS.open(filename, FILE_READ);
    needsHeader = !existing || existing.size() == 0;
    if (existing) existing.close();
  }
  File file = SPIFFS.open(filename, FILE_APPEND);
  if (!file) { Serial.println("[SPIFFS] gagal membuka " + filename); return false; }
  if (needsHeader) file.println(header);
  bool ok = file.println(row) > 0;
  file.close();
  if (!ok) Serial.println("[SPIFFS] gagal menulis " + filename);
  return ok;
}

// Mengambil dayKey dari nama /mYYYYMMDD.csv atau /hYYYYMMDD.csv; output "" bila bukan file data.
String dataFileKey(const String &name) {
  int slash = name.lastIndexOf('/');
  int start = slash + 1;
  if ((int)name.length() < start + 13) return "";
  char prefix = name.charAt(start);
  if (prefix != 'm' && prefix != 'h') return "";
  String key = name.substring(start + 1, start + 9);
  for (int i = 0; i < 8; ++i) if (!isDigit(key.charAt(i))) return "";
  if (!name.endsWith(".csv")) return "";
  return key;
}

// Bila pemakaian > threshold, hapus satu file data tertua. File hari ini dan antrean tidak pernah dihapus.
void checkStorageRotation() {
  if (!spiffsMounted || SPIFFS.totalBytes() == 0) return;
  float usage = (float)SPIFFS.usedBytes() / SPIFFS.totalBytes();
  if (usage <= SPIFFS_ROTATION_THRESHOLD) return;
  String today = getDayKey();
  File root = SPIFFS.open("/");
  if (!root) return;
  String oldest = "", oldestKey = "";
  File file = root.openNextFile();
  while (file) {
    String name = String(file.name());
    String key = dataFileKey(name);
    if (key.length() && key < today &&
        (!oldest.length() || key < oldestKey || (key == oldestKey && name < oldest))) {
      oldest = name; oldestKey = key;
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  Serial.printf("[SPIFFS] usage=%.1f%% > %.0f%%\n", usage * 100.0f, SPIFFS_ROTATION_THRESHOLD * 100.0f);
  if (!oldest.length()) { Serial.println("[SPIFFS] rotasi ditunda: hanya ada file hari ini"); return; }
  if (!oldest.startsWith("/")) oldest = "/" + oldest;
  if (SPIFFS.remove(oldest)) Serial.println("[SPIFFS] file terlama dihapus: " + oldest);
  else Serial.println("[SPIFFS] gagal menghapus: " + oldest);
}

// Menambah satu body JSON ke antrean kirim ulang; input nama antrean dan body; output sukses.
bool queuePush(const char *queue, const String &body) {
  if (!spiffsMounted) return false;
  File existing = SPIFFS.open(queue, FILE_READ);
  size_t size = existing ? existing.size() : 0;
  if (existing) existing.close();
  if (size > QUEUE_MAX_BYTES) { Serial.println("[QUEUE] penuh; record hanya ada di CSV"); return false; }
  File file = SPIFFS.open(queue, FILE_APPEND);
  if (!file) return false;
  bool ok = file.println(body) > 0;
  file.close();
  return ok;
}

// Menghitung baris antrean; input nama antrean; output jumlah record tertunda.
int queueCount(const char *queue) {
  if (!spiffsMounted || !SPIFFS.exists(queue)) return 0;
  File file = SPIFFS.open(queue, FILE_READ);
  int lines = 0;
  while (file && file.available()) if (file.read() == '\n') lines++;
  if (file) file.close();
  return lines;
}

// Mencetak daftar file SPIFFS beserta ukurannya (perintah LS); tanpa input/output.
void handleListCommand() {
  if (!spiffsMounted) { Serial.println("[SPIFFS] belum mounted"); return; }
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  Serial.println("[LS] BEGIN");
  while (file) {
    Serial.printf("%s\t%u\n", file.name(), (unsigned)file.size());
    file.close();
    file = root.openNextFile();
  }
  root.close();
  Serial.printf("[LS] END used=%u/%u bytes\n", SPIFFS.usedBytes(), SPIFFS.totalBytes());
}

// Mencetak seluruh isi file dari perintah DUMP <filename>; input baris perintah.
void handleDumpCommand(String command) {
  String filename = command.substring(4); filename.trim();
  if (!filename.length()) { Serial.println("[CMD] Gunakan DUMP <filename>"); return; }
  if (!filename.startsWith("/")) filename = "/" + filename;
  if (!spiffsMounted) { Serial.println("[SPIFFS] belum mounted"); return; }
  File file = SPIFFS.open(filename, FILE_READ);
  if (!file) { Serial.println("[SPIFFS] file tidak ditemukan: " + filename); return; }
  Serial.println("[DUMP] BEGIN " + filename);
  while (file.available()) Serial.write(file.read());
  file.close();
  Serial.println("\n[DUMP] END");
}

#endif
