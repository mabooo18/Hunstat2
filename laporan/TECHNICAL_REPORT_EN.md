# Comprehensive Technical Report: Portable Electrochemical Potentiostat Based on the AD5941 AFE and XIAO RP2040

---

## 1. Introduction and Background

Electrochemistry is a fundamental science bridging chemical reactions and electrical signals. Investigating redox reactions, corrosion, sensors, and bio-impedance requires a **potentiostat**—an instrument that controls the voltage difference between a Working Electrode (WE) and a Reference Electrode (RE) while measuring the resulting current flowing through a Counter Electrode (CE).

Historically, potentiostats were bulky, expensive laboratory instruments. Recent demands for Point-of-Care (PoC) medical diagnostics, environmental field testing, and wearable sensors have driven research into **portable, low-cost, and handheld potentiostats**. 

This report details the implementation, architecture, hardware design, and software refactoring of the **HunStat2**, a portable potentiostat based on the **Analog Devices AD5941** Analog Front End (AFE) and the **Seeed Studio XIAO RP2040** microcontroller.

---

## 2. Literature Review and Design Frameworks

### 2.1. Electrochemical Fundamentals
An electrochemical cell contains three electrodes:
* **Working Electrode (WE)**: The reactive surface where the redox reaction of interest occurs.
* **Reference Electrode (RE)**: An electrode with a stable, known potential (e.g., Ag/AgCl) used as a voltage reference.
* **Counter Electrode (CE)**: A conductor that supplies the current necessary to balance the reaction at the WE.

The system relies on a feedback control loop (potentiostat loop) to force the WE potential ($V_{WE}$) to match a target value relative to the RE ($V_{RE}$). Current is measured at the WE using a Transimpedance Amplifier (TIA).

### 2.2. Handheld Potentiostat Frameworks: FreiStat and HELPStat
Modern handheld designs draw inspiration from open-source potentiostat frameworks:
* **FreiStat**: A modular framework emphasizing clean separation between hardware-specific setups, communication protocols, and electrochemical methods.
* **HELPStat**: A handheld, EIS-enabled potentiostat demonstrating that high-precision Impedance Spectroscopy can be achieved on a battery-powered micro-platform using the AD5940/AD5941 family.

Our refactoring of the HunStat2 firmware adopts the **FreiStat modular paradigm**, transitioning from a monolithic code structure to a layered, maintainable software design.

---

## 3. AD5941 Architecture and Register Configurations

The AD5941 is a high-precision, low-power impedance and electrochemical front end. 

```
                                AD5941 BLOCK DIAGRAM
  +---------------------------------------------------------------------------------+
  |  +------------------------+      +-------------+                                |
  |  | Low-Power Loop (LPTIA) | <--- | 12-Bit DAC  | <----+                         |
  |  +------------------------+      +-------------+        |                         |
  |                                                         v                         |
  |  +------------------------+      +-------------+   +---------+   +-------------+  |
  |  | High-Speed Loop (HSTIA)| <--- | 12-Bit DAC  |   | Switch  |-->|  Electrodes |  |
  |  +------------------------+      +-------------+   | Matrix  |   | (WE, RE, CE)|  |
  |                                  +-------------+   |  (MMR)  |   +-------------+  |
  |  +------------------------+      | Wave Gen    |   +---------+          |         |
  |  | 16-Bit SD ADC          | <--- | (Sinusoidal)| <------+               |         |
  |  +------------------------+      +-------------+                        v         |
  |              |                                                     +---------+    |
  |              v                                                     |   TIA   |    |
  |  +------------------------+                                        +---------+    |
  |  | DFT Accelerator Engine |                                             |         |
  |  +------------------------+                                             v         |
  |              |                                                    [ADC Input]     |
  |              +------------------------> [ FIFO Buffer ] ---------------------------> SPI
  +---------------------------------------------------------------------------------+
```

### 3.1. Internal Subsystems
1. **Dual Control Loops**:
   * **Low-Power (LP) Loop**: Features a low-power potentiostat amplifier and a Low-Power TIA (LPTIA). Designed for slow DC sweeps (e.g., CV, CA, OCP, DPV, SWV) to minimize power draw.
   * **High-Speed (HS) Loop**: Optimized for high-frequency AC excitation, featuring a high-bandwidth amplifier and a High-Speed TIA (HSTIA) for Impedance Spectroscopy (EIS).
2. **Excitation Signal Generator**: Supports DC bias levels and AC sinusoidal signal generation via the High-Speed DAC (HSDAC).
3. **16-Bit Sigma-Delta ADC**: Configurable Oversampling Rates (OSR) with digital Sinc3, Sinc2, and Notch filters.
4. **DFT Hardware Accelerator**: Calculates real and imaginary Fourier coefficients internally for EIS, saving MCU overhead.
5. **Programmable Switch Matrix**: Internal Multiplexer switches configured via Memory-Mapped Registers (MMR) to route internal amplifiers to external pins.

### 3.2. Key Register Mappings
* **`REG_AFE_ADCCON`**: Configures the ADC, positive/negative multiplexer inputs, and the Programmable Gain Amplifier (PGA) gains ($1.0\times, 1.5\times, 2.0\times, 4.0\times, 9.0\times$).
* **`REG_AFE_DATAFIFORD`**: Direct read access to the FIFO buffer containing conversion results.
* **Switch Matrix Register Codes**:
  * `SWD_CE0` / `SWP_RE0` / `SWN_SE0`: Connects Counter Electrode 0, Reference Electrode 0, and Sensor Electrode (Working) 0.
  * `SWT_TRTIA`: Routes current through the Transimpedance Amplifier feedback loops.

---

## 4. Hardware Potentiostat Design

### 4.1. Schematic & Microcontroller Wiring
The XIAO RP2040 acts as the host SPI controller. The SPI bus communicates with the AD5941 using the following pin layout:

| XIAO RP2040 Pin | AD5941 Pin | Description |
|---|---|---|
| **D8 (MISO)** | MISO | SPI Master In Slave Out |
| **D10 (MOSI)** | MOSI | SPI Master Out Slave In |
| **D9 (SCK)** | SCK | SPI Clock |
| **D7 (CS)** | CS | SPI Chip Select (Active Low) |
| **D6 (RESET)** | Reset | Hardware Chip Reset |
| **A2 (GP2)** | GP0 / INT | External Interrupt Input (Falling Edge MCU ISR) |

### 4.2. Power and Decoupling
To achieve low noise floor measurements down to the pico-ampere range:
* Separate analog power ($AV_{DD}$) and digital power ($DV_{DD}$) domains are created, decoupled using $100\text{ nF}$ and $10\text{ }\mu\text{F}$ ceramic capacitors placed close to the AFE pins.
* An external low-drift calibration resistor ($R_{cal} = 10\text{ k}\Omega, 0.1\%$) is placed across the `RCAL0` and `RCAL1` pins to calibrate TIA gain errors.

---

## 5. Software Design and Modular Architecture

The refactored software structure separates orchestrators, parsers, and hardware drivers:

```
                               SOFTWARE DIRECTORY WALK
  AD5941_25/
  ├── AD5941_25.ino           <-- Main orchestrator: initializes hardware and runs loop()
  ├── cv.cpp                  <-- Cyclic Voltammetry configuration and launcher
  ├── rampTest.cpp            <-- Low-level Analog Devices sequencer setup
  ├── XIAOPort.cpp            <-- SPI interface, hardware pin maps, and MCU ISR
  ├── utilities.cpp           <-- Binary-to-float math (ToFloat) and NeoPixel status
  └── src/
      ├── setup/              <-- PGA calibrations and chip resets
      ├── data_storage/       <-- Global parameter storage and measurement caches
      ├── communication/      <-- Command parser and serial dispatcher
      ├── hardware/           <-- ADC clock, switch matrix, and TIA gain optimizers
      ├── interface/          <-- Low-level RGB NeoPixel color configurations
      ├── utils/              <-- Status LED translation utilities
      └── electrochemical_methods/ <-- Isolated technique implementations (CA, SWV, EIS, OCP, DPV)
```

### 5.1. Command Delimiter & Tokenizer Parser
The `C_Communication::ReadAndProcess()` function reads incoming characters from the USB serial buffer. It separates commands using delimiters (`;`, `|`, `\r`, `\n`) via `strtok` and dispatches tokens to type-specific parsers:
* **Single parameter floats**: Parse commands like `b-50.00` (sets bias voltage to -50 mV).
* **Multiple parameter commands**: E.g. `D 100,200,5,10,2` (configures CV sweep parameters: $V_{start}$, $V_{stop}$, $E_{step}$, $ScanRate$, $Cycles$).
* **Execution triggers**: Single character commands such as `O` (OCP), `E` (EIS), `A` (CA), `W` (SWV), `D` (DPV), `M` (CV).

### 5.2. Dynamic AFE Sequencer (`rampTest.cpp`)
Voltammetric sweeps (like CV and LSV) require fast, precisely timed steps. The AD5941's internal hardware sequencer is programmed dynamically:
* `AppRAMPInit()`: Translates physical parameters (limits, scan rates) into LP-DAC voltage step registers. It compiles command sequences into the AFE SRAM memory.
* `AppRAMPISR()`: Triggered on the falling edge of pin `A2` (GP0 interrupt). It reads data from the AFE FIFO, averages samples, converts raw registers, and sends coordinate data to the serial port.

---

## 6. Electrochemical Working Mechanisms in Code

### 6.1. Open Circuit Potential (OCP)
Measures the cell voltage without applying current. The AFE routes the positive input multiplexer to the working electrode and the negative multiplexer to the reference electrode. No bias DAC is active (high-impedance mode).

### 6.2. Cyclic Voltammetry (CV)
Sweeps potential linearly in a triangular waveform. Code in `rampTest.cpp` handles the step updates and schedules ADC reads using the hardware sequencer:
$$\text{Update LP-DAC} \rightarrow \text{Settling Delay} \rightarrow \text{ADC Conversion}$$

### 6.3. Chronoamperometry (CA)
Applies a potential step to the cell and measures current over time:
1. `SetDACLevel(CA_Voltage_mV)` is called.
2. The ADC runs continuously, capturing current at the configured sampling rate.
3. Coordinates are output as `CA,<time>,<current>`.

### 6.4. Square Wave Voltammetry (SWV)
Excitation potential consists of a staircase waveform with a high-frequency square wave.
For each step:
1. Apply $V_{forward} = V_{step} + V_{amplitude}$, sample current $I_{forward}$.
2. Apply $V_{reverse} = V_{step} - V_{amplitude}$, sample current $I_{reverse}$.
3. Output the differential current: $\Delta I = I_{forward} - I_{reverse}$.

### 6.5. Differential Pulse Voltammetry (DPV)
Excitation potential consists of periodic pulses on a staircase waveform.
For each step:
1. Measure baseline current $I_{base}$ before the pulse.
2. Apply a pulse voltage $V_{pulse} = V_{step} + V_{amplitude}$, measure current $I_{pulse}$ at the end of the pulse.
3. Output the differential current: $\Delta I = I_{pulse} - I_{base}$.

### 6.6. Electrochemical Impedance Spectroscopy (EIS)
Applies a low-amplitude AC sine wave voltage over a range of frequencies:
1. Configure sinusoidal waveform generator via `wave_gen.cpp` at frequency $f$.
2. Route excitation through the cell (or $R_{cal}$ in calibration mode).
3. Enable the on-chip DFT engine:
   * Real component: $I_{real} = \text{DFT\_REAL}$
   * Imaginary component: $I_{imag} = \text{DFT\_IMAG}$
4. Compute complex impedance and project coordinates onto the Nyquist plane.

---

## 7. Iterative Debugging Processes

Throughout development, several integration issues were identified and resolved:

### 7.1. Empty Plot Rendering in the Python UI
* **Symptom**: The Python GUI connected to the MCU serial port, but the live graph remained empty.
* **Root Cause**: The UI parser expected strict tag prefixes (e.g. `CA,x,y`) to map coordinate points. The older firmware output raw un-tagged values or logs that caused parsing exceptions.
* **Solution**: Standardized all technique print logs (e.g., `CA,%.4f,%.4e`, `SWV,%.2f,%.4e`) and updated the Tkinter UI parsing regular expressions in `hunstat2_test_ui.py`.

### 7.2. SPI Bus and Chip ID Invalidation
* **Symptom**: The AFE failed to initialize; calling the read-chip-ID function returned `0x0000` or `0xFFFF`.
* **Root Cause**: Poor SPI bus signal integrity or incorrect pin mapping.
* **Solution**: Introduced a command line register probe `I 0x0404`. If `CHIPID` reads `0x0000` or `0xFFFF`, the firmware isolates it as a wiring/power supply error, preventing software debugging of non-responsive hardware.

### 7.3. High-Frequency Clock Phase Shift in EIS
* **Symptom**: Distortion in EIS Nyquist plots at frequencies above $80\text{ kHz}$.
* **Solution**: Configured the code to automatically switch the internal system clock to 32MHz mode (`HfOSC32MHzMode = bTRUE`) and activate High-Power AFE mode (`AD5940_HPModeEn(bTRUE)`) when frequency exceeds $80\text{ kHz}$.

---

## 8. Expected vs. Actual Results

Here is a summary of expected results for a Randles cell model (standard electrical equivalent model of an electrochemical cell):

| Method | Expected Plot Profile | Physical Significance |
|---|---|---|
| **CV** | Duck-shaped voltammetric curve | Shows oxidation and reduction peaks. Peak separation indicates reaction reversibility. |
| **EIS** | Nyquist plot: Semi-circle at high frequencies, straight line ($45^\circ$ Warburg impedance) at low frequencies | Semi-circle diameter represents charge transfer resistance ($R_{ct}$). Intercept represents solution resistance ($R_s$). |
| **CA** | Cottrell decay curve ($I \propto t^{-1/2}$) | Demonstrates diffusion-controlled mass transport at the electrode surface. |
| **SWV / DPV** | Bell-shaped differential current peak | Provides high sensitivity by subtracting charging currents, isolating faradaic reaction peaks. |

---

## 9. Conclusion and Future Recommendations

The modular refactoring of the HunStat2 firmware has successfully separated concerns, creating a maintainable, extensible code base. The implementation of the Python Test Console allows rapid testing via both simulated dummy profiles and real-time board measurements.

### Recommendations for Future Work:
1. **Calibration Automation**: Implement automatic $R_{cal}$ sweeps before each EIS run to correct for ambient temperature drifts.
2. **Metadata Integration**: Include parameters (step size, amplitude, date/time) in the header of exported CSV files.
3. **Firmware Hardening**: Add watchdog timer resets in the main loop to recover from communication hangs during long OCP monitoring sessions.
