# Panduan Presentasi: Refaktorisasi HunStat2 / AD5941_25

Dokumen ini disusun sebagai panduan berbicara (speaker notes) untuk memandu Anda mempresentasikan hasil pekerjaan pengembangan firmware dan UI HunStat2. 

Panduan ini mencocokkan setiap slide yang dihasilkan oleh skrip `generate_ppt_report.py` dengan narasi bicara yang direkomendasikan.

---

## Slide 1: Judul
* **Teks Slide**: Refaktorisasi HunStat2 / AD5941_25 — Laporan implementasi, debugging, dan kesiapan presentasi.
* **Naskah Bicara**:
  > *"Halo semuanya, hari ini saya ingin mempresentasikan perkembangan terbaru mengenai proyek HunStat2. Fokus utama dari pekerjaan ini adalah melakukan refaktorisasi pada firmware berbasis chip AD5941, meningkatkan modularitas sistem, menyelesaikan isu-isu debugging data plot, serta membangun alat uji cepat berbasis Python UI."*

---

## Slide 2: Latar Belakang
* **Teks Slide**:
  * Kode awal cenderung monolitik dan sulit dipelihara.
  * Debugging data plot kosong memakan waktu.
  * Diperlukan struktur modular dan alat uji cepat.
* **Naskah Bicara**:
  > *"Mari kita mulai dengan latar belakang mengapa pekerjaan ini penting. Sebelum refaktorisasi, firmware HunStat2 ditulis dalam bentuk monolitik. Artinya, semua logika dicampur di satu file utama, membuat pemeliharaan dan pelacakan bug sangat sulit. Selain itu, ada masalah berkala di mana plot grafik di UI kosong, yang memerlukan investigasi mendalam apakah masalahnya ada di software UI, parser serial MCU, atau di level kelistrikan perangkat keras."*

---

## Slide 3: Tujuan Pekerjaan
* **Teks Slide**:
  * Memodularisasi firmware agar mirip pola FreiStat.
  * Memisahkan setup, komunikasi, storage, dan metode.
  * Menyediakan UI test Python untuk board dan dummy.
  * Membuat jalur diagnosis register AD5941.
* **Naskah Bicara**:
  > *"Berdasarkan latar belakang tersebut, kami menetapkan empat tujuan utama pekerjaan ini. Pertama, menata ulang arsitektur firmware agar setara dengan struktur modular FreiStat yang terbukti stabil. Kedua, membagi fungsi-fungsi utama menjadi modul terpisah: inisialisasi, komunikasi serial, penyimpanan parameter, dan metode elektrokimia. Ketiga, membuat UI simulasi Python dengan mode dummy data agar visualisasi grafik dapat diuji tanpa harus mencolokkan perangkat keras asli. Dan keempat, menyediakan fungsi khusus untuk membaca register chip AD5941 secara langsung."*

---

## Slide 4: Arsitektur Baru Firmware
* **Teks Slide**:
  * Main sketch menjadi orkestrator alur.
  * Layer setup untuk init/reset/kalibrasi AD5941.
  * Layer communication untuk parse dan dispatch command.
  * Layer electrochemical_methods per metode (OCP/EIS/CA/SWV/DPV/CV).
  * Layer interface/status untuk LED dan indikator.
* **Naskah Bicara**:
  > *"Berikut adalah arsitektur firmware yang baru setelah direfaktor. File utama (main sketch) sekarang hanya bertindak sebagai orkestrator atau konduktor alur program. Logika fungsional dipindah ke submodul di dalam folder `src/`. Kami memiliki modul `setup` untuk menyalakan dan mengkalibrasi chip, modul `communication` untuk membaca dan memisahkan string Serial, modul `data_storage` untuk menyimpan parameter uji, modul `electrochemical_methods` untuk implementasi algoritma tes kimia seperti OCP, EIS, CA, SWV, DPV, dan CV, serta modul `interface` untuk mengubah warna LED visual."*

---

## Slide 5: Manfaat Refaktorisasi
* **Teks Slide**:
  * Kode lebih mudah dibaca dan diuji.
  * Risiko perubahan silang antar fitur berkurang.
  * Penambahan fitur baru lebih aman.
  * Troubleshooting lebih terarah.
* **Naskah Bicara**:
  > *"Dengan arsitektur baru ini, ada beberapa manfaat teknis yang langsung kita rasakan. Kode program menjadi jauh lebih mudah dibaca dan dipahami oleh pengembang lain. Ketika kita ingin menambahkan atau memodifikasi satu metode elektrokimia, misalnya Chronoamperometry, perubahan tersebut terisolasi di file metodenya sendiri sehingga tidak ada risiko merusak metode lain seperti EIS. Hal ini membuat pengembangan jangka panjang menjadi jauh lebih aman dan cepat."*

---

## Slide 6: UI Python Test Console
* **Teks Slide**:
  * Koneksi serial, kirim command, monitor log.
  * Atur parameter metode dari UI.
  * Plot data real-time per mode.
  * Export data ke CSV untuk analisis lanjutan.
* **Naskah Bicara**:
  > *"Untuk mempercepat siklus pengujian, kami mengembangkan alat bantu berupa UI Python Test Console. UI ini memungkinkan pengguna melakukan koneksi serial secara instan, mengirim perintah biner maupun ASCII secara manual, memantau log aktivitas, serta mengatur parameter eksperimen langsung dari tombol input. Data yang masuk dari perangkat keras akan diplot secara real-time dan hasilnya bisa langsung diekspor ke file CSV untuk dianalisis lebih lanjut di Excel atau MATLAB."*

---

## Slide 7: Mode Data: BOARD vs DUMMY
* **Teks Slide**:
  * BOARD: command dikirim ke perangkat nyata.
  * DUMMY: data sintetis dibuat di UI untuk validasi alur.
  * Memudahkan demo tanpa ketergantungan hardware.
  * Mendukung uji parser dan plotting secara cepat.
* **Naskah Bicara**:
  > *"Salah satu fitur paling berguna di UI test ini adalah pembagian mode sumber data: BOARD dan DUMMY. Mode BOARD digunakan saat kita ingin mengambil data aktual dari sel elektrokimia asli. Sementara mode DUMMY menghasilkan data sintetis bermodel matematika langsung di level komputer. Ini sangat krusial saat kita ingin melakukan demo software saat bepergian tanpa perangkat keras fisik, atau saat ingin memastikan fungsi plotting grafik bekerja dengan benar tanpa gangguan noise sirkuit."*

---

## Slide 8: Investigasi Plot Kosong
* **Teks Slide**:
  * UI hanya memplot data yang formatnya parseable.
  * Sebagian output firmware tidak selalu sesuai parser.
  * Solusi: perluasan parser + dummy mode untuk isolasi masalah.
* **Naskah Bicara**:
  > *"Selama sesi debugging, kami sempat menyelidiki mengapa data plot terkadang kosong. Hasil investigasi menunjukkan bahwa UI Python hanya memplot baris data yang berhasil dikenali oleh parser-nya. Jika firmware mengirimkan baris teks di luar format koordinat yang baku (misalnya echo command atau pesan log internal), parser UI akan mengabaikannya sehingga grafiknya terlihat kosong. Solusinya, kami memperluas parser di UI agar lebih fleksibel membaca string keluaran firmware, serta menggunakan mode dummy untuk memastikan fungsionalitas rendering grafik di komputer berjalan dengan stabil."*

---

## Slide 9: Investigasi Register AD5941
* **Teks Slide**:
  * Dibuat test register terpisah (Python + Arduino sketch).
  * Diverifikasi jalur command baca register tersedia.
  * CHIPID invalid (0x0000/0xFFFF) mengarah ke isu hardware/SPI.
  * Kesimpulan: bedakan masalah software vs wiring/power.
* **Naskah Bicara**:
  > *"Masalah lain yang sering terjadi adalah kegagalan inisialisasi chip. Untuk itu, kami menambahkan fitur pembacaan register langsung. Kami memverifikasi bahwa jika kita membaca register CHIPID dan hasilnya adalah 0x0000 atau 0xFFFF, itu adalah indikasi kuat adanya masalah fisik di perangkat keras—seperti kabel SPI yang longgar, pin reset yang tidak terhubung, atau daya chip yang drop—bukan kesalahan software parser. Ini mempermudah tim hardware melakukan troubleshooting."*

---

## Slide 10: Status Dummy Metode
* **Teks Slide**:
  * EIS/CV/CA/SWV/DPV/OCP tersedia untuk pengujian UI.
  * Profil dummy bisa disetel sesuai kebutuhan demo.
  * SWV saat ini mengikuti preferensi presentasi pengguna.
* **Naskah Bicara**:
  > *"Saat ini, semua metode elektrokimia utama telah didukung oleh generator data dummy di UI, mulai dari EIS, CV, CA, SWV, DPV, hingga OCP. Khusus untuk Square Wave Voltammetry (SWV), data dummy-nya disetel ke profil garis lurus linear sederhana sesuai dengan masukan terakhir dari tim pengujian untuk keperluan demo tertentu. Tentu saja, untuk pengujian ilmiah lanjutan, pengguna disarankan menghubungkannya ke board nyata."*

---

## Slide 11: Hasil dan Dampak
* **Teks Slide**:
  * Maintainability meningkat signifikan.
  * Debug loop lebih cepat (serial + plot dalam satu UI).
  * Diagnosis AD5941 lebih sistematis.
  * Fondasi proyek lebih siap untuk pengembangan berikutnya.
* **Naskah Bicara**:
  > *"Sebagai kesimpulan dari pekerjaan ini, refaktorisasi telah meningkatkan keterbacaan dan struktur kode secara signifikan. Siklus debug kami terpangkas dari menit menjadi hitungan detik karena kita dapat memantau serial data dan grafik secara bersamaan. Jalur diagnosis kesalahan hardware menjadi jauh lebih sistematis. Sekarang, HunStat2 berada di posisi fondasi software yang kokoh untuk pengembangan produk komersial berikutnya."*

---

## Slide 12: Rekomendasi Lanjutan
* **Teks Slide**:
  * Tambah profil dummy realistis (linear/realistic/noisy).
  * Standarisasi format output serial lintas metode.
  * Tambah metadata eksperimen dan timestamp di CSV.
  * Tambahkan test parser otomatis berbasis sample log.
* **Naskah Bicara**:
  > *"Untuk langkah pengembangan berikutnya, kami merekomendasikan beberapa hal: Pertama, menambahkan variasi profil dummy (seperti profil realistik dengan noise buatan). Kedua, menyamakan format output serial untuk seluruh metode agar pembacaan parser lebih seragam. Dan ketiga, menambahkan timestamp serta parameter uji ke dalam ekspor file CSV agar pendataan hasil laboratorium lebih rapi."*

---

## Slide 13: Penutup
* **Teks Slide**:
  * Refaktorisasi berhasil memperkuat fondasi software.
  * UI test membantu validasi dan presentasi teknis.
  * Langkah selanjutnya: hardening build dan validasi board end-to-end.
* **Naskah Bicara**:
  > *"Secara keseluruhan, refaktorisasi ini telah berhasil memperkuat infrastruktur software HunStat2. Langkah selanjutnya adalah melakukan pengujian integrasi fisik secara mendalam (end-to-end board validation) untuk menyelaraskan kalibrasi biner firmware dengan pembacaan instrumen kimia standar. Terima kasih atas perhatiannya, saya persilakan jika ada pertanyaan."*
