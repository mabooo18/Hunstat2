# Flowchart Panggilan Kode (Call Flow Chart)

Diagram di bawah ini menggambarkan bagaimana modul-modul program saling berinteraksi, mulai dari file utama (**Main Orchestrator**), parser perintah serial (**Communication Layer**), hingga ke submodul **Hardware** dan **Metode Elektrokimia** di dalam folder `src/`.

---

## Diagram Alur Arsitektur (Mermaid Flowchart)

```mermaid
graph TD
    %% Styling Node Warna Premium
    classDef main fill:#1E293B,stroke:#38BDF8,stroke-width:2px,color:#FFF;
    classDef comm fill:#0F766E,stroke:#0D9488,stroke-width:2px,color:#FFF;
    classDef methods fill:#1D4ED8,stroke:#3B82F6,stroke-width:2px,color:#FFF;
    classDef hw fill:#701A75,stroke:#D946EF,stroke-width:2px,color:#FFF;
    classDef utils fill:#7C2D12,stroke:#F97316,stroke-width:2px,color:#FFF;

    %% Definisi Node
    INO["AD5941_25.ino (Loop Utama)"]:::main
    Comm["C_Communication::ReadAndProcess()<br>(src/communication/)"]:::comm
    CV_Main["cv.cpp (cvSetup & Run)"]:::main
    
    CA["c_ca.cpp<br>(C_CA::Run)"]:::methods
    SWV["c_swv.cpp<br>(C_SWV::Run)"]:::methods
    DPV["c_dpv.cpp<br>(C_DPV::Run)"]:::methods
    EIS["c_eis.cpp<br>(C_EIS::Run)"]:::methods
    OCP["c_ocp.cpp<br>(C_OCP::Measure)"]:::methods
    
    Ramp["rampTest.cpp<br>(AppRAMPInit / AppRAMPISR)"]:::methods
    
    WG["wave_gen.cpp<br>(Hardware_Do_WaveGen)"]:::hw
    ADC["adc_control.cpp<br>(init_AD5940_ADC)"]:::hw
    GC["gain_control.cpp<br>(Hardware_FindOptimum_Rf_PGA)"]:::hw
    
    Util["utilities.cpp<br>(ToFloat / Status LED)"]:::utils
    Storage["data_storage.cpp<br>(Global g_Data)"]:::utils

    %% Alur Hubungan (Call Hierarchy)
    INO -->|1. Polling Serial secara berkala| Comm
    
    %% Cabang Pengiriman Command
    Comm -->|2a. Jalankan CA| CA
    Comm -->|2b. Jalankan SWV| SWV
    Comm -->|2c. Jalankan DPV| DPV
    Comm -->|2d. Jalankan EIS| EIS
    Comm -->|2e. Jalankan OCP| OCP
    Comm -->|2f. Jalankan CV ('M')| CV_Main
    
    %% Alur CV via Sequencer
    CV_Main -->|3. Siapkan Sequencer| Ramp
    Ramp -->|4. Trigger hardware WUPT timer| AFE_Seq["AD5941 SRAM Sequencer"]:::hw
    AFE_Seq -->|5. Interrupt Pin A2 Aktif| ISR_Call["XIAOPort.cpp (Falling ISR)"]:::hw
    ISR_Call -->|6. Panggil Handler| Ramp
    
    %% Interaksi ke Hardware/PGA/Filter
    EIS -->|Set sinyal AC sinus| WG
    WG -->|Optimasi otomatis Gain| GC
    EIS -->|Set Sinc filters / DFT| ADC
    
    %% Parsing Data & Dekode 18-bit signed
    CA -->|7. Dekode biner| Util
    SWV -->|7. Dekode biner| Util
    DPV -->|7. Dekode biner| Util
    OCP -->|7. Dekode biner| Util
    EIS -->|7. Dekode biner| Util
    Ramp -->|7. Dekode biner| Util
    
    %% Penyimpanan koordinat data
    CA -->|8. Simpan data| Storage
    SWV -->|8. Simpan data| Storage
    DPV -->|8. Simpan data| Storage
    OCP -->|8. Simpan data| Storage
    EIS -->|8. Simpan data| Storage
    Ramp -->|8. Simpan data| Storage
    
    Storage -->|9. Kirim koordinat ke serial| Host["Komputer / Python UI"]:::main
```

---

## Penjelasan Alur Interaksi

1. **Orkestrator Utama (`AD5941_25.ino`)**:
   * Menjalankan fungsi `loop()` secara terus-menerus.
   * Melakukan polling ke `C_Communication::ReadAndProcess()` untuk membaca perintah yang dikirim komputer lewat USB Serial.

2. **Parser Perintah (`src/communication/`)**:
   * Membaca buffer serial dan membagi perintah (tokenisasi).
   * Melakukan *routing* panggilan langsung ke kelas metode elektrokimia yang sesuai di `src/electrochemical_methods/` (seperti `c_eis.cpp`, `c_ca.cpp`, dll).

3. **Metode Non-Ramp (CA, SWV, DPV, OCP, EIS)**:
   * Mengatur bias tegangan sel menggunakan Low-Power DAC secara langsung.
   * Menghubungi modul hardware untuk inisialisasi filter (`adc_control.cpp`) atau generator gelombang AC (`wave_gen.cpp`).
   * Menunggu jeda kimiawi (*settling delay*).
   * Membaca hasil register konversi ADC, mengubah nilai biner 18-bit ke bentuk float voltase/arus (`utilities.cpp`), menyimpannya di memori (`data_storage.cpp`), lalu mengirimkannya ke Serial.

4. **Metode Ramp (Cyclic Voltammetry / CV)**:
   * Karena memerlukan kecepatan tinggi, CV memanggil konfigurasi di `cv.cpp` yang meneruskannya ke mesin sequencer `rampTest.cpp`.
   * Perintah dirangkai langsung di memori SRAM AD5941. Saat berjalan, chip mengaktifkan jalur interrupt MCU (`A2`) setiap kali selesai membaca step.
   * Fungsi `AppRAMPISR()` mengambil data dari FIFO chip, merata-ratakannya, lalu melakukan konversi dan penyimpanan.
