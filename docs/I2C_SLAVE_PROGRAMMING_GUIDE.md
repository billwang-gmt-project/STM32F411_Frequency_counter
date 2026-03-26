# I2C Slave Programming Guide — STM32F411 FanTestKit

## Overview

The STM32F411 FanTestKit provides an I2C slave interface for register-based control. A host microcontroller (Arduino, STM32, Raspberry Pi, ESP32, etc.) connects via I2C and reads measurement data, writes configuration parameters, or controls two independent PWM outputs.

> **Note:** The device also supports USB CDC (text console) and USB HID (binary register access) interfaces. This guide covers the I2C slave interface only.

**I2C address:** `0x08` (7-bit)
**Bus speed:** Standard (100 kHz) or Fast (400 kHz)
**Byte order:** Little-endian for all multi-byte values

---

## Hardware Connection

```
Host MCU                      STM32F411 FanTestKit
─────────                     ──────────────────────────
I2C SDA  ──────────┬──────── PB7  (SDA)
                   ├── 4.7kΩ ── 3.3V
I2C SCL  ──────────┬──────── PB8  (SCL)
                   ├── 4.7kΩ ── 3.3V
GND      ────────────────── GND

Signal input ──────────────── PA15
PWM output 1 ─────────────── PA8  (TIM1_CH1)
PWM output 2 ─────────────── PB6  (TIM4_CH1)
Trigger output ───────────── PA7  (pulse on PWM change)
```

- Pull-up resistors (2.2 kΩ–4.7 kΩ to 3.3 V) are required on both SDA and SCL.
- The signal under test connects to PA15 (5 V tolerant on this pin).
- PWM outputs (PA8, PB6) are 3.3 V push-pull. Trigger output (PA7) pulses high on each PWM apply.
- Share a common GND between host and FanTestKit.

---

## Register Map

| Address | Name | Size | Access | Description |
|---------|------|------|--------|-------------|
| `0x00` | PERIOD | 4 bytes | Read | Period in timer ticks |
| `0x04` | FREQ | 4 bytes | Read | Frequency in Hz |
| `0x08` | DUTY | 4 bytes | Read | Duty cycle in 0.01% units (5000 = 50.00%) |
| `0x0C` | PULSE | 4 bytes | Read | Pulse width in timer ticks |
| `0x10` | EDGE | 1 byte | Read/Write | Capture edge: `0` = rising, `1` = falling |
| `0x11` | TIM_PSC | 2 bytes | Read/Write | Timer prescaler (0–65535) |
| `0x13` | IC_PSC | 1 byte | Read/Write | Input capture prescaler (0–3) |
| `0x14` | CAPTURE_CTRL | 1 byte | Read/Write | Capture enable: `0` = off, `1` = on (default: 1) |
| `0x15–0x1F` | *(reserved)* | 11 bytes | Read | Zero-filled |
| `0x20` | LED_PERIOD | 2 bytes | Read/Write | Status LED (PC13) blink period in ms |
| `0x22` | LED_DUTY | 1 byte | Read/Write | Status LED on-duty percentage (0–100) |
| `0x23` | LED_G_PERIOD | 2 bytes | Read/Write | Green LED (PC14) blink period in ms |
| `0x25` | LED_G_DUTY | 1 byte | Read/Write | Green LED on-duty percentage (0–100) |
| `0x26` | LED_R_PERIOD | 2 bytes | Read/Write | Red LED (PB10) blink period in ms |
| `0x28` | LED_R_DUTY | 1 byte | Read/Write | Red LED on-duty percentage (0–100) |
| `0x30` | SAVE_CFG | 1 byte | Write only | Write `0x5A` to save config to flash |
| `0x31–0x3F` | *(reserved)* | 15 bytes | Read | Zero-filled |
| `0x40` | PWM1_FREQ_L | 2 bytes | Read/Write | PWM1 target frequency, low 16 bits (Hz) |
| `0x42` | PWM1_FREQ_H | 2 bytes | Read/Write | PWM1 target frequency, high 16 bits (Hz) |
| `0x44` | PWM1_DUTY | 2 bytes | Read/Write | PWM1 duty cycle in 0.01% units (0–10000) |
| `0x46` | PWM1_CTRL | 1 byte | Read/Write | bit0 = enable; **writing applies staged values** |
| `0x47` | PWM1_PSC | 2 bytes | Read | Auto-computed prescaler (for debug) |
| `0x49` | PWM1_ARR | 2 bytes | Read | Auto-computed auto-reload value (for debug) |
| `0x4B` | PWM2_FREQ_L | 2 bytes | Read/Write | PWM2 target frequency, low 16 bits (Hz) |
| `0x4D` | PWM2_FREQ_H | 2 bytes | Read/Write | PWM2 target frequency, high 16 bits (Hz) |
| `0x4F` | PWM2_DUTY | 2 bytes | Read/Write | PWM2 duty cycle in 0.01% units (0–10000) |
| `0x51` | PWM2_CTRL | 1 byte | Read/Write | bit0 = enable; **writing applies staged values** |
| `0x52` | PWM2_PSC | 2 bytes | Read | Auto-computed prescaler (for debug) |
| `0x54` | PWM2_ARR | 2 bytes | Read | Auto-computed auto-reload value (for debug) |
| `0x56` | TRIG_WIDTH | 2 bytes | Read/Write | Trigger pulse width in µs (1–1000, default: 10) |
| `0x58–0x5F` | *(reserved)* | 8 bytes | Read | Zero-filled |
| `0x60` | NICKNAME | 16 bytes | Read/Write | Device nickname, NUL-padded ASCII (default: serial number) |

Registers are contiguous in a 112-byte map (0x00–0x6F). **Burst reads** are supported — a single read can span multiple registers. The slave builds a snapshot and sends from the start address onward.

---

## I2C Transaction Protocol

### Reading Registers

A register read is a two-step I2C transaction using repeated start:

```
[START] [0x08 + W] [reg_addr] [REPEATED START] [0x08 + R] [data...] [NACK] [STOP]
```

**Burst reads:** You can read multiple consecutive registers in a single transaction. For example, reading 16 bytes from address `0x00` returns PERIOD + FREQ + DUTY + PULSE all at once. The master NACKs the last byte and issues STOP to end.

**Single register read (4 bytes):**
```
[START] [0x08+W] [0x04] [RS] [0x08+R] [b0] [b1] [b2] [b3 NACK] [STOP]
                  ^FREQ                 └── 4 bytes of FREQ ──┘
```

**Burst read (16 bytes from 0x00):**
```
[START] [0x08+W] [0x00] [RS] [0x08+R] [PERIOD 4B] [FREQ 4B] [DUTY 4B] [PULSE 4B NACK] [STOP]
```

### Writing a Register

A register write sends the register address followed by the data in a single transaction:

```
[START] [0x08 + W] [reg_addr] [data_0] [data_1] [STOP]
```

> **Important — fixed 2-byte data payload:** The I2C slave uses `HAL_I2C_Slave_Seq_Receive_IT(buf, 3)`, which expects exactly **3 bytes per write transaction** (1 address byte + 2 data bytes). For 1-byte registers (EDGE, IC_PSC, CAPTURE_CTRL, PWM_CTRL, SAVE_CFG, LED_DUTY), **always send 2 data bytes** — pad with `0x00`:
>
> ```
> [START] [0x08 + W] [reg_addr] [value] [0x00] [STOP]
> ```
>
> If only 1 data byte is sent, the slave's receive-complete callback will not fire and the write will be silently ignored.

---

## Understanding the Measurements

### Timer Clock

The base timer clock is **96 MHz**. With a prescaler `PSC`, the effective timer clock is:

```
timer_clock = 96,000,000 / (TIM_PSC + 1)
```

Default `TIM_PSC = 0` → timer clock = 96 MHz → tick resolution ≈ 10.4 ns.

### Converting Register Values

| Value | Formula | Example |
|-------|---------|---------|
| **Frequency** | `FREQ` register gives Hz directly | `FREQ = 1000` → 1 kHz |
| **Period (seconds)** | `PERIOD / timer_clock` | `PERIOD = 100000, PSC = 0` → 1 ms |
| **Duty cycle (%)** | `DUTY / 100.0` | `DUTY = 5000` → 50.00% |
| **Pulse width (seconds)** | `PULSE / timer_clock` | `PULSE = 50000, PSC = 0` → 500 µs |

In slave-reset mode the counter resets every signal cycle regardless of IC prescaler. All four registers (FREQ, DUTY, PERIOD, PULSE) always reflect single-cycle values — no host-side compensation is needed. The IC prescaler only reduces how often the capture interrupt fires.

### IC Prescaler Values

| IC_PSC | Meaning | Use case |
|--------|---------|----------|
| 0 | Every edge captured (DIV1) | Default, best for low frequencies |
| 1 | Every 2nd edge (DIV2) | Reduce IRQ rate for medium frequencies |
| 2 | Every 4th edge (DIV4) | High-frequency signals |
| 3 | Every 8th edge (DIV8) | Very high-frequency signals |

### Frequency Range

| TIM_PSC | Timer Clock | Min Freq (32-bit) | Max Freq | Tick Resolution |
|---------|-------------|-------------------|----------|-----------------|
| 0 | 96 MHz | ~0.022 Hz | 48 MHz* | ~10.4 ns |
| 9 | 9.6 MHz | ~0.002 Hz | 4.8 MHz* | ~104 ns |
| 95 | 1 MHz | ~0.0002 Hz | 500 kHz | 1 µs |
| 9599 | 10 kHz | N/A | 5 kHz | 100 µs |

\* Practical max frequency depends on signal quality and IC prescaler.

### No-Signal Timeout

If no edge is captured for **1 second**, all measurement registers (PERIOD, FREQ, DUTY, PULSE) are zeroed. Reading `FREQ = 0` indicates no signal is present.

---

## Code Examples

### Arduino (Wire Library)

```cpp
#include <Wire.h>

#define FANTEST_ADDR 0x08

// Read a 4-byte (uint32_t) register
uint32_t readReg32(uint8_t reg) {
    Wire.beginTransmission(FANTEST_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);  // repeated start

    Wire.requestFrom(FANTEST_ADDR, 4);
    uint32_t val = 0;
    if (Wire.available() == 4) {
        val  = (uint32_t)Wire.read();
        val |= (uint32_t)Wire.read() << 8;
        val |= (uint32_t)Wire.read() << 16;
        val |= (uint32_t)Wire.read() << 24;
    }
    return val;
}

// Read a 2-byte (uint16_t) register
uint16_t readReg16(uint8_t reg) {
    Wire.beginTransmission(FANTEST_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);

    Wire.requestFrom(FANTEST_ADDR, 2);
    uint16_t val = 0;
    if (Wire.available() == 2) {
        val  = (uint16_t)Wire.read();
        val |= (uint16_t)Wire.read() << 8;
    }
    return val;
}

// Read a 1-byte register
uint8_t readReg8(uint8_t reg) {
    Wire.beginTransmission(FANTEST_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);

    Wire.requestFrom(FANTEST_ADDR, 1);
    return Wire.available() ? Wire.read() : 0;
}

// Write a 1-byte register (must send 2 data bytes; slave expects 3-byte frame)
void writeReg8(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(FANTEST_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.write((uint8_t)0x00);  // padding byte — slave requires 2 data bytes
    Wire.endTransmission();
}

// Write a 2-byte register (little-endian)
void writeReg16(uint8_t reg, uint16_t value) {
    Wire.beginTransmission(FANTEST_ADDR);
    Wire.write(reg);
    Wire.write(value & 0xFF);
    Wire.write((value >> 8) & 0xFF);
    Wire.endTransmission();
}

// Write a 4-byte value as two 16-bit register writes (for PWM frequency)
void writeFreq(uint8_t regL, uint32_t freq_hz) {
    writeReg16(regL,     freq_hz & 0xFFFF);         // FREQ_L
    writeReg16(regL + 2, (freq_hz >> 16) & 0xFFFF); // FREQ_H
}

void setup() {
    Serial.begin(115200);
    Wire.begin();
    Wire.setClock(400000);  // 400 kHz Fast mode
}

void loop() {
    uint32_t freq = readReg32(0x04);
    uint32_t duty = readReg32(0x08);

    Serial.print("Freq: ");
    Serial.print(freq);
    Serial.print(" Hz, Duty: ");
    Serial.print(duty / 100.0, 2);
    Serial.println(" %");

    delay(500);
}

// --- PWM control example ---
// Set PWM1 (PA8) to 1 kHz, 50% duty
void setupPWM1() {
    writeFreq(0x40, 1000);         // 1000 Hz
    writeReg16(0x44, 5000);        // 50.00%
    writeReg8(0x46, 0x01);         // enable + apply

    // Read back auto-computed values
    uint16_t psc = readReg16(0x47);
    uint16_t arr = readReg16(0x49);
    Serial.print("PSC="); Serial.print(psc);
    Serial.print(" ARR="); Serial.println(arr);
}
```

### Raspberry Pi (Python, smbus2)

```python
import smbus2
import struct
import time

ADDR = 0x08
bus = smbus2.SMBus(1)  # /dev/i2c-1

def read_reg32(reg):
    """Read a 4-byte little-endian register."""
    bus.write_byte(ADDR, reg)
    data = bus.read_i2c_block_data(ADDR, reg, 4)
    return struct.unpack('<I', bytes(data))[0]

def read_reg16(reg):
    """Read a 2-byte little-endian register."""
    bus.write_byte(ADDR, reg)
    data = bus.read_i2c_block_data(ADDR, reg, 2)
    return struct.unpack('<H', bytes(data))[0]

def read_reg8(reg):
    """Read a 1-byte register."""
    bus.write_byte(ADDR, reg)
    return bus.read_byte(ADDR)

def write_reg8(reg, value):
    """Write a 1-byte register (padded to 2 bytes; slave expects 3-byte frame)."""
    bus.write_i2c_block_data(ADDR, reg, [value, 0x00])

def write_reg16(reg, value):
    """Write a 2-byte little-endian register."""
    data = struct.pack('<H', value)
    bus.write_i2c_block_data(ADDR, reg, list(data))

# --- Read all measurements ---
freq = read_reg32(0x04)
duty = read_reg32(0x08)
period = read_reg32(0x00)
pulse = read_reg32(0x0C)

print(f"Frequency: {freq} Hz")
print(f"Duty cycle: {duty / 100:.2f} %")
print(f"Period: {period} ticks")
print(f"Pulse width: {pulse} ticks")

# --- Change capture edge to falling ---
write_reg8(0x10, 1)

# --- Set timer prescaler to 9 (10 MHz clock) ---
write_reg16(0x11, 9)

# --- Save config to flash ---
write_reg8(0x30, 0x5A)

# --- PWM control ---
def write_freq(reg_l, freq_hz):
    """Write a 32-bit frequency as two 16-bit registers."""
    write_reg16(reg_l, freq_hz & 0xFFFF)
    write_reg16(reg_l + 2, (freq_hz >> 16) & 0xFFFF)

# Set PWM1 (PA8) to 10 kHz, 30% duty
write_freq(0x40, 10000)        # 10 kHz
write_reg16(0x44, 3000)        # 30.00%
write_reg8(0x46, 0x01)         # enable + apply → trigger pulse on PA7

# Set PWM2 (PB6) to 440 Hz, 50% duty
write_freq(0x4B, 440)          # 440 Hz
write_reg16(0x4F, 5000)        # 50.00%
write_reg8(0x51, 0x01)         # enable + apply

# Read back auto-computed prescaler and ARR
psc = read_reg16(0x47)
arr = read_reg16(0x49)
print(f"PWM1: PSC={psc}, ARR={arr}")

# Disable PWM1
write_reg8(0x46, 0x00)

# Save all settings (including PWM) to flash
write_reg8(0x30, 0x5A)
```

### STM32 HAL (Host MCU)

```c
#include "i2c.h"
#include <string.h>

#define FC_ADDR  (0x08 << 1)  // HAL uses 8-bit address

// Read a 4-byte register
uint32_t FC_ReadReg32(uint8_t reg) {
    uint8_t buf[4];
    HAL_I2C_Mem_Read(&hi2c1, FC_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                     buf, 4, 100);
    uint32_t val;
    memcpy(&val, buf, 4);
    return val;
}

// Read a 2-byte register
uint16_t FC_ReadReg16(uint8_t reg) {
    uint8_t buf[2];
    HAL_I2C_Mem_Read(&hi2c1, FC_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                     buf, 2, 100);
    uint16_t val;
    memcpy(&val, buf, 2);
    return val;
}

// Read a 1-byte register
uint8_t FC_ReadReg8(uint8_t reg) {
    uint8_t val;
    HAL_I2C_Mem_Read(&hi2c1, FC_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                     &val, 1, 100);
    return val;
}

// Write a 1-byte register (must send 2 data bytes; slave expects 3-byte frame)
void FC_WriteReg8(uint8_t reg, uint8_t value) {
    uint8_t buf[2] = { value, 0x00 };
    HAL_I2C_Mem_Write(&hi2c1, FC_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                      buf, 2, 100);
}

// Write a 2-byte register
void FC_WriteReg16(uint8_t reg, uint16_t value) {
    uint8_t buf[2];
    memcpy(buf, &value, 2);
    HAL_I2C_Mem_Write(&hi2c1, FC_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                      buf, 2, 100);
}

// Write a 32-bit frequency as two 16-bit register writes
void FC_WriteFreq(uint8_t regL, uint32_t freq_hz) {
    FC_WriteReg16(regL, freq_hz & 0xFFFF);
    FC_WriteReg16(regL + 2, (freq_hz >> 16) & 0xFFFF);
}

// Usage example — read measurements
void ReadFanTestKit(void) {
    uint32_t freq = FC_ReadReg32(0x04);
    uint32_t duty = FC_ReadReg32(0x08);

    // freq is in Hz
    // duty is in 0.01% units, divide by 100.0 for percent
}

// Usage example — set PWM1 to 5 kHz, 75% duty
void SetupPWM1(void) {
    FC_WriteFreq(0x40, 5000);      // 5 kHz
    FC_WriteReg16(0x44, 7500);     // 75.00%
    FC_WriteReg8(0x46, 0x01);      // enable + apply
}
```

### ESP32 (Arduino Framework)

```cpp
#include <Wire.h>

#define FANTEST_ADDR 0x08
#define SDA_PIN 21
#define SCL_PIN 22

uint32_t readReg32(uint8_t reg) {
    Wire.beginTransmission(FANTEST_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);

    Wire.requestFrom((uint8_t)FANTEST_ADDR, (uint8_t)4);
    uint32_t val = 0;
    for (int i = 0; i < 4 && Wire.available(); i++) {
        val |= (uint32_t)Wire.read() << (8 * i);
    }
    return val;
}

void setup() {
    Serial.begin(115200);
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(400000);
}

void loop() {
    uint32_t freq = readReg32(0x04);
    uint32_t duty = readReg32(0x08);
    Serial.printf("Freq: %u Hz, Duty: %.2f%%\n", freq, duty / 100.0);
    delay(500);
}
```

### MicroPython (Raspberry Pi Pico / ESP32)

```python
from machine import I2C, Pin
import struct
import time

i2c = I2C(0, scl=Pin(1), sda=Pin(0), freq=400_000)

ADDR = 0x08

def read_reg32(reg):
    i2c.writeto(ADDR, bytes([reg]))
    data = i2c.readfrom(ADDR, 4)
    return struct.unpack('<I', data)[0]

def read_reg16(reg):
    i2c.writeto(ADDR, bytes([reg]))
    data = i2c.readfrom(ADDR, 2)
    return struct.unpack('<H', data)[0]

def read_reg8(reg):
    i2c.writeto(ADDR, bytes([reg]))
    return i2c.readfrom(ADDR, 1)[0]

def write_reg8(reg, val):
    i2c.writeto(ADDR, bytes([reg, val, 0x00]))  # pad to 2 data bytes

def write_reg16(reg, val):
    i2c.writeto(ADDR, bytes([reg]) + struct.pack('<H', val))

def write_freq(reg_l, freq_hz):
    write_reg16(reg_l, freq_hz & 0xFFFF)
    write_reg16(reg_l + 2, (freq_hz >> 16) & 0xFFFF)

# Read measurements
while True:
    freq = read_reg32(0x04)
    duty = read_reg32(0x08)
    print(f"Freq: {freq} Hz, Duty: {duty/100:.2f}%")
    time.sleep(0.5)

# PWM example: set PWM1 to 1 kHz, 50%
# write_freq(0x40, 1000)
# write_reg16(0x44, 5000)
# write_reg8(0x46, 0x01)
```

### C# (Burst Read — Frequency + Duty)

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

### C# (Burst Read — All Measurements)

```csharp
// Read all 4 measurement registers at once (16 bytes from 0x00)
i2c.Write(ADDR, new byte[] { 0x00 });
byte[] data = i2c.Read(ADDR, 16);

uint period = BitConverter.ToUInt32(data, 0);     // PERIOD (ticks)
uint freq   = BitConverter.ToUInt32(data, 4);     // FREQ (Hz)
uint duty   = BitConverter.ToUInt32(data, 8);     // DUTY (0.01% units)
uint pulse  = BitConverter.ToUInt32(data, 12);    // PULSE (ticks)

double dutyPercent = duty / 100.0;
double periodUs = period * (1.0 / 96.0);  // at 96 MHz, 1 tick ≈ 10.4 ns

Console.WriteLine($"Freq: {freq} Hz");
Console.WriteLine($"Duty: {dutyPercent:F2} %");
Console.WriteLine($"Period: {periodUs:F2} us");
Console.WriteLine($"Pulse: {pulse * (1.0 / 96.0):F2} us");
```

### C# (PWM Control)

```csharp
// Helper: write a 32-bit frequency as two 16-bit register writes
void WriteFreq(byte regL, uint freqHz) {
    byte[] lo = BitConverter.GetBytes((ushort)(freqHz & 0xFFFF));
    byte[] hi = BitConverter.GetBytes((ushort)(freqHz >> 16));
    i2c.Write(ADDR, new byte[] { regL, lo[0], lo[1] });
    i2c.Write(ADDR, new byte[] { (byte)(regL + 2), hi[0], hi[1] });
}

// Set PWM1 (PA8) to 1 kHz, 50% duty
WriteFreq(0x40, 1000);
byte[] duty = BitConverter.GetBytes((ushort)5000);
i2c.Write(ADDR, new byte[] { 0x44, duty[0], duty[1] });  // DUTY = 5000
i2c.Write(ADDR, new byte[] { 0x46, 0x01, 0x00 });         // CTRL = enable + apply (padded to 2 data bytes)

// Read back auto-computed PSC and ARR
i2c.Write(ADDR, new byte[] { 0x47 });
byte[] pscArr = i2c.Read(ADDR, 4);
ushort psc = BitConverter.ToUInt16(pscArr, 0);
ushort arr = BitConverter.ToUInt16(pscArr, 2);
Console.WriteLine($"PSC={psc}, ARR={arr}");
```

---

## Common Operations

> **Reminder:** All write examples below use shorthand notation. For 1-byte registers, remember to pad the data to 2 bytes (e.g., `Write 0x10, 0x01` means sending `[0x10, 0x01, 0x00]` on the bus).

### 1. Read All Measurements (Burst Read)

Read 16 bytes from register `0x00` in a single transaction:

```
Write 0x00, then read 16 bytes →
  bytes  0–3:  PERIOD (uint32, ticks)
  bytes  4–7:  FREQ   (uint32, Hz)
  bytes  8–11: DUTY   (uint32, 0.01% units)
  bytes 12–15: PULSE  (uint32, ticks)
```

**Decoding (little-endian):**
```
Example raw: 9C 82 01 00  F2 03 00 00  87 13 00 00  45 C1 00 00

PERIOD = 0x0001829C = 99,996 ticks
FREQ   = 0x000003F2 = 1,010 Hz
DUTY   = 0x00001387 = 4,999 → 49.99%
PULSE  = 0x0000C145 = 49,477 ticks
```

### 2. Check for No Signal

```
Read 0x04 → if FREQ == 0, no signal is present (timeout after 1 second)
```

### 3. Switch to Falling Edge Capture

```
Write 0x10, 0x01   → capture on falling edge
```

To revert to rising edge:

```
Write 0x10, 0x00   → capture on rising edge
```

### 4. Disable / Enable Capture

Disable input capture (all measurements read zero, TIM2 interrupts stop):

```
Write 0x14, 0x00   → capture off
```

Re-enable capture:

```
Write 0x14, 0x01   → capture on
```

Configuration changes (edge, prescalers) made while capture is disabled take effect when re-enabled.

### 5. Measure Low Frequencies (< 1 Hz)

Increase the timer prescaler to extend the 32-bit counter range:

```
Write 0x11, [PSC_lo, PSC_hi]   → set prescaler (little-endian)
```

Example: PSC = 95 (timer clock = 1 MHz, max period = 4294 seconds)

```
Write 0x11, [0x5F, 0x00]
```

### 6. Measure High Frequencies (> 1 MHz)

Use the IC prescaler to reduce interrupt rate:

```
Write 0x13, 0x03   → capture every 8th edge (DIV8)
```

**Note:** In slave-reset mode the counter resets every signal cycle regardless of IC prescaler. All measurement registers (FREQ, DUTY, PERIOD, PULSE) always reflect single-cycle values — no host-side compensation is needed. The IC prescaler only reduces interrupt rate.

### 7. Read / Write Device Nickname

The 16-byte nickname at register `0x60` identifies the device in multi-device setups. Default is the serial number hex string.

**Read nickname (16 bytes):**
```
Read 0x60, 16 bytes → "BENCH-1\0\0\0\0\0\0\0\0\0" (NUL-padded)
```

**Write nickname via I2C (2 bytes per transaction):**
```
Write 0x60, [0x44, 0x55]   → nickname[0-1] = "DU"
Write 0x62, [0x54, 0x2D]   → nickname[2-3] = "T-"
Write 0x64, [0x31, 0x00]   → nickname[4-5] = "1\0"
Write 0x66, [0x00, 0x00]   → nickname[6-7] = "\0\0"
... (write zeros to remaining positions 0x68-0x6E)
```

**Note:** I2C writes 2 bytes at a time and do NOT auto-clear the remainder. To fully replace a nickname, write all 16 bytes (8 transactions). Save with `Write 0x30, 0x5A`.

### 8. Configure LEDs

Three LEDs are independently configurable:

| LED | Period Register | Duty Register | Pin | Active |
|-----|----------------|---------------|-----|--------|
| Status | `0x20` (2B) | `0x22` (1B) | PC13 | Low |
| Green | `0x23` (2B) | `0x25` (1B) | PC14 | High |
| Red | `0x26` (2B) | `0x28` (1B) | PB10 | High |

Example: fast blink on status LED (200 ms period, 25% on):

```
Write 0x20, [0xC8, 0x00]   → 200 ms
Write 0x22, 0x19            → 25%
```

Example: green LED steady on:

```
Write 0x23, [0x01, 0x00]   → 1 ms period
Write 0x25, 0x64            → 100% duty
```

### 9. Control PWM Outputs

Two independent PWM outputs are available:

| Output | Pin | Timer | Registers |
|--------|-----|-------|-----------|
| PWM1 | PA8 | TIM1_CH1 | `0x40`–`0x46` |
| PWM2 | PB6 | TIM4_CH1 | `0x4B`–`0x51` |

#### Staging Protocol

PWM uses a **staged write** pattern to allow glitch-free, atomic updates:

1. Write `FREQ_L` — stores low 16 bits of target frequency (Hz), no hardware change
2. Write `FREQ_H` — stores high 16 bits of target frequency (Hz), no hardware change
3. Write `DUTY` — stores duty cycle (0–10000 in 0.01% units), no hardware change
4. Write `CTRL` with bit0 = 1 — **applies** all staged values atomically and fires a trigger pulse on PA7

You can update any subset of registers before writing `CTRL`. Only `CTRL` triggers the hardware update.

#### Auto-Prescaler

The firmware automatically computes the optimal timer prescaler (PSC) and auto-reload value (ARR) to maximize duty cycle resolution. Both TIM1 and TIM4 run from a 96 MHz clock. You can read back the computed values from the PSC and ARR registers for debugging.

```
PWM frequency = 96,000,000 / ((PSC + 1) × (ARR + 1))
```

| Target Frequency | Computed PSC | Computed ARR | Duty Steps |
|-----------------|-------------|-------------|------------|
| 1 Hz | 1464 | 65,535 | 65,536 |
| 1 kHz | 1 | 47,999 | 48,000 |
| 10 kHz | 0 | 9,599 | 9,600 |
| 100 kHz | 0 | 959 | 960 |
| 1 MHz | 0 | 95 | 96 |

#### Example: Set PWM1 to 1 kHz, 50% Duty

```
Write 0x40, [0xE8, 0x03]   → FREQ_L = 0x03E8 (1000 low 16 bits)
Write 0x42, [0x00, 0x00]   → FREQ_H = 0x0000 (1000 high 16 bits)
Write 0x44, [0x88, 0x13]   → DUTY   = 0x1388 (5000 = 50.00%)
Write 0x46, 0x01            → CTRL   = enable + apply → PA7 trigger fires
```

#### Example: Change Duty to 25% (Keep Same Frequency)

```
Write 0x44, [0xC4, 0x09]   → DUTY = 0x09C4 (2500 = 25.00%)
Write 0x46, 0x01            → CTRL = apply → PA7 trigger fires
```

Only the changed register and CTRL need to be written. FREQ_L/H retain their previous values.

#### Example: Disable PWM1

```
Write 0x46, 0x00            → CTRL bit0 = 0 → output goes low
```

#### Example: Set PWM2 to 440 Hz, 75% Duty

```
Write 0x4B, [0xB8, 0x01]   → FREQ_L = 0x01B8 (440 low 16 bits)
Write 0x4D, [0x00, 0x00]   → FREQ_H = 0x0000
Write 0x4F, [0xD0, 0x1D]   → DUTY   = 0x1DD0 (7632 ≈ 76.32%... use 7500 = 0x1D4C)
Write 0x51, 0x01            → CTRL   = enable + apply
```

For 75.00% duty: `7500 = 0x1D4C` → `Write 0x4F, [0x4C, 0x1D]`

#### Setting Frequencies Above 65535 Hz

The 32-bit frequency is split across two 16-bit registers. For example, 100,000 Hz = `0x000186A0`:

```
Write 0x40, [0xA0, 0x86]   → FREQ_L = 0x86A0 (low 16 bits)
Write 0x42, [0x01, 0x00]   → FREQ_H = 0x0001 (high 16 bits)
Write 0x46, 0x01            → Apply
```

#### Reading PWM Status

```
Read 0x40 (2 bytes) → FREQ_L
Read 0x42 (2 bytes) → FREQ_H
Read 0x44 (2 bytes) → DUTY
Read 0x46 (1 byte)  → CTRL (bit0 = enabled)
Read 0x47 (2 bytes) → PSC (auto-computed prescaler)
Read 0x49 (2 bytes) → ARR (auto-computed auto-reload)
```

Or burst read 11 bytes from `0x40` to get all PWM1 registers at once.

#### Trigger Pulse (PA7)

Every time `CTRL` is written (for either PWM channel), a positive pulse is output on PA7. This can be used to trigger an oscilloscope or logic analyzer to capture the moment of change.

- Default pulse width: **10 µs**
- Configurable via register `0x56` (TRIG_WIDTH): 1–1000 µs
- Pulse timing is approximate (software loop)

```
Write 0x56, [0x32, 0x00]   → set trigger pulse width to 50 µs
```

### 10. Save Configuration to Flash

To persist the current edge, prescaler, LED, **PWM**, and **nickname** settings across power cycles:

```
Write 0x30, 0x5A   → trigger flash save
```

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

On next power-up, saved PWM outputs will automatically resume if CTRL bit0 was set when saved.

### 11. Read Back Current Configuration

Single reads:
```
Read 0x10 (1 byte)  → EDGE
Read 0x11 (2 bytes) → TIM_PSC
Read 0x13 (1 byte)  → IC_PSC
Read 0x14 (1 byte)  → CAPTURE_CTRL
Read 0x20 (2 bytes) → LED_PERIOD
Read 0x22 (1 byte)  → LED_DUTY
Read 0x23 (2 bytes) → LED_G_PERIOD
Read 0x25 (1 byte)  → LED_G_DUTY
Read 0x26 (2 bytes) → LED_R_PERIOD
Read 0x28 (1 byte)  → LED_R_DUTY
Read 0x40 (2 bytes) → PWM1_FREQ_L
Read 0x42 (2 bytes) → PWM1_FREQ_H
Read 0x44 (2 bytes) → PWM1_DUTY
Read 0x46 (1 byte)  → PWM1_CTRL
Read 0x47 (2 bytes) → PWM1_PSC (auto-computed)
Read 0x49 (2 bytes) → PWM1_ARR (auto-computed)
Read 0x4B (2 bytes) → PWM2_FREQ_L
Read 0x4D (2 bytes) → PWM2_FREQ_H
Read 0x4F (2 bytes) → PWM2_DUTY
Read 0x51 (1 byte)  → PWM2_CTRL
Read 0x52 (2 bytes) → PWM2_PSC (auto-computed)
Read 0x54 (2 bytes) → PWM2_ARR (auto-computed)
Read 0x56 (2 bytes) → TRIG_WIDTH
```

Or burst read 9 bytes from `0x20` to get all LED config, or 11 bytes from `0x40` for all PWM1 config.

---

## Timing Considerations

- **After writing a config register** (EDGE, TIM_PSC, IC_PSC, CAPTURE_CTRL): The firmware immediately reconfigures the timer. Measurements are cleared (zeroed) and will populate once new edges arrive (if capture is enabled). Allow at least one signal period before reading.
- **After writing PWM CTRL:** The PWM output changes within microseconds. The trigger pulse on PA7 fires immediately after the update. No wait is needed between CTRL write and the next I2C transaction.
- **PWM staging registers** (FREQ_L, FREQ_H, DUTY): These can be written in any order at any speed. The hardware only changes when CTRL is written.
- **After saving config** (`0x30 = 0x5A`): Flash erase+write takes a few milliseconds. During this time, I2C may not respond. Wait **~10 ms** before the next I2C transaction.
- **Polling rate:** There is no minimum interval between reads. The firmware updates measurements on every capture edge. A polling rate of 10–100 Hz is typical.
- **I2C clock stretching:** The slave may stretch the clock briefly during reads while copying measurement data. Ensure your host I2C master supports clock stretching.

---

## Troubleshooting

| Symptom | Possible Cause | Solution |
|---------|---------------|----------|
| No ACK from slave | Wrong address, wiring, no pull-ups | Verify 0x08, check SDA/SCL/GND, add pull-ups |
| FREQ always 0 | No signal on PA15, signal timeout | Check signal input, verify PA15 connection |
| Duty reads 0 or 10000 | DC signal (always high or low) | Not a periodic signal — expected behavior |
| Wrong frequency reading | Prescaler mismatch | Read back TIM_PSC to verify current setting |
| Measurements jump erratically | Noisy signal, missing pull-ups | Add input filtering, verify signal integrity |
| Config lost after power cycle | Forgot to save | Write `0x5A` to register `0x30` after configuring |
| I2C hangs | Bus stuck, missing pull-ups | Power cycle both devices, check pull-up resistors |
| Reads return stale data | Polling too fast after reconfig | Wait at least one signal period after config change |
| PWM output stays off | CTRL not written after FREQ/DUTY | Write CTRL with bit0=1 to apply staged values |
| Write to 1-byte register ignored | Only 1 data byte sent | Slave expects 2 data bytes per write; pad with `0x00` (see "Writing a Register") |
| PWM frequency incorrect | Only FREQ_L written, FREQ_H stale | Always write both FREQ_L and FREQ_H before CTRL |
| PWM PSC/ARR read as 0 | Frequency out of range or disabled | Check CTRL bit0; verify frequency is 2 Hz–50 MHz |
| No trigger pulse on PA7 | CTRL not written, or pulse too short | Write CTRL to trigger; increase TRIG_WIDTH if needed |
| PWM not restored after power cycle | Config not saved | Write `0x5A` to register `0x30` after configuring PWM |
