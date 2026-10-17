# Duplicate BiruLangit Project · Kelompok C · MTE 2026

Sistem pemantau kualitas udara (AQMS) ujung ke ujung untuk Tubes P3-P4 mata kuliah IoT Cerdas:
firmware **ESP32**, backend **FastAPI + SQLite** dengan **prakiraan Machine Learning**, dan dashboard
**Next.js** bertema **langitbiru**.

```
 ESP32 (SHT31 + simulator CO₂/PM2.5)
   │  edge: validasi rentang, outlier IQR, rata-rata jam, SPIFFS
   ├──HTTPS──► Server RnD BiruLangit (wajib TOR: 2m_raw, 2m_validated, 1h_valid)
   └──HTTP───► FastAPI (laptop) ──► SQLite (aq.db) ──► Next.js dashboard (CSR + polling)
                    └── model prakiraan ML per jam
```

> **Tentang data.** Tanpa perangkat, semua data di dashboard berasal dari `backend/simulator.py`
> (ESP32 virtual, termasuk suhu/RH buatan). Bila ESP32 asli dihubungkan, suhu & RH berasal dari
> SHT31 fisik, sedangkan CO₂ & PM2.5 tetap disimulasikan di ESP32 sesuai ketentuan TOR.

---

## Daftar isi

1. [Struktur repositori](#1-struktur-repositori)
2. [Cara menjalankan](#2-cara-menjalankan)
3. [Menghubungkan ESP32](#3-menghubungkan-esp32)
4. [Prakiraan (forecasting): faktor yang digunakan](#4-prakiraan-forecasting-faktor-yang-digunakan)
5. [API backend](#5-api-backend)
6. [Dashboard langitbiru](#6-dashboard-langitbiru)
7. [Firmware: perbaikan terhadap TOR](#7-firmware-perbaikan-terhadap-tor)

---

## 1. Struktur repositori

```
├─ IoT_AQMS.ino      Konfigurasi, global, akuisisi SHT31, setup(), loop() dengan scheduler slot
├─ Date_Time.h       RTC, sinkron waktu dari server, indeks waktu (dayKey, jam, menit, minOfDay)
├─ Simulasi.h        Simulator CO₂ & PM2.5 (indoor/outdoor), outlier & out-of-range + kunci jawaban
├─ Edge.h            Flag kualitas, validasi rentang, outlier IQR, rata-rata jam & 24 jam
├─ Storage.h         SPIFFS: CSV harian, rotasi file, antrean kirim ulang, perintah DUMP/LS
├─ Send.h            Penyusun JSON TOR, satu fungsi kirim, kirim ke FastAPI lokal, kirim ulang
├─ backend/
│  ├─ app/main.py        Endpoint REST (FastAPI)
│  ├─ app/db.py          Skema & akses SQLite
│  ├─ app/forecast.py    Model prakiraan ML (ridge regression autoregresif, numpy)
│  ├─ simulator.py       ESP32 virtual untuk uji tanpa perangkat
│  ├─ tests/test_api.py  Tes otomatis (pytest)
│  └─ requirements.txt
├─ frontend/             Dashboard Next.js (App Router, TypeScript)
│  ├─ app/page.tsx       Halaman utama + switch tampilan
│  ├─ app/globals.css    Tema langitbiru (mode siang & malam)
│  ├─ app/components/    Kartu nilai, grafik, prakiraan, tabel
│  └─ app/lib/           Klien API, kategori kualitas udara, hook polling
└─ run_all.ps1           Menyalakan backend + frontend sekaligus (Windows)
```

---

## 2. Cara menjalankan

### Prasyarat

| Perangkat lunak | Versi | Cek |
|---|---|---|
| Python | 3.10 atau lebih baru | `python --version` |
| Node.js | 20 atau lebih baru | `node -v` |
| Git | apa saja | `git --version` |

### Langkah 1: unduh dan pasang dependensi (sekali saja)

```bash
git clone https://github.com/anugraaa-rizky/Duplicate-birulangit-project-kel-C-MTE-2026.git
cd Duplicate-birulangit-project-kel-C-MTE-2026

cd backend
python -m pip install -r requirements.txt
cd ../frontend
npm install
cp .env.example .env.local      # Windows PowerShell: Copy-Item .env.example .env.local
cd ..
```

### Langkah 2a: jalankan semua sekaligus (Windows)

```powershell
./run_all.ps1          # backend + frontend, lalu browser terbuka otomatis
./run_all.ps1 -Live    # ditambah simulator yang mengirim satu slot tiap 5 detik
```

Bila database masih kosong, skrip otomatis mengisi 3 hari data simulasi. Jika muncul pesan
*"running scripts is disabled"*, jalankan sekali: `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`.

### Langkah 2b: jalankan manual (Windows / macOS / Linux)

```bash
# Terminal 1: backend
cd backend
python -m uvicorn app.main:app --host 0.0.0.0 --port 8000

# Terminal 2: isi data contoh (sekali), lalu opsional kirim data terus-menerus
cd backend
python simulator.py --seed-days 3 --tor
python simulator.py --live --interval 5

# Terminal 3: frontend
cd frontend
npm run dev
```

| Alamat | Isi |
|---|---|
| http://localhost:3000 | Dashboard langitbiru |
| http://localhost:8000/docs | Dokumentasi API interaktif (Swagger), bisa uji POST langsung |

### Opsi simulator

```bash
python simulator.py --seed-days 3            # isi histori 3 hari (720 slot/hari)
python simulator.py --live                   # real time: satu slot tiap 120 detik
python simulator.py --live --interval 5      # demo cepat: waktu maju 2 menit tiap 5 detik
python simulator.py --live --mode indoor     # pola indoor (CO₂ naik saat jam kerja)
python simulator.py --seed-days 1 --tor      # juga kirim paket format TOR ke mirror lokal
```

### Tes otomatis

```bash
cd backend
python -m pytest -q        # 7 tes: idempoten, validasi 422, rata-rata jam, forecast, mirror TOR
```

### Menghapus data dummy

Hentikan backend, lalu hapus `backend/aq.db` (berikut `aq.db-shm` dan `aq.db-wal`). Database kosong
dibuat ulang otomatis saat backend dinyalakan.

---

## 3. Menghubungkan ESP32

### 3.1 Siapkan Arduino IDE

| Item | Nilai |
|---|---|
| Board | **ESP32 Dev Module** |
| Core | **esp32 by Espressif v1.0.6** (Board Manager URL: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`) |
| Partition Scheme | **Default 4MB with spiffs** |
| Library | **ESP32Time** (fbiego), **Adafruit SHT31 Library** ≥ 2.0 (Adafruit BusIO ikut terpasang) |
| Wiring SHT31 | VCC → 3V3, GND → GND, SDA → GPIO21, SCL → GPIO22, alamat I²C 0x44 |

Arduino IDE mewajibkan nama folder sama dengan nama file `.ino`. Buat folder **`IoT_AQMS/`** lalu
salin keenam file firmware (`IoT_AQMS.ino`, `Date_Time.h`, `Simulasi.h`, `Edge.h`, `Storage.h`, `Send.h`)
ke dalamnya.

Kode ini sudah diverifikasi compile dengan konfigurasi di atas: 72% flash, 12% RAM.

### 3.2 Isi konfigurasi di `IoT_AQMS.ino`

```cpp
const char* ssid     = "NAMA_WIFI";       // ganti dengan WiFi 2,4 GHz yang SAMA dengan laptop
const char* password = "PASSWORD_WIFI";

const bool  LOCAL_API_ENABLED = true;                          // nyalakan kiriman ke FastAPI
const char* api_local = "http://192.168.x.x:8000/api/readings"; // IP LAN laptop, BUKAN localhost
const char* local_device_id = "esp32-kelompokC";                // beda dari "aqms-01" milik simulator
```

Cara mengetahui IP laptop:
- Windows: `ipconfig` → baris **IPv4 Address**.
- macOS/Linux: `ipconfig getifaddr en0` atau `hostname -I`.

> **Jangan commit SSID dan password asli.** Kembalikan ke `NAMA_WIFI`/`PASSWORD_WIFI` sebelum push
> atau sebelum mengumpulkan tugas.

### 3.3 Nyalakan backend agar bisa diakses dari jaringan

```bash
cd backend
python -m uvicorn app.main:app --host 0.0.0.0 --port 8000
```

- `--host 0.0.0.0` wajib. Tanpa opsi ini backend hanya menerima koneksi dari laptop sendiri.
- Saat pertama kali, Windows Firewall akan bertanya: pilih **Allow** untuk jaringan Private.
- Uji dari HP yang terhubung ke WiFi yang sama: buka `http://<IP-laptop>:8000/api/health`.

### 3.4 Upload dan verifikasi

1. Upload firmware, buka Serial Monitor 115200 baud.
2. Saat boot terlihat `[WiFi] connected`, `[RTC] HTTP=200`, dan `[RTC] RTT=… status=OK`.
3. Tiap menit genap: `[SLOT] …`, `[SEND] 2m_raw HTTP=200`, `[SEND] 2m_validated HTTP=200`, dan
   `[SEND] local HTTP=201`.
4. Jendela backend menampilkan `POST /api/readings 201` dari IP ESP32.
5. Di dashboard, pilih `esp32-kelompokC` pada dropdown kanan atas.

Jangan jalankan `simulator.py` bersamaan bila ingin data asli tidak tercampur data dummy.

### 3.5 Yang dikirim ESP32

**Ke server RnD BiruLangit (TOR):** tiga jenis data, semua nilai berupa string.

```json
{ "device_1": "2m_validated-Kelompok_C", "RSSI_1": "-61", "created_at_1": "2026-10-03 14:36:00",
  "Uptime_1": "02:14:07", "SD_status_1": "1", "PM25_status_1": "0", "SHT_status_1": "1",
  "CO2v_1": "612", "PM25v_1": "NULL", "Tempinv_1": "27.81", "Humidinv_1": "71.20" }
```

**Ke FastAPI lokal (kontrak slide P3):** satu paket mentah per slot, beserta flag.

```json
{ "device_id": "esp32-kelompokC", "ts": 1791010800, "co2": 612, "pm25": 23.5,
  "temp": 27.81, "rh": 71.20, "flags": [0, 3, 0, 0] }
```

- `ts` adalah epoch UTC awal slot.
- `flags` berurutan CO₂, PM2.5, T, RH: 0 = bersih, 1 = F_SENSOR, 2 = F_RANGE, 3 = F_OUTLIER.
- Nilai ber-flag **tetap dikirim** dan disimpan. Keputusan memakai atau membuang diambil saat analisis.

### 3.6 Ketahanan koneksi

- **WiFi putus:** record tetap tersimpan di SPIFFS (`/m<tanggal>.csv`). Kiriman yang gagal masuk antrean
  `/q_tor.txt` dan `/q_loc.txt`, lalu dikirim ulang (≤ 10 per slot) setelah online.
- **Tidak ada duplikat:** baris antrean hanya dihapus setelah server membalas 200/201. Backend memakai
  `PRIMARY KEY (device_id, ts)`, jadi kiriman ulang ke FastAPI tidak pernah membuat baris ganda.

### 3.7 Uji alur TOR sepenuhnya secara lokal (opsional)

Backend juga meniru server RnD. Arahkan sementara:

```cpp
const char* api_time = "http://192.168.x.x:8000/api/get-time";
const char* api_post = "http://192.168.x.x:8000/api/post/telkom_group";
```

Hitungan paket per hari (target T2: 720 / 720 / 24) tampil di tab **Tabel** dashboard. Endpoint ini
memakai HTTP biasa, jadi untuk uji lokal ganti juga `postJson(..., true, ...)` menjadi `false` di `sendData()`.
Kembalikan ke URL HTTPS RnD sebelum mengumpulkan.

### 3.8 Perintah Serial

| Perintah | Fungsi |
|---|---|
| `ENV indoor` / `ENV outdoor` | Ganti mode simulator |
| `INJ pm25 1500` | Ganti nilai pada slot berikutnya (`co2`, `pm25`, `temp`, `rh`), untuk uji T3 |
| `DUMP /m20261017.csv` | Cetak isi file SPIFFS |
| `LS` | Daftar file SPIFFS dan pemakaian storage |
| `STATUS` | Waktu RTC, mode, status WiFi/SPIFFS/SHT31, jumlah antrean |

---

## 4. Prakiraan (forecasting): faktor yang digunakan

Kode: [`backend/app/forecast.py`](backend/app/forecast.py). Endpoint: `GET /api/forecast`.

### 4.1 Data masukan

Model **tidak** dilatih dari data mentah 2 menit, melainkan dari **deret rata-rata per jam yang bersih**:

1. Hanya sampel dengan **flag 0** yang dipakai. Outlier, nilai di luar rentang, dan gagal baca dibuang.
2. Satu jam dianggap sah bila memiliki **≥ 23 sampel bersih dari 30** (aturan F8 TOR). Bila kurang,
   nilai jam itu kosong.
3. Jam kosong diisi dengan **interpolasi linear** antartetangga, supaya fitur lag tetap terbentuk.
4. Histori yang dipakai: hingga **7 hari terakhir** (parameter `history_hours`, maksimal 30 hari).

### 4.2 Faktor (fitur) yang digunakan

Untuk memprakirakan nilai pada jam *t*, model memakai faktor berikut **dari parameter yang sama**.
CO₂, PM2.5, suhu, dan RH masing-masing punya model sendiri.

| Faktor | Arti | Mengapa dipakai |
|---|---|---|
| `lag1` = y(t−1) | Nilai 1 jam sebelumnya | Kondisi udara berubah perlahan, jadi jam lalu adalah petunjuk terkuat |
| `lag2` = y(t−2) | Nilai 2 jam sebelumnya | Menangkap arah tren (naik/turun) |
| `lag3` = y(t−3) | Nilai 3 jam sebelumnya | Menangkap percepatan/perlambatan tren |
| `lag24` = y(t−24) | Nilai pada jam yang sama kemarin | Pola harian berulang: jam sibuk PM2.5 pagi/sore, CO₂ ruang kelas siang hari. **Hanya aktif bila histori ≥ 48 jam** |
| `hour_sin` = sin(2π·jam/24) | Posisi jam dalam sehari (WIB) | Bentuk siklus harian. Sin dan cos membuat pukul 23 dan 00 dianggap berdekatan |
| `hour_cos` = cos(2π·jam/24) | Pasangan dari `hour_sin` | Bersama `hour_sin` menentukan jam secara unik |

Nama model yang tampil di dashboard menunjukkan faktor yang aktif: `ridge-ar` (lag 1-3 + jam) atau
`ridge-ar+daily` (lag 1-3 + lag 24 + jam).

**Yang tidak dipakai:** hubungan antarparameter (misalnya RH untuk memprakirakan PM2.5), data cuaca
atau lalu lintas luar, hari dalam pekan, dan mode indoor/outdoor. Ini bisa menjadi pengembangan berikutnya.

### 4.3 Algoritma

- **Ridge regression** (regresi linear dengan regularisasi L2, α = 1,0), ditulis langsung dengan numpy:
  `w = (XᵀX + αI)⁻¹ Xᵀy`. Semua fitur distandardisasi (z-score) dulu, dan intercept tidak diregularisasi.
- **Dilatih ulang setiap permintaan**, sehingga model selalu mengikuti data terbaru tanpa file model tersimpan.
- **Prakiraan rekursif:** hasil jam ke-1 dipakai sebagai `lag1` untuk jam ke-2, dan seterusnya, sampai
  horizon 1-24 jam (default 6).
- **Batas fisik:** CO₂, PM2.5, dan RH tidak boleh negatif. Suhu boleh negatif.

### 4.4 Interval ketidakpastian 95%

```
batas = prakiraan ± 1,96 × σ_residual × √langkah
```

σ_residual adalah simpangan baku galat model pada data latih. Faktor √langkah membuat pita melebar
untuk jam yang makin jauh, karena kesalahan menumpuk pada prakiraan rekursif.

### 4.5 Evaluasi (backtest)

Setiap permintaan juga menjalankan backtest **satu langkah ke depan** pada 12 jam terakhir. Model dilatih
ulang tanpa 12 jam itu, lalu dibandingkan dengan **persistence** (tebakan "jam depan sama dengan jam ini"):

| Metrik | Arti |
|---|---|
| `mae` | Rata-rata galat absolut model |
| `mae_persistence` | Galat tebakan persistence |
| `skill` = 1 − mae/mae_persistence | > 0 berarti model lebih baik dari persistence; 1 = sempurna |

Contoh pada 3 hari data simulasi outdoor: MAE PM2.5 sekitar **1,0 µg/m³**, persistence **8,8 µg/m³**,
skill sekitar **89%**.

### 4.6 Syarat data minimum

| Histori jam bersih | Hasil |
|---|---|
| < 12 jam | `insufficient_data`, belum ada prakiraan |
| 12 sampai < 48 jam | Model `ridge-ar` (tanpa lag 24 jam) |
| ≥ 48 jam | Model `ridge-ar+daily` (dengan pola harian kemarin) |

---

## 5. API backend

| Metode | Path | Fungsi |
|---|---|---|
| POST | `/api/readings` | Terima paket ESP32, balas **201**. Kiriman ulang dengan `(device_id, ts)` sama tidak menggandakan data |
| GET | `/api/readings/latest?device_id=` | Record terbaru lengkap dengan flag |
| GET | `/api/readings/hourly?device_id=&hours=24` | Rata-rata per jam dari data flag 0, null bila < 23 sampel bersih |
| GET | `/api/readings/recent?hours=24` | Record 2 menit mentah + flag |
| GET | `/api/summary` | Kelengkapan hari ini dan jumlah flag per parameter |
| GET | `/api/devices` | Daftar perangkat yang pernah mengirim |
| GET | `/api/forecast?param=all&horizon=6` | Prakiraan ML + interval 95% + backtest |
| GET | `/api/get-time` | Mirror server RnD: `{"datetime": "YYYY-MM-DD HH:MM:SS"}` WIB |
| POST | `/api/post/{group}` | Mirror server RnD: terima payload TOR |
| GET | `/api/tor/summary` | Jumlah paket per `device_1` per hari (target T2) |
| GET | `/api/health` | Status backend dan jumlah record |

Uji cepat tanpa ESP32:

```bash
curl -X POST http://localhost:8000/api/readings -H "Content-Type: application/json" \
  -d '{"device_id":"aqms-01","ts":1791010800,"co2":612,"pm25":23.5,"temp":27.8,"rh":64.2,"flags":[0,2,0,0]}'
```

Konfigurasi lewat environment variable: `AQ_DB_PATH` (lokasi database) dan `AQ_CORS_ORIGINS`
(origin dashboard yang diizinkan, default `http://localhost:3000`).

---

## 6. Dashboard langitbiru

- **Tema:** kartu bergaris tebal, sudut membulat, dan pill gelap, dengan palet biru langit. Ada mode
  **siang** dan **malam** (tombol bulan/matahari); default mengikuti sistem.
- **Tampilan yang bisa di-switch:**

| Tab | Isi |
|---|---|
| Ringkas | Kartu nilai terkini tiap parameter. Ikon peringatan muncul bila flag ≠ 0. Ada juga kategori kualitas (PM2.5 mengikuti ISPU) dan ringkasan kelengkapan |
| Grafik | Grafik uji T9 (2m_raw, outlier, 2m_validated, 1h_valid dalam satu grafik) + grafik 24 jam per parameter |
| Prakiraan ML | Grafik aktual vs prakiraan dengan pita 95%, puncak prakiraan, dan metrik backtest |
| Tabel | Record 2 menit dengan filter "hanya ber-flag", plus hitungan kiriman format TOR |

- Data diambil di browser dan diperbarui tiap **120 detik**, sama dengan periode kirim ESP32. Tombol ⟳
  memuat ulang seketika.
- Konfigurasi di `frontend/.env.local`: `NEXT_PUBLIC_API_URL`, `NEXT_PUBLIC_DEVICE_ID`, `NEXT_PUBLIC_POLL_MS`.
- Tautan langsung ke tampilan tertentu: `http://localhost:3000/?view=prakiraan&theme=dark`.

---

## 7. Firmware: perbaikan terhadap TOR

| Modul | Masalah di kode awal | Perbaikan |
|---|---|---|
| Compile | `enum { F_OK = 0, ... }` bentrok dengan makro POSIX `F_OK` dari `unistd.h` di core ESP32, sehingga kode awal tidak bisa di-compile | Flag 0 diganti nama menjadi `F_CLEAN` |
| F6 Outlier | Nilai F_OUTLIER tidak masuk jendela, padahal TOR hanya mengecualikan F_RANGE/F_SENSOR. Setelah perubahan level, sampel bisa terus dianggap outlier. IQR minimum belum ada | Outlier ikut masuk jendela. IQR minimum = max(batas absolut, 30% × median) untuk gas. Uji simulasi: presisi PM2.5 naik dari 0,37 ke ~0,99, recall tetap 1,00 |
| F4 Indeks | `minOfDay` belum dibentuk. Slot bisa terlewat bila loop terblokir koneksi WiFi 15 s | `TimeIndex {dayKey, hour, minute, minOfDay}`. Slot dipicu pergantian `epoch/120` dan selalu dicap awal slot |
| Alur slot | Urutan simpan/kirim tidak sesuai TOR | Akuisisi → 2m_raw → validasi → SPIFFS → 2m_validated → tutup jam → 1h_valid |
| F7 SPIFFS | Rotasi bisa menghapus file hari ini. Flash baru tidak pernah diformat | Hanya file hari sebelumnya yang dihapus. `SPIFFS.begin(true)`. Perintah `LS` |
| F8 | CSV jam tanpa rata-rata 24 jam | Kolom `avg24_*` di `/h<tanggal>.csv` dan `min_of_day` di `/m<tanggal>.csv` |
| F3 | Sinkron harian hanya dicoba tepat pukul 00:01 | Retry tiap 5 menit sampai berhasil; tetap 30 s selama RTC belum sah |
| F2 Simulator | CO₂ indoor melompat 450 → 800 ppm pada 08:00; outlier bukan kelipatan nilai dasar | Kurva indoor kontinu; outlier = baseline × 3,00 sampai 5,00 |
| F1 | Suhu dan RH dibaca dengan dua pengukuran | `readBoth()` satu pengukuran, CRC gagal → NAN |
| F10 | Tiga fungsi kirim terpisah; `Uptime_1` tidak 2 digit | Satu fungsi `sendData(kind, payload)`; `Uptime_1` = `02:14:07` |
| Aturan kode | `WiFiClientSecure` dibuat global | Satu objek dibuat di `setup()` dan dipakai ulang |
| Bonus +10 | Belum ada kirim ulang | Antrean SPIFFS, kirim ulang setelah online, tanpa duplikat |
