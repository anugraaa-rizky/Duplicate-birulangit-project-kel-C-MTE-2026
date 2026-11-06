// =====================================================================
//  Storage.h - FINAL (LIST, DUMP, CLEAR, FIX, Rotasi)
// =====================================================================
#include <SPIFFS.h>
#include <FS.h>

bool spiffsMounted = false;

void appendCSV(String filename, String content) {
  if (!spiffsMounted) return;
  File f = SPIFFS.open(filename, FILE_APPEND);
  if (f) { f.println(content); f.close(); }
}

// Memperbaiki otomatis jika baris 23:00 hari sebelumnya tersimpan di /h20261031.csv
void fixMisplaced23Hour() {
  if (!spiffsMounted) return;
  if (!SPIFFS.exists("/h20261031.csv")) return;

  File f31 = SPIFFS.open("/h20261031.csv", FILE_READ);
  if (!f31) return;

  String firstLine = f31.readStringUntil('\n');
  firstLine.trim();

  // Jika baris pertama adalah jam 23:00 hari sebelumnya (2026-10-30)
  if (firstLine.startsWith("2026-10-30 23:00:00")) {
    Serial.println("[STORAGE] Memperbaiki file CSV...");
    Serial.println("[STORAGE] Menambahkan baris 23:00 ke /h20261030.csv...");
    appendCSV("/h20261030.csv", firstLine);

    String remaining = "";
    while (f31.available()) {
      String l = f31.readStringUntil('\n');
      l.trim();
      if (l.length() > 0) remaining += l + "\n";
    }
    f31.close();

    SPIFFS.remove("/h20261031.csv");
    if (remaining.length() > 0) {
      File fw = SPIFFS.open("/h20261031.csv", FILE_WRITE);
      if (fw) { fw.print(remaining); fw.close(); }
    }
    Serial.println("[STORAGE] Perbaikan berhasil! /h20261030.csv lengkap 24 jam dan /h20261031.csv bersih.");
  } else {
    f31.close();
  }
}

bool initStorage() {
  if (!SPIFFS.begin(true)) { Serial.println("[SPIFFS] Gagal mount!"); return false; }
  spiffsMounted = true;
  Serial.println("[SPIFFS] Berhasil mount.");
  fixMisplaced23Hour();
  return true;
}

void checkStorageRotation() {
  if (!spiffsMounted) return;
  size_t total = SPIFFS.totalBytes(), used = SPIFFS.usedBytes();
  if (total > 0 && used > (total * 0.75)) {
    Serial.println("[SPIFFS] Rotasi > 75%...");
    File root = SPIFFS.open("/"); String oldest = "";
    File file = root.openNextFile();
    while (file) {
      String fname = String(file.name());
      if (fname.endsWith(".csv") && (oldest == "" || fname < oldest)) oldest = fname;
      file = root.openNextFile();
    }
    if (oldest != "") { Serial.println("[SPIFFS] Hapus: " + oldest); SPIFFS.remove("/" + oldest); }
  }
}

void handleDumpCommand(String cmd) {
  if (!cmd.startsWith("DUMP")) return;
  String filename = cmd.substring(5); filename.trim();
  if (filename == "") { Serial.println("Usage: DUMP <file>"); return; }
  if (!filename.startsWith("/")) filename = "/" + filename;
  File f = SPIFFS.open(filename, FILE_READ);
  if (!f) { Serial.println("File not found: " + filename); return; }
  Serial.println();
  Serial.println("--- MULAI: " + filename + " ---");
  while (f.available()) Serial.write(f.read());
  Serial.println("--- SELESAI: " + filename + " ---");
  Serial.println();
  f.close();
}

void printFileList() {
  Serial.println("\n=== LIST SPIFFS ===");
  File root = SPIFFS.open("/"); File file = root.openNextFile();
  while (file) { Serial.printf("%s \t %u bytes\n", file.name(), file.size()); file = root.openNextFile(); }
  Serial.printf("Used: %u / %u bytes\n", (unsigned)SPIFFS.usedBytes(), (unsigned)SPIFFS.totalBytes());
  Serial.println("=================\n");
}

void clearStorage() {
  if (!spiffsMounted) return;
  Serial.println("[SPIFFS] CLEAR...");
  SPIFFS.format(); SPIFFS.end();
  spiffsMounted = SPIFFS.begin(true);
  Serial.printf("[SPIFFS] CLEAR Done. Used: %u bytes\n", (unsigned)SPIFFS.usedBytes());
}
