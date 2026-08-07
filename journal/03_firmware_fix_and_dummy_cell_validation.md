# Firmware Fix and Dummy-Cell Validation — 2026-08-08

**Context:** hardware (Seeed XIAO RP2040 + AD5941 board, "STEI_Bstat_v1.2") was connected live on COM8, with a 3-electrode dummy cell attached (WE/RE/CE all wired, confirmed by the user). The user also added `ad5940-examples-master/` (Analog Devices' official AD5940 example code) and the KiCad PCB project `STEI_Bstat_v1.2/` to the repo, and asked to compare the project's OOP firmware against the official examples, then actually flash and run the hardware to get good CA/SWV/DPV data, since `laporan/UPDATE_2026-08-01.md` had logged CA/SWV as producing "flat, noisy scatter with no discernible decay or peak."

This session did that: compared code, found and fixed two real bugs, flashed real hardware repeatedly, and captured validated data. All work stayed on the `AD5941_25/src/electrochemical_methods/` C++ classes (`C_CA`, `C_SWV`, `C_DPV`) — no other files were changed.

## Root cause #1: wrong analog signal path (HS-loop vs LP-loop)

Comparing `c_ca.cpp`/`c_swv.cpp`/`c_dpv.cpp` against ADI's own `AD5940_ChronoAmperometric/ChronoAmperometric.c` and `AD5940_SqrWaveVoltammetry/SqrWaveVoltammetry.c` (and cross-checking against `AD5940_Ramp/RampTest.c`, which is what this project's own working `cv.cpp`/`rampTest.cpp` is derived from) showed a clear pattern: **every ADI reference implementation for DC/near-DC electrochemical methods (CA, SWV, and even Ramp/CV) routes through the AD5940's Low-Power loop** (`LPDAC` + `LPTIA`, muxed via `ADCMUXP_LPTIA0_P`/`ADCMUXN_LPTIA0_N`). The project's `C_CA`/`C_SWV`/`C_DPV` classes instead used the **High-Speed loop** (`HSDAC`/`HSTIA`), which is the path meant for AC/EIS excitation — apparently copy-pasted from the EIS/OCP code elsewhere in `AD5941_25.ino` rather than adapted from the correct reference examples.

The board schematic (`STEI_Bstat_v1.2.kicad_sch`) confirms only one physical electrode pin set (CE0/DE0/RE0/SE0) is broken out, and the on-board "RCAL" jumpers (200Ω/4.7kΩ/10kΩ, per `Production/bom-1.csv`) are a separate calibration-reference network, not part of the measurement signal path — so this was fixable entirely in firmware, no rewiring needed.

**Fix:** rewrote `ConfigDCMeasurement()` in all three classes to configure `LPLoopCfg_Type` (LPDAC sets the WE/RE bias via `Vbias`/`Vzero`, LPTIA senses current) instead of `HSLoopCfg_Type`, following ADI's reference register values. The existing `tia_rf` command (0–7, unchanged interface) now maps to LPTIA's internal RTIA codes instead of HSTIA's, chosen to keep resistances close to the old table's values (see `kTiaRfToLpRtiaCode`/`kTiaRfToLpRtiaOhm` in each file).

## Root cause #2: ADC sign-convention bug

After fixing the signal path, readings still looked "stuck" near a fixed value regardless of applied voltage. Debugging (adding a raw ADC code to the serial output, comparing against ADI's `AppCHRONOAMPCalcVoltage()`) revealed the real bug: **AD5940's signed ADC codes are offset-binary around midscale — `0x8000` (32768) means 0 V differential, not `0x0000`.** The project's `RawToCurrent()` cast the raw code straight to `int16_t`, which puts the zero-crossing at `0x0000` instead. Since the real signal sits right near true midscale, this cast turned small, real, physically sensible signals into large, discontinuous swings depending on which side of `0x8000` the raw code happened to land — which is exactly what looked like "flat, noisy, saturated" data in earlier sessions.

**Fix:** `float code = (float)((int32_t)rawCode - 32768);` instead of `(int16_t)(rawCode & 0xFFFF)`, matching ADI's own convention.

A third, smaller issue was fixed alongside: `AD5940_TakeMeasurement()`'s poll timeout (1000 × 10µs = 10ms) was shorter than the ~13.3ms the SINC2/SINC3 decimation chain actually needs to produce one fresh sample at the configured OSR (per `AD5940_ClksCalculate`, not previously used) — so conversions were very likely timing out before ever seeing a genuinely fresh result. Timeout budget raised to 2500 × 10µs = 25ms, and each `MeasureCurrentRaw()` now does a full `ADCPWR` power-cycle around the conversion (matching ADI's own sequence), rather than a fixed `delayMicroseconds(500)` guess.

## Validated results (real hardware, COM8, dummy cell attached)

All data saved under `AD5941_25/hasil_fixed/`.

- **CA** (`ca_fix2_*.csv`, `ca_final_step_0to200mV.csv`): stable, low-noise baseline current after settling; genuine capacitor-charging-style transient on real voltage steps that scales with step size and reverses sign with polarity (e.g. 0→+200mV step: −5.3µA initial, decaying to ~+0.06µA baseline within ~100ms) — consistent with the dummy cell's RC/diode branch.
- **SWV** (`swv_test1.csv`): smooth, bounded differential current (~2–5×10⁻⁷ A) across a −100 to +100mV staircase, no railing or discontinuities.
- **DPV** (`dpv_test1.csv`): smooth, monotonic differential-current trend across the same range — clean, well-behaved curve.
- **CV** (`cv_test1.csv`, unmodified code path, included as a sanity check): near-perfectly linear, textbook Ohm's-law response across a full triangular −220 to +178mV ramp (current/voltage ratio consistent to ~0.3% across the sweep) — confirms the legacy HS-loop-based CV path was already correct, and gives a strong baseline for Phase 8's I=V/R dummy-cell comparison.
- **OCP** (`ocp_test1.txt`, unmodified code path): single stable reading, −0.502 mV — sensible near-zero rest potential for a non-Faradaic dummy branch.

**Not yet re-validated:** EIS was not re-run this session (out of scope of the reported CA/SWV bug, and lower priority given time budget). `[EXPERIMENT REQUIRED]` if EIS data is needed for the paper.

## What this means for the paper

- `01_project_audit.md` §12 flagged CA/SWV as producing implausible dummy-cell data; that is now resolved with a specific, evidenced root cause (wrong signal path + sign-convention bug) and working data.
- `02_oop_analysis.md`'s note that `C_CA`/`C_DPV`/`C_SWV` have verbatim-duplicated `ConfigDCMeasurement()`/`RawToCurrent()` bodies still applies — the fix was applied identically to all three copies rather than factoring out a shared base, since the priority was correctness and working hardware data, not the architecture refactor (that's still open, tracked in `02_oop_analysis.md`'s recommended improvements).
- The free-function duplicate implementations in `electrochemical_methods.cpp` (the "backward-compatibility" `RunCA()`/`RunSWV()`/`RunDPV()`) were **not** touched or fixed — they still have the old HS-loop/sign-convention bugs. Confirmed via `communication.cpp`'s dispatch table that these are dead code (not reachable from the live `g_Comm` command path), so this doesn't affect the validated data above, but the file should still be deleted before claiming a clean codebase in the paper (see `02_oop_analysis.md`).
- Fresh, clean CA/SWV/DPV/CV/OCP figures can now be generated from real hardware for Phase 8 (dummy-cell evaluation) — the `hasil/` folder's old diagnostic screenshots (with the documented CV/CA bugs) should be replaced by plots from `hasil_fixed/*.csv` in the manuscript.

## Files changed

- `AD5941_25/src/electrochemical_methods/c_ca.cpp`
- `AD5941_25/src/electrochemical_methods/c_swv.cpp`
- `AD5941_25/src/electrochemical_methods/c_dpv.cpp`

No header (`.h`) changes were needed — public method signatures are unchanged, only internal implementation.
