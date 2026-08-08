# Code Comparison — Project Firmware vs. Analog Devices AD5940 Reference Examples

**Scope:** this document compares the fixed `C_CA`/`C_SWV`/`C_DPV` classes (`AD5941_25/src/electrochemical_methods/`) against the Analog Devices reference examples added to the repo (`ad5940-examples-master/examples/AD5940_ChronoAmperometric/ChronoAmperometric.c`, `AD5940_SqrWaveVoltammetry/SqrWaveVoltammetry.c`, `AD5940_Ramp/RampTest.c`). It covers what is structurally different, what parameters differ (with actual values from both codebases), and why — distinguishing *deliberate simplifications* made for this project's runtime-configurable, MCU-polling architecture from *genuine gaps* still open for future work.

**Verification note:** the CA comparison below is a full comparison — `ChronoAmperometric.c`/`.h` (639 + 111 lines) were read in full. The SWV comparison is based on `SqrWaveVoltammetry.h`'s configuration struct and a partial reading of `SqrWaveVoltammetry.c` (941 lines; the state-machine body was not read line-by-line). **DPV has no ADI reference in this repository at all** — see §7. Treat the SWV/DPV architectural claims below as extrapolated from the CA comparison and the confirmed LP-loop pattern, not as an independently verified line-by-line diff. `[CODE VERIFICATION REQUIRED]` if a full SWV/DPV diff is needed later.

---

## 1. Programming Model: Hardware Sequencer vs. Direct MCU Polling

This is the largest structural difference, and it was a deliberate choice, not an oversight.

**ADI's `ChronoAmperometric.c`** drives the AD5940 through its onboard **sequencer**: register-configuration and measurement steps are compiled into byte-code (`SEQ_WR`, `SEQ_WAIT`, `SEQ_STOP`) and written into the AD5940's own SRAM (`AD5940_SEQCmdWrite`). A **Wakeup Timer**, clocked from a 32 kHz low-frequency oscillator, fires the measurement sequence (`AppCHRONOAMPCtrl(CHRONOAMPCTRL_START, ...)`) at a precisely-timed interval (`AmpODR`, in Hz) while the MCU and much of the AFE sleep (`AD5940_EnterSleepS()`) between samples, waking only to service a FIFO-threshold interrupt (`AppCHRONOAMPISR`). This is a **power-optimized, hardware-timed** design intended for battery-powered, long-duration monitoring.

**This project's `C_CA::Run()`** does none of this. It is a plain MCU-side loop:
```cpp
for (uint32_t i = 0; i < m_pData->CA_NumSamples; ++i) {
    uint32_t raw = MeasureCurrentRaw();      // blocking AFECTRL toggle + poll
    ...
    delay((int)(1000.0f / m_pData->CA_SampleRate_Hz));
}
```
The AD5940 sequencer is **never used anywhere in this project** (confirmed: no `SEQ_WR`/`SEQ_GenInit` calls anywhere in `src/`). Every method — CA, SWV, DPV, EIS, OCP — configures registers directly from the MCU and polls status flags in a busy-wait loop.

**Why this matters, concretely:**
- **Timing precision.** ADI's `AmpODR` gives an exact, hardware-guaranteed sample interval. This project's `delay(1000/SampleRate_Hz)` is added *on top of* however long the actual conversion took (~13–16 ms after the fix in this work), so the true sample period is `measurement_time + delay`, not exactly `1/SampleRate_Hz`. At low rates (the project's default is 100 Hz, i.e. 10 ms requested) the discrepancy is proportionally large. This is a real, unresolved accuracy gap for any figure that reports a precise timebase.
- **Fast-transient capture.** ADI's reference additionally provides a *dedicated* single-shot "transient" sequence (`CHRONOAMPCTRL_PULSETEST` / `AppCHRONOAMPTransientMeasureGen`) that samples as fast as the raw filter chain allows, with no MCU-loop overhead — meant for exactly the kind of fast capacitive decay observed in this project's CA data (settling within ~40 ms, Section III-B of `manuscript.tex`). This project's polling loop cannot resolve that decay finely, because each loop iteration itself costs several milliseconds of MCU-side overhead (serial print, `delay()`, function-call overhead) on top of the ADC's own ~13 ms fill time.
- **Power.** Not evaluated in this work, but ADI's sleep-between-samples model would materially change any future battery-life claim; this project's implementation keeps the MCU and AFE active/polling throughout.

**Why the project doesn't use the sequencer:** the whole codebase (predating this session) is built around a synchronous, ASCII-serial-command request/response protocol (`C_Communication::ReadAndProcess()` in the main `loop()`), not an interrupt-driven, sleep/wake state machine. Adopting the sequencer would be a substantially larger architectural change than the scope of this fix — it is flagged here as a legitimate future improvement, not treated as a defect of the current work.

---

## 2. Signal Path Configuration (the core bug, side by side)

This is covered in depth in `03_firmware_fix_and_dummy_cell_validation.md`; summarized here for direct comparison.

| | ADI reference (`ChronoAmperometric.c`) | Project, before this fix | Project, after this fix |
|---|---|---|---|
| Excitation | LPDAC (`LPLoopCfg_Type.LpDacCfg`) | HSDAC (`WGCfg_Type` via `SetDACLevel()`) | LPDAC (`LPLoopCfg_Type.LpDacCfg`) |
| Current sensing | LPTIA, `ADCMUXP_LPTIA0_P`/`ADCMUXN_LPTIA0_N` | HSTIA, `ADCMUXP_HSTIA_P`/`ADCMUXN_HSTIA_N` | LPTIA, `ADCMUXP_LPTIA0_P`/`ADCMUXN_LPTIA0_N` |
| Switch matrix | N/A (LP loop doesn't use the HS switch matrix) | `SWD_CE0`/`SWP_RE0`/`SWN_SE0`/`SWT_TRTIA\|SWT_SE0LOAD` (HS matrix) | HS loop explicitly zeroed and disabled (`AD5940_StructInit` + `AD5940_HSLoopCfgS`) |

The applied-potential computation is now structurally identical between ADI and this project:
```c
/* ADI, AppCHRONOAMPSeqCfgGen() */
lp_loop.LpDacCfg.DacData6Bit  = (uint32_t)((Vzero-200)/DAC6BITVOLT_1LSB);
lp_loop.LpDacCfg.DacData12Bit = (int32_t)(SensorBias/DAC12BITVOLT_1LSB) + DacData6Bit*64;
```
```cpp
/* This project, C_CA::ConfigDCMeasurement() (after fix) */
uint32_t vzeroCode = (uint32_t)((Vzero_mV - 200.0f) / DAC6BITVOLT_1LSB);
int32_t  vbiasCode = (int32_t)(voltage_mV / DAC12BITVOLT_1LSB) + (int32_t)(vzeroCode * 64);
```
The one functional difference: ADI's `Vzero` is a user-configurable field (defaults to 1100 mV in their example); this project **hardcodes** `Vzero_mV = 1100.0f` as a constant inside `ConfigDCMeasurement()` and only exposes the equivalent of ADI's `SensorBias` as the runtime-settable parameter (`voltage_mV`, driven by the serial `1<value>` command). This was a deliberate simplification — see §6.

---

## 3. Parameter Model: Struct-of-Everything vs. Minimal Runtime Command

ADI's `AppCHRONOAMPCfg_Type` (in `ChronoAmperometric.h`) has **34 fields** covering sequencer bookkeeping, clock frequencies, FIFO configuration, RTIA calibration state, and measurement parameters — it is designed to be edited **at compile time** (a static struct literal) for a fixed experiment, then run.

This project instead exposes a **small, runtime-settable parameter subset** through the existing ASCII serial protocol, because the instrument is meant to be driven live from the host GUI without recompiling firmware per experiment. Table 1 compares the parameters that actually govern a CA run in each codebase.

**Table 1 — CA parameter comparison**

| Parameter | ADI default (`ChronoAmperometric.c`) | This project's default (`data_storage.h`) | Runtime-settable here? |
|---|---|---|---|
| Applied potential | `pulseAmplitude` = 500 mV (step test) / `SensorBias` (general) | `CA_Voltage_mV` = 0.0 mV | Yes (`1<mV>`) |
| Duration / sample count | `pulseLength` = 500 ms (transient) or driven by `AmpODR`+`NumOfData` | `CA_Duration_s` = 1.0 s | Yes (`2<s>`) |
| Sample rate / ODR | `AmpODR` = 1 Hz | `CA_SampleRate_Hz` = 100 Hz | Yes (`3<Hz>`) |
| Vzero (WE bias point) | `Vzero` = 1100 mV (configurable field) | 1100 mV (hardcoded constant) | **No** |
| LPTIA RTIA | `LptiaRtiaSel` = `LPTIARTIA_10K` | `tia_rf`→`kTiaRfToLpRtiaCode[]`, default index 3 → `LPTIARTIA_10K` | Yes (`r<0..7>`) |
| LPTIA filter resistor | `LpTiaRf` = not set in this struct's listed defaults (app-specific) | `LPTIARF_1M` (hardcoded, matches ADI's own value used elsewhere in their examples) | No |
| ADC PGA gain | `ADCPgaGain` = `ADCPGA_1P5` | `adc_base.ADCPga = 1` (i.e. `ADCPGA_1`, 1×) | No |
| ADC SINC2 OSR | `ADCSinc2Osr` = `ADCSINC2OSR_44` | `ADCSINC2OSR_1333` | No |
| ADC SINC3 OSR | `ADCSinc3Osr` = `ADCSINC3OSR_4` | `ADCSINC3OSR_4` | No (matches) |
| ADC reference | `ADCRefVolt` = 1.82 V | `Vref_mV = 1820.0f` (1.82 V) | No (matches) |
| RTIA calibration reference | `RcalVal` = 10 000 Ω, used by `AppCHRONOAMPRtiaCal()` | Not used — see §4 | N/A |

**The PGA gain and SINC2 OSR rows are real, unreconciled numeric differences**, not part of this session's fix (both were already present in the pre-fix code and were left unchanged since they don't affect the sign/loop-path bugs being corrected):
- **PGA 1× vs 1.5×**: ADI's 1.5× default trades off some input range for better resolution on small signals. This project uses 1× (unity gain), which is a marginally more conservative default (max input range, less risk of clipping) but yields a coarser effective LSB for very small currents.
- **SINC2 OSR 44 vs 1333**: this is a substantial difference. ADI's default (44) trades filtering/noise rejection for speed, consistent with their fast 1 Hz-class ODR target; this project's 1333 is far higher, giving heavier decimation/lower noise **and** requiring the much longer ~13.3 ms fill time computed in this work's fix (see `03_firmware_fix_and_dummy_cell_validation.md`, and Eq. via `AD5940_ClksCalculate`). This project's higher OSR is arguably a reasonable choice for a benchtop-style dummy-cell characterization instrument (favor low noise over speed), but it was inherited from the pre-existing EIS/OCP code rather than chosen deliberately for CA/SWV/DPV — **worth revisiting** as a considered decision rather than an inherited default. `[EXPERIMENT REQUIRED]` if a formal noise-vs-speed trade study is wanted.

---

## 4. RTIA Value: Live-Calibrated (ADI) vs. Nominal Table (this project)

ADI's reference performs an **active calibration** before each new configuration (`AppCHRONOAMPRtiaCal()` → `AD5940_LPRtiaCal()`), measuring the actual LPTIA RTIA value against the board's real `RcalVal` (10 kΩ nominal) and storing a calibrated magnitude+phase (`fImpPol_Type RtiaCalValue`) used in every subsequent current calculation:
```c
fCurrent = fVoltage / AppCHRONOAMPCfg.RtiaCalValue.Magnitude;
```
This project **does not calibrate** — `RawToCurrent()` uses a fixed, datasheet/factory-trim-typical lookup table (`kTiaRfToLpRtiaOhm[]`, sourced from the vendor driver's own internal `LpRtiaTable[]` in `ad5940.cpp`) as if it were exact:
```cpp
float Rtia_Ohm = (tia_rf < 8) ? kTiaRfToLpRtiaOhm[tia_rf] : 10000.0f;
```
This is the single most important remaining accuracy gap between the two approaches. It does **not** affect the linearity result reported in `manuscript.tex` (CV, $R^2=0.999997$), since a fixed multiplicative error in $R_{TIA}$ would not change the *shape* of an $I$–$V$ curve — but it does mean **absolute current values reported by this instrument have not been independently calibrated against a known reference resistor**, only against the vendor's typical/nominal RTIA table. Implementing the equivalent of `AppCHRONOAMPRtiaCal()` (already present in the vendor driver as `AD5940_LPRtiaCal()`, just never called by this project) is the concrete next step to close this gap. `[EXPERIMENT REQUIRED]`.

---

## 5. Timing/Delay Constants: Calculated (ADI) vs. Empirical Margin (this project)

ADI computes the exact number of system-clock wait cycles needed before a sample is valid, per configuration, using `AD5940_ClksCalculate()` — e.g. for a single SINC2 sample at OSR 1333/OSR3 4, this evaluates to 213 555 system clocks (≈13.3 ms at 16 MHz), and that exact figure is what gets encoded as a `SEQ_WAIT()` instruction.

This project (not using the sequencer, see §1) instead uses a **fixed, generously-padded timeout** discovered empirically during this session's debugging (raised from an under-sized 10 ms budget that was very likely the cause of stale/timed-out reads, to a 25 ms budget with comfortable margin above the calculated ~13.3 ms minimum):
```cpp
int32_t time_out = 2500;   // 2500 * 10us poll step = 25ms ceiling
```
This is functionally safe (the real fill time is polled-for via the SINC2RDY flag, not blindly waited-out — the 25 ms figure is only a ceiling before giving up), but it is **not** the same engineering discipline as ADI's exact, configuration-derived constant. If the ADC filter OSR settings are ever changed, this project's fixed 25 ms budget must be manually re-checked against a fresh `AD5940_ClksCalculate()`-style computation for the new settings; ADI's sequencer-based approach would recompute this automatically.

---

## 6. What Was a Deliberate Design Choice (not a gap)

To be fair to the project's own architecture, several differences from ADI's reference are intentional and appropriate for this instrument's actual use case (a host-GUI-driven, live-configurable instrument), not oversights:

- **Runtime parameter configuration over compile-time structs.** ADI's examples are meant to be recompiled per experiment; this project's ASCII command protocol lets a user (or the Python GUI) change CA/SWV/DPV parameters live without reflashing. This is a genuine usability advantage for this project's context, at the cost of the parameter surface being smaller than ADI's full struct (Table 1).
- **No sequencer / no sleep.** Appropriate for a USB-tethered bench instrument where power is not constrained and simplicity of the MCU-side control flow matters more than microsecond-level timing precision or battery life.
- **Fixed Vzero.** Since this instrument's electrode bias point doesn't need to change per-measurement in normal use (only the applied potential does), collapsing ADI's two-parameter `Vzero`/`SensorBias` model into one runtime-settable value (`voltage_mV`) plus one hardcoded constant (`Vzero_mV = 1100`) is a reasonable simplification, not a bug — though it does mean the instrument cannot currently reach potentials that would require shifting Vzero itself (e.g. very large excursions near the LPDAC's rails) without a firmware constant change.

---

## 7. DPV: No Direct ADI Reference Exists

Analog Devices' `ad5940-examples` repository, as added to this project, contains **no dedicated differential-pulse-voltammetry example** (the full example list is: ADC, Amperometric, BATImpedance, BIA, BIA\_HiZ\_Electrodes, BIOZ-2Wire, BioElec, ChronoAmperometric, DFT, ECG, ECSns\_EIS, EDA, HSDACCal, Impedance, Impedance\_Adjustable\_with\_frequency, LPDAC, LPLoop, Ramp, Reset, SPI, Sequencer, SqrWaveVoltammetry, Temperature, WG). Consequently, **`C_DPV`'s fix in this work was not validated against a vendor reference implementation the way CA and SWV were** — it was fixed by applying the same confirmed-correct LP-loop pattern (identical `ConfigDCMeasurement()`/`RawToCurrent()` structure, matching CA and SWV exactly) under the reasoning that DPV is structurally a staircase-plus-single-sided-pulse variant of the same underlying measurement, not a different signal path. The hardware validation in `manuscript.tex` (smooth, bounded, monotonic DPV curve) is real evidence the fix works correctly on this hardware, but it should be understood as validated *empirically*, not *by reference comparison*, unlike CA and SWV.

---

## Summary Table

| Aspect | ADI reference | This project (post-fix) | Classification |
|---|---|---|---|
| Excitation/sensing loop | LP loop | LP loop | **Matched** (this was the core fix) |
| Applied-potential formula | Eq. via Vzero/SensorBias codes | Identical formula | **Matched** |
| ADC zero-point convention | `code - 32768` | `code - 32768` | **Matched** (this was the second fix) |
| Programming model | AD5940 hardware sequencer + wakeup timer | MCU-side polling loop | **Deliberate difference** |
| Parameter configurability | Compile-time struct | Runtime ASCII commands | **Deliberate difference** (project advantage for its use case) |
| RTIA value | Live-calibrated per session | Fixed nominal table | **Gap** — accuracy not yet characterized |
| Sample timing precision | Hardware-timed (exact ODR) | Software `delay()` (approximate) | **Gap** — not exactly the requested rate |
| ADC PGA gain | 1.5× | 1× | **Unreconciled difference**, pre-existing |
| ADC SINC2 OSR | 44 (fast) | 1333 (slow, low-noise) | **Unreconciled difference**, pre-existing |
| Fast-transient capture | Dedicated single-shot sequence | Not available | **Gap** |
| DPV reference available | No (not in ADI's example set) | Fixed by extrapolation from CA/SWV | **Validated empirically, not by reference** |
