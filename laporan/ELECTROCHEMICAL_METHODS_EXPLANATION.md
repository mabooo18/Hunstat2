# How Electrochemical Methods Work in Code (AD5941)

This document explains what an electrochemical method is, how it functions physically, and how the underlying physics are translated into C++ code in the HunStat2 firmware.

---

## 1. What is an Electrochemical Method and How Does It Work?

### 1.1. Core Concept of Electrochemistry
**Electrochemistry** is the study of chemical reactions that involve the transfer of electrons between an electronic conductor (an **electrode**) and an ionic conductor (an **electrolyte** containing chemical species). 

An **electrochemical method** (or analytical technique) is an experiment where we use a electronic instrument (a **potentiostat**) to control the electrical potential (voltage) of the working electrode and measure the resulting current (flow of electrons) as chemical species undergo oxidation or reduction reactions at the electrode surface.

### 1.2. The Three-Electrode Cell Setup
To perform these measurements accurately, we use a **3-electrode cell** configuration. The potentiostat interfaces with these electrodes:

1. **Working Electrode (WE)**: The surface where the chemical reaction of interest takes place. We measure the current flowing into or out of this electrode.
2. **Reference Electrode (RE)**: Provides a stable, constant reference potential (voltage) that does not change. The potentiostat measures the potential of the WE relative to the RE. **Importantly, no current is allowed to flow through the RE** to prevent its potential from drifting.
3. **Counter Electrode (CE)** (or Auxiliary Electrode): Completes the electrical circuit. The potentiostat drives current through the CE to match whatever current is flowing through the WE, maintaining the target potential between WE and RE.

```
       [ Potentiostat Control Feedback Loop ]
                 |                  |
                 v                  v
         +---------------+  +---------------+
         |   RE Pin      |  |    CE Pin     |
         | (Reference)   |  |   (Counter)   |
         +---------------+  +---------------+
                 |                  |
                 v                  v
            ==============================
                  ELECTROCHEMICAL CELL
            ==============================
                          |
                          v
                  +---------------+
                  |    WE Pin     | ----> [ Transimpedance Amp (TIA) ]
                  |   (Working)   |               |
                  +---------------+               v
                                          [ 16-Bit Sigma-Delta ADC ]
                                                  |
                                                  v
                                          [ Raw Digital Code ]
```

### 1.3. Oxidation, Reduction, and Potential
The potential applied to the Working Electrode acts as a "pump" for electrons:
* **Oxidation (Anodic Current)**: If we apply a sufficiently **positive potential** to the WE, we lower the energy of its electrons. The electrode begins to pull electrons *away* from molecules in the solution. This electron loss is oxidation, producing a positive (anodic) current.
* **Reduction (Cathodic Current)**: If we apply a sufficiently **negative potential** to the WE, we raise the energy of its electrons. The electrode begins to push electrons *into* molecules in the solution. This electron gain is reduction, producing a negative (cathodic) current.

### 1.4. Mass Transport and Diffusion
When a reaction occurs at the WE surface, the concentration of the chemical species drops near the electrode. New molecules must travel to the surface to keep the reaction going. This travel happens through **diffusion** (movement from high concentration to low concentration). 

Most electrochemical methods wait for a brief **settling delay** before reading the current to ensure that the current measured is driven by stable diffusion gradients rather than the charging of the electrical "double-layer capacitance" of the cell.

---

## 2. The Core AD5941 Loop: Voltage Excitation & Current Readback

As you noted, the AD5941's job in code is simply:
1. **Apply a target voltage (potential)** between the electrodes.
2. **Wait** for the system to settle.
3. **Measure and read the resulting current** flowing through the cell.

Inside the AD5941 chip:
* **Voltage Generation**: A Digital-to-Analog Converter (either the Low-Power 12-bit DAC or the High-Speed 12-bit DAC) sets the target voltage.
* **Current Measurement**: Current entering the **WE** pin is converted into a voltage using a **Transimpedance Amplifier (TIA)** (with a feedback resistor $R_f$).
* **Analog-to-Digital Conversion**: The output voltage of the TIA is measured by the on-chip **16-bit Sigma-Delta ADC**.
* **Ohm's Law Conversion**: The firmware converts the raw ADC bits back into current using:
  $$\text{Current (Amperes)} = \frac{\text{Measured Voltage}}{\text{PGA Gain} \times R_{TIA}}$$

---

## 3. Chronoamperometry (CA) in Code

**Goal**: Apply a single constant voltage to the cell and record the current decay over time.

### Step-by-Step Code Flow:
1. **Set Voltage**: The host sends command `1` (e.g. `1400` for 400 mV). The code saves this in `CA_Voltage_mV`.
2. **Initialize AFE**: The function `Config_AD5941_DCMeasurement(CA_Voltage_mV)` is called:
   * It turns off the AC Waveform Generator (`AFECTRL_WG` set to false).
   * It programs the DAC to output the target potential using `SetDACLevel(CA_Voltage_mV)`.
   * It configures the switch matrix to route the TIA to the Working Electrode (`WE`).
3. **Start ADC**: The function `MeasureCurrentRaw()` turns on the ADC power and conversions.
4. **Delay**: It waits for a settling delay (`delayMicroseconds(500)`) to let the capacitive double-layer charging current settle.
5. **Read Result**: The ADC captures a conversion sample (`AD5940_TakeMeasurement()`).
6. **Convert to Current**: `RawToCurrent()` interprets the signed 16-bit raw code and divides it by the TIA resistor value ($R_f$) to compute Amperes.
7. **Stream**: The coordinates are formatted (`CA,<time>,<current>`) and printed to the Serial port.

---

## 4. Cyclic Voltammetry (CV) in Code

**Goal**: Sweep the voltage linearly from $V_{start}$ to $V_{stop}$, and then back to $V_{start}$, measuring the current continuously to observe oxidation and reduction peaks.

### Step-by-Step Code Flow:
*Because CV sweeps are fast and require precise timing, doing this in microcontroller loops causes jitter. Instead, the firmware compiles commands directly into the AD5941's internal **Hardware Sequencer SRAM**.*

1. **Setup Parameters**: The variables `V_Start`, `V_Stop`, `EStep` (step size), and `ScanRate` (speed) are configured.
2. **Sequencer Compilation (`AppRAMPInit`)**:
   * The code calculates the list of voltage steps from start to peak and back.
   * It compiles sequencer command blocks:
     * **SEQ0/SEQ1**: Commands instructing the AFE to write a new voltage step code directly to the LP-DAC register, wait for settling, and sleep.
     * **SEQ2**: Commands instructing the AFE to wake up and turn on the ADC.
3. **Trigger Sweeps (`AppRAMPCtrl`)**:
   * The **Wakeup Timer (WUPT)** is configured to run at intervals matching the scan rate.
   * On every timer tick, the hardware runs the compiled sequences:
     `Update Voltage (SEQ0/SEQ1) -> Wait Settling -> Start ADC (SEQ2) -> Capture Data`
4. **Interrupt Handling (`AppRAMPISR`)**:
   * Each time a step completes, the AFE pulls the microcontroller's interrupt pin (`A2`) LOW.
   * The RP2040 runs `AppRAMPISR()`:
     * It reads the raw samples out of the AFE's FIFO buffer.
     * It averages the samples to remove high-frequency noise.
     * It prints the resulting Voltage vs. Current coordinate over Serial.

---

## 5. Square Wave Voltammetry (SWV) in Code

**Goal**: Sweep potential in a staircase pattern, but overlay a high-frequency square wave pulse on each step to eliminate background charging current.

```
Voltage
  ^
  |          Forward Pulse (V_forward = V_step + Amplitude)
  |            _
  |          /   \
  |         |     |
  |    _____|     |_____  <- Staircase Step (V_step)
  |               |     |
  |                \   /
  |                  -    <- Reverse Pulse (V_reverse = V_step - Amplitude)
  +------------------------------------------------------------> Time
```

### Step-by-Step Code Flow:
For each step in the staircase sweep:
1. **Apply Forward Potential**: 
   * The potential is set to $V_{forward} = V_{step} + V_{amplitude}$ using `ConfigDCMeasurement()`.
   * It waits for `SWV_CurrentSampleDelay_s` (e.g. 20 ms).
   * It takes an ADC reading ($I_{forward}$) using `MeasureCurrentRaw()`.
2. **Apply Reverse Potential**: 
   * The potential is set to $V_{reverse} = V_{step} - V_{amplitude}$ using `ConfigDCMeasurement()`.
   * It waits for `SWV_CurrentSampleDelay_s`.
   * It takes an ADC reading ($I_{reverse}$) using `MeasureCurrentRaw()`.
3. **Differential Math**: 
   * It computes $\Delta I = I_{forward} - I_{reverse}$.
4. **Stream**:
   * Outputs the coordinate `SWV,<voltage>,<delta_I>`.
5. **Next Step**: Increments the staircase baseline voltage $V_{step}$ and repeats.

---

## 6. Differential Pulse Voltammetry (DPV) in Code

**Goal**: Sweep potential in a staircase pattern, applying a periodic voltage pulse at each step to isolate faradaic reactions.

```
Voltage
  ^
  |                       Pulse (V_pulse = V_step + Amplitude)
  |                         ________
  |                        |        |
  |                        |        |
  |    ____________________|        |____  <- Staircase Step (V_step)
  |    ^                            ^
  |    |                            |
  |  Sample Base Current          Sample Pulse Current
  |   (I_base)                     (I_pulse)
  +------------------------------------------------------------> Time
```

### Step-by-Step Code Flow:
For each step in the staircase sweep:
1. **Sample Base Current**:
   * Applies the staircase baseline potential $V_{step}$ to the cell using `ConfigDCMeasurement()`.
   * Waits for settling, then measures base current ($I_{base}$) using `MeasureCurrentRaw()`.
2. **Sample Pulse Current**:
   * Applies the pulse potential $V_{pulse} = V_{step} + V_{amplitude}$ using `ConfigDCMeasurement()`.
   * Waits for settling, then measures pulse current ($I_{pulse}$) using `MeasureCurrentRaw()`.
3. **Differential Math**:
   * Computes $\Delta I = I_{pulse} - I_{base}$.
4. **Stream**:
   * Outputs the coordinate `DPV,<voltage>,<delta_I>`.
5. **Wait and Increment**:
   * Delays for the remainder of the pulse period, increments $V_{step}$, and repeats.

---

## 7. Electrochemical Impedance Spectroscopy (EIS) in Code

**Goal**: Apply an AC sine wave voltage perturbation at various frequencies, measure the amplitude and phase shift of the resulting AC current, and calculate the cell impedance ($Z$).

### Step-by-Step Code Flow:
For each log-spaced frequency step:
1. **Configure AC Waveform**:
   * `Do_WaveGen()` programs the high-speed sinusoidal wave generator.
   * It calculates the required frequency control word using the system clock ($16\text{ MHz}$).
2. **Route Switches**:
   * Mode `0` (working scan): routes the excitation wave through the CE pin, and TIA to the WE pin.
   * Mode `1` (calibration scan): routes the excitation wave through the internal calibration resistor ($R_{cal}$).
3. **Configure DFT Engine**:
   * The AD5941 has an onboard **Discrete Fourier Transform (DFT) hardware accelerator**.
   * `Hardware_Init_AD5940_ADC()` configures the DFT size (e.g. 16384 points for low frequencies, 4096 for mid-range).
4. **Acquire DFT Data**:
   * The ADC samples the TIA current output.
   * The DFT engine processes the ADC samples in real-time, calculating:
     * **DFT_REAL**: Cosine component of the AC current vector.
     * **DFT_IMAG**: Sine component of the AC current vector.
   * The firmware reads these real/imaginary values directly from the FIFO read register `REG_AFE_DATAFIFORD`.
5. **Nyquist Impedance Calculation**:
   * In SeeedStat mode, it performs two sweeps (one on the cell $R_z$, one on $R_{cal}$).
   * The magnitude ($\text{mag}$) and phase ($\text{phase}$) are calculated for both sweeps:
     $$\text{mag} = \sqrt{\text{Real}^2 + \text{Imag}^2}$$
     $$\text{phase} = \text{atan2}(-\text{Imag}, \text{Real})$$
   * The cell impedance is computed relative to the known calibration resistor value ($fR_{cal}$):
     $$Z_{mag} = \left|\frac{\text{mag}_{R_{cal}}}{\text{mag}_{R_z}}\right| \times fR_{cal}$$
     $$Z_{phase} = \text{phase}_{R_z} - \text{phase}_{R_{cal}}$$
   * The impedance is converted back to Cartesian Nyquist coordinates ($X = \text{Real } Z$, $Y = \text{Imag } Z$):
     $$\text{Nyquist } X = Z_{mag} \times \cos(Z_{phase})$$
     $$\text{Nyquist } Y = Z_{mag} \times \sin(Z_{phase})$$
   * The resulting Nyquist points are printed to the Serial monitor.
