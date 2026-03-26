# STM32F411 FanTestKit

A FanTestKit with I2C slave + USB CDC + USB HID interfaces built on the STM32F411CEUx (WeAct BlackPill or similar). Measures frequency, period, duty cycle, and pulse width using TIM2 input capture in PWM Input mode. Also provides two independent PWM outputs with auto-prescaler for maximum resolution. A host MCU reads measurements, configures settings, and controls PWM outputs over I2C, USB CDC text console (SCPI), or USB HID binary register access. All configuration is persistable to flash. Runs on FreeRTOS.

## Hardware

| Pin | Function |
|-----|----------|
| PA15 | Signal input (TIM2_CH1) |
| PA8 | PWM output 1 (TIM1_CH1) |
| PB6 | PWM output 2 (TIM4_CH1) |
| PA7 | Trigger pulse output (fires on PWM parameter apply) |
| PB7 | I2C1 SDA |
| PB8 | I2C1 SCL |
| PC13 | Status LED (active-low, blinks at 1 Hz default) |
| PC14 | Green LED (active-high, configurable blink) |
| PB10 | Red LED (active-high, configurable blink) |

- MCU: STM32F411CEUx, 96 MHz (HSE 25 MHz + PLL)
- TIM2 timer clock: 96 MHz (default prescaler = 0)
- I2C1: 400 kHz fast mode, 7-bit addressing
- USB: Composite device (CDC + HID), VID `0x0483`, PID `0x5741`, product "FanTestKit-411 USB Composite"

> **Note:** External pull-up resistors (e.g., 4.7k) are required on SDA/PB7 and SCL/PB8 since the GPIOs are configured as open-drain with no internal pull-ups.

## I2C Protocol

**Slave address: 0x08** (7-bit)

### Register Map

| Address | Name | Size | Access | Description |
|---------|------|------|--------|-------------|
| 0x00 | PERIOD | 4 bytes | Read | Signal period in timer ticks |
| 0x04 | FREQ | 4 bytes | Read | Calculated frequency in Hz |
| 0x08 | DUTY | 4 bytes | Read | Duty cycle in 0.01% units (5000 = 50.00%) |
| 0x0C | PULSE | 4 bytes | Read | Pulse width (high-time) in timer ticks |
| 0x10 | EDGE | 1 byte | Read/Write | Capture edge: 0 = rising/high-time duty (default), 1 = falling/low-time duty |
| 0x11 | TIM_PSC | 2 bytes | Read/Write | Timer prescaler, 0-65535 (default: 0) |
| 0x13 | IC_PSC | 1 byte | Read/Write | Input capture prescaler: 0=DIV1, 1=DIV2, 2=DIV4, 3=DIV8 |
| 0x14 | CAPTURE_CTRL | 1 byte | Read/Write | Capture enable: 0 = off, 1 = on (default: 1) |
| 0x15–0x1F | *(reserved)* | 11 bytes | Read | Zero-filled |
| 0x20 | LED_PERIOD | 2 bytes | Read/Write | Status LED (PC13) blink period in ms (default: 1000) |
| 0x22 | LED_DUTY | 1 byte | Read/Write | Status LED on-duty in % 0-100 (default: 50) |
| 0x23 | LED_G_PERIOD | 2 bytes | Read/Write | Green LED (PC14) blink period in ms (default: 1000) |
| 0x25 | LED_G_DUTY | 1 byte | Read/Write | Green LED on-duty in % 0-100 (default: 50) |
| 0x26 | LED_R_PERIOD | 2 bytes | Read/Write | Red LED (PB10) blink period in ms (default: 1000) |
| 0x28 | LED_R_DUTY | 1 byte | Read/Write | Red LED on-duty in % 0-100 (default: 50) |
| 0x30 | SAVE_CFG | 1 byte | Write | Write 0x5A to save all config to flash |
| 0x40 | PWM1_FREQ_L | 2 bytes | Read/Write | PWM1 target frequency, low 16 bits (Hz) |
| 0x42 | PWM1_FREQ_H | 2 bytes | Read/Write | PWM1 target frequency, high 16 bits (Hz) |
| 0x44 | PWM1_DUTY | 2 bytes | Read/Write | PWM1 duty in 0.01% units (0–10000) |
| 0x46 | PWM1_CTRL | 1 byte | Read/Write | bit0=enable; writing applies staged values |
| 0x47 | PWM1_PSC | 2 bytes | Read | Auto-computed prescaler |
| 0x49 | PWM1_ARR | 2 bytes | Read | Auto-computed auto-reload value |
| 0x4B | PWM2_FREQ_L | 2 bytes | Read/Write | PWM2 target frequency, low 16 bits (Hz) |
| 0x4D | PWM2_FREQ_H | 2 bytes | Read/Write | PWM2 target frequency, high 16 bits (Hz) |
| 0x4F | PWM2_DUTY | 2 bytes | Read/Write | PWM2 duty in 0.01% units (0–10000) |
| 0x51 | PWM2_CTRL | 1 byte | Read/Write | bit0=enable; writing applies staged values |
| 0x52 | PWM2_PSC | 2 bytes | Read | Auto-computed prescaler |
| 0x54 | PWM2_ARR | 2 bytes | Read | Auto-computed auto-reload value |
| 0x56 | TRIG_WIDTH | 2 bytes | Read/Write | Trigger pulse width in µs (1–1000, default: 10) |
| 0x58–0x5F | *(reserved)* | 8 bytes | Read | Zero-filled |
| 0x60 | NICKNAME | 16 bytes | Read/Write | Device nickname, NUL-padded ASCII (default: serial number) |

All multi-byte values are **little-endian** (native ARM byte order).

Timer clock = 96,000,000 / (TIM_PSC + 1). With default TIM_PSC=0, each tick ≈ 10.4 ns.

### Persistent Configuration

All writable registers can be saved to internal flash by writing `0x5A` to the SAVE_CFG register. Saved settings are automatically restored on power-up — including PWM outputs, which resume automatically if enabled when saved, and capture state. Configuration is stored in flash sector 7 (0x08060000) with a magic number (`0xDEADBEF5`) for validation.

**Items saved to flash:**

| # | Item | Register | Description |
|---|------|----------|-------------|
| 1 | EDGE | 0x10 | Capture edge (rising/falling) |
| 2 | TIM_PSC | 0x11 | Timer prescaler (0–65535) |
| 3 | IC_PSC | 0x13 | Input capture prescaler (DIV1–DIV8) |
| 4 | CAPTURE_CTRL | 0x14 | Capture enable/disable |
| 5 | LED_PERIOD | 0x20 | Status LED (PC13) blink period (ms) |
| 6 | LED_DUTY | 0x22 | Status LED on-duty (%) |
| 7 | LED_G_PERIOD | 0x23 | Green LED (PC14) blink period (ms) |
| 8 | LED_G_DUTY | 0x25 | Green LED on-duty (%) |
| 9 | LED_R_PERIOD | 0x26 | Red LED (PB10) blink period (ms) |
| 10 | LED_R_DUTY | 0x28 | Red LED on-duty (%) |
| 11 | PWM1_FREQ_L | 0x40 | PWM1 frequency low 16 bits (Hz) |
| 12 | PWM1_FREQ_H | 0x42 | PWM1 frequency high 16 bits (Hz) |
| 13 | PWM1_DUTY | 0x44 | PWM1 duty cycle (0–10000, 0.01% units) |
| 14 | PWM1_CTRL | 0x46 | PWM1 enable/disable |
| 15 | PWM2_FREQ_L | 0x4B | PWM2 frequency low 16 bits (Hz) |
| 16 | PWM2_FREQ_H | 0x4D | PWM2 frequency high 16 bits (Hz) |
| 17 | PWM2_DUTY | 0x4F | PWM2 duty cycle (0–10000, 0.01% units) |
| 18 | PWM2_CTRL | 0x51 | PWM2 enable/disable |
| 19 | TRIG_WIDTH | 0x56 | Trigger pulse width (1–1000 µs) |
| 20 | NICKNAME | 0x60 | Device nickname (16 bytes, NUL-padded ASCII) |

### Reading Registers

Write the register address, then read the data bytes. Burst reads are supported — you can read multiple consecutive registers in a single transaction:

```
START → 0x08 W → [reg_addr] → RESTART → 0x08 R → [data bytes...] → NACK → STOP
```

For example, reading 16 bytes from address 0x00 returns PERIOD + FREQ + DUTY + PULSE in one transaction. Registers 0x15–0x1F are reserved (read as zero).

### Writing a Register

Write the register address followed by the data:

```
START → 0x08 W → [reg_addr] [data bytes...] → STOP
```

### PWM Outputs

Two independent PWM outputs (PA8 and PB6) are controlled via a staging protocol:

1. Write `FREQ_L` and `FREQ_H` to set the target frequency in Hz (32-bit, split into two 16-bit registers)
2. Write `DUTY` to set the duty cycle (0–10000 in 0.01% units)
3. Write `CTRL` with bit0=1 to **apply** the staged values atomically and enable output

Only writing `CTRL` changes the hardware output. The firmware auto-computes the optimal prescaler (PSC) and auto-reload value (ARR) to maximize duty cycle resolution. Both timers run at 96 MHz (APB1) / 96 MHz (APB2). A trigger pulse is output on PA7 each time `CTRL` is written.

**Example: set PWM1 to 1 kHz, 50% duty:**
```
Write 0x40, [0xE8, 0x03]   → FREQ_L = 1000 (low 16 bits)
Write 0x42, [0x00, 0x00]   → FREQ_H = 0 (high 16 bits)
Write 0x44, [0x88, 0x13]   → DUTY = 5000 (50.00%)
Write 0x46, 0x01            → enable + apply
```

See [docs/HOST_PROGRAMMING_GUIDE.md](docs/HOST_PROGRAMMING_GUIDE.md) for detailed examples in Arduino, Python, C#, STM32 HAL, and MicroPython.

### Host Examples

**Arduino:**
```cpp
#include <Wire.h>

#define FANTEST_ADDR 0x08

uint32_t readRegister32(uint8_t reg) {
    Wire.beginTransmission(FANTEST_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);  // repeated start
    Wire.requestFrom(FANTEST_ADDR, 4);
    uint32_t val = 0;
    for (int i = 0; i < 4 && Wire.available(); i++) {
        val |= (uint32_t)Wire.read() << (i * 8);
    }
    return val;
}

void writeRegister8(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(FANTEST_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

void setup() {
    Serial.begin(115200);
    Wire.begin();
}

void loop() {
    uint32_t freq   = readRegister32(0x04);  // FREQ register
    uint32_t duty   = readRegister32(0x08);  // DUTY register
    uint32_t period = readRegister32(0x00);  // PERIOD register

    Serial.print("Freq: "); Serial.print(freq); Serial.println(" Hz");
    Serial.print("Duty: "); Serial.print(duty / 100.0, 2); Serial.println(" %");
    Serial.print("Period: "); Serial.print(period); Serial.println(" ticks");
    Serial.println();

    delay(500);
}
```

**Python (Raspberry Pi / MicroPython):**
```python
import struct
from machine import I2C, Pin  # MicroPython
# or: import smbus2  # CPython on Raspberry Pi

ADDR = 0x08

# MicroPython
i2c = I2C(0, scl=Pin(1), sda=Pin(0), freq=400000)

def read_reg32(reg):
    i2c.writeto(ADDR, bytes([reg]), False)
    data = i2c.readfrom(ADDR, 4)
    return struct.unpack('<I', data)[0]

def write_reg8(reg, val):
    i2c.writeto(ADDR, bytes([reg, val]))

freq = read_reg32(0x04)
duty = read_reg32(0x08)
print(f"Frequency: {freq} Hz, Duty: {duty/100:.2f}%")

# Switch to falling edge capture
write_reg8(0x10, 1)
```

**C# (burst read — frequency + duty):**
```csharp
const byte ADDR = 0x08;

// Write register address 0x04 (FREQ), then read 8 bytes (FREQ + DUTY)
i2c.Write(ADDR, new byte[] { 0x04 });
byte[] data = i2c.Read(ADDR, 8);

uint freq = BitConverter.ToUInt32(data, 0);       // FREQ register
uint dutyCenti = BitConverter.ToUInt32(data, 4);  // DUTY register
double dutyPercent = dutyCenti / 100.0;

Console.WriteLine($"Frequency: {freq} Hz, Duty: {dutyPercent:F2} %");
```

## USB Interfaces

The device enumerates as a USB composite device with two interfaces:

**CDC** (virtual COM port): Text-based SCPI command console. See [docs/CDC_programming_guide.md](docs/CDC_programming_guide.md) for the full command reference.

**HID** (vendor-defined): 64-byte binary reports for direct register read/write access. Uses the same register map as I2C.

### PWM Dashboard (Test GUI)

A Python/Tkinter GUI tool is included for interactive testing:

```bash
pip install pyserial
python tools/pwm_dashboard.py
```

Features: PWM output control, real-time measurement display, capture edge selection, device nickname editing, multi-device support, and a CDC console for raw SCPI commands. See [docs/PWM_Dashboard_User_Guide.md](docs/PWM_Dashboard_User_Guide.md) for details.

## Measurement Specifications

| Parameter | Value |
|-----------|-------|
| Frequency range | ~2.3 Hz to ~48 MHz (with default PSC=0) |
| Period resolution | ~10.4 ns per tick (at 96 MHz timer clock) |
| Duty resolution | 0.01% |
| No-signal timeout | 1 second (all registers read 0) |
| Minimum measurable period | 2 timer ticks (50 MHz max input) |
| Maximum measurable period | 2^32 ticks = ~44.7 seconds (at 96 MHz) |

## PWM Output Specifications

| Parameter | Value |
|-----------|-------|
| Outputs | 2 independent channels: PA8 (TIM1) and PB6 (TIM4) |
| Frequency range | ~2 Hz to ~50 MHz (auto-prescaler) |
| Duty resolution | Depends on frequency (ARR+1 steps, maximized by auto-prescaler) |
| Update method | Glitch-free: preload registers + forced update event |
| Trigger output | PA7 positive pulse on each CTRL write (default 10 µs, configurable 1–1000 µs) |

### Prescaler Usage

For very high frequency signals where resolution is sufficient, increase TIM_PSC to extend the measurable range or reduce interrupt rate. For example, TIM_PSC=99 gives a 1 MHz timer clock (1 us resolution).

The IC prescaler (DIV1/2/4/8) reduces capture interrupt rate for high-frequency signals. In slave-reset mode the counter still resets every signal cycle, so all measurement values remain single-cycle — no host-side compensation needed.

## Software Architecture

The firmware runs on **FreeRTOS** (v10.3.1, native API) with three application tasks:

| Task | Purpose |
|------|---------|
| UsbTask | Processes CDC commands and HID reports from event queue |
| LedTask | Manages blinking of all 3 LEDs with configurable period and duty cycle |
| MonitorTask | Detects signal loss (1s timeout) and zeros measurement registers |

ISR callbacks (TIM2 input capture, I2C slave protocol) run at high NVIC priority and do not use any FreeRTOS API — they update shared volatile globals directly.

## Building

Requires **STM32CubeIDE**. Import the project and build the Debug or Release configuration.

The CubeMX configuration file is `FanTestKit.ioc`. After regenerating code from CubeMX, re-apply FreeRTOS handler changes in `stm32f4xx_it.c` (see CLAUDE.md for details).

## License

This project uses STM32 HAL drivers licensed under the ST SLA0044 license. See the LICENSE file in the Drivers directory for details.
