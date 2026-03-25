# Host Programming Guide — STM32F411 Frequency Counter (I2C Slave)

## Overview

The STM32F411 frequency counter operates as an I2C slave device. A host microcontroller (Arduino, STM32, Raspberry Pi, ESP32, etc.) connects via I2C and reads measurement data or writes configuration parameters through a register-based protocol.

**I2C address:** `0x08` (7-bit)
**Bus speed:** Standard (100 kHz) or Fast (400 kHz)
**Byte order:** Little-endian for all multi-byte values

---

## Hardware Connection

```
Host MCU                      STM32F411 Frequency Counter
─────────                     ──────────────────────────
I2C SDA  ──────────┬──────── PB7  (SDA)
                   ├── 4.7kΩ ── 3.3V
I2C SCL  ──────────┬──────── PB8  (SCL)
                   ├── 4.7kΩ ── 3.3V
GND      ────────────────── GND

Signal input ──────────────── PA15
```

- Pull-up resistors (2.2 kΩ–4.7 kΩ to 3.3 V) are required on both SDA and SCL.
- The signal under test connects to PA15 (5 V tolerant on this pin).
- Share a common GND between host and counter.

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
| `0x14–0x1F` | *(reserved)* | 12 bytes | Read | Zero-filled |
| `0x20` | LED_PERIOD | 2 bytes | Read/Write | Status LED (PC13) blink period in ms |
| `0x22` | LED_DUTY | 1 byte | Read/Write | Status LED on-duty percentage (0–100) |
| `0x23` | LED_G_PERIOD | 2 bytes | Read/Write | Green LED (PC14) blink period in ms |
| `0x25` | LED_G_DUTY | 1 byte | Read/Write | Green LED on-duty percentage (0–100) |
| `0x26` | LED_R_PERIOD | 2 bytes | Read/Write | Red LED (PB10) blink period in ms |
| `0x28` | LED_R_DUTY | 1 byte | Read/Write | Red LED on-duty percentage (0–100) |
| `0x30` | SAVE_CFG | 1 byte | Write only | Write `0x5A` to save config to flash |

Registers are contiguous in a 41-byte map (0x00–0x28). **Burst reads** are supported — a single read can span multiple registers. The slave builds a snapshot and sends from the start address onward.

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
[START] [0x08 + W] [reg_addr] [data_0] [data_1] ... [STOP]
```

---

## Understanding the Measurements

### Timer Clock

The base timer clock is **100 MHz**. With a prescaler `PSC`, the effective timer clock is:

```
timer_clock = 100,000,000 / (TIM_PSC + 1)
```

Default `TIM_PSC = 0` → timer clock = 100 MHz → tick resolution = 10 ns.

### Converting Register Values

| Value | Formula | Example |
|-------|---------|---------|
| **Frequency** | `FREQ` register gives Hz directly | `FREQ = 1000` → 1 kHz |
| **Period (seconds)** | `PERIOD / timer_clock` | `PERIOD = 100000, PSC = 0` → 1 ms |
| **Duty cycle (%)** | `DUTY / 100.0` | `DUTY = 5000` → 50.00% |
| **Pulse width (seconds)** | `PULSE / timer_clock` | `PULSE = 50000, PSC = 0` → 500 µs |

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
| 0 | 100 MHz | ~0.023 Hz | 50 MHz* | 10 ns |
| 9 | 10 MHz | ~0.002 Hz | 5 MHz* | 100 ns |
| 99 | 1 MHz | ~0.0002 Hz | 500 kHz | 1 µs |
| 9999 | 10 kHz | N/A | 5 kHz | 100 µs |

\* Practical max frequency depends on signal quality and IC prescaler.

### No-Signal Timeout

If no edge is captured for **1 second**, all measurement registers (PERIOD, FREQ, DUTY, PULSE) are zeroed. Reading `FREQ = 0` indicates no signal is present.

---

## Code Examples

### Arduino (Wire Library)

```cpp
#include <Wire.h>

#define FREQ_COUNTER_ADDR 0x08

// Read a 4-byte (uint32_t) register
uint32_t readReg32(uint8_t reg) {
    Wire.beginTransmission(FREQ_COUNTER_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);  // repeated start

    Wire.requestFrom(FREQ_COUNTER_ADDR, 4);
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
    Wire.beginTransmission(FREQ_COUNTER_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);

    Wire.requestFrom(FREQ_COUNTER_ADDR, 2);
    uint16_t val = 0;
    if (Wire.available() == 2) {
        val  = (uint16_t)Wire.read();
        val |= (uint16_t)Wire.read() << 8;
    }
    return val;
}

// Read a 1-byte register
uint8_t readReg8(uint8_t reg) {
    Wire.beginTransmission(FREQ_COUNTER_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);

    Wire.requestFrom(FREQ_COUNTER_ADDR, 1);
    return Wire.available() ? Wire.read() : 0;
}

// Write a 1-byte register
void writeReg8(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(FREQ_COUNTER_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

// Write a 2-byte register (little-endian)
void writeReg16(uint8_t reg, uint16_t value) {
    Wire.beginTransmission(FREQ_COUNTER_ADDR);
    Wire.write(reg);
    Wire.write(value & 0xFF);
    Wire.write((value >> 8) & 0xFF);
    Wire.endTransmission();
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
    """Write a 1-byte register."""
    bus.write_byte_data(ADDR, reg, value)

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

// Write a 1-byte register
void FC_WriteReg8(uint8_t reg, uint8_t value) {
    HAL_I2C_Mem_Write(&hi2c1, FC_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                      &value, 1, 100);
}

// Write a 2-byte register
void FC_WriteReg16(uint8_t reg, uint16_t value) {
    uint8_t buf[2];
    memcpy(buf, &value, 2);
    HAL_I2C_Mem_Write(&hi2c1, FC_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                      buf, 2, 100);
}

// Usage example
void ReadFrequencyCounter(void) {
    uint32_t freq = FC_ReadReg32(0x04);
    uint32_t duty = FC_ReadReg32(0x08);

    // freq is in Hz
    // duty is in 0.01% units, divide by 100.0 for percent
}
```

### ESP32 (Arduino Framework)

```cpp
#include <Wire.h>

#define FREQ_COUNTER_ADDR 0x08
#define SDA_PIN 21
#define SCL_PIN 22

uint32_t readReg32(uint8_t reg) {
    Wire.beginTransmission(FREQ_COUNTER_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);

    Wire.requestFrom((uint8_t)FREQ_COUNTER_ADDR, (uint8_t)4);
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
    i2c.writeto(ADDR, bytes([reg, val]))

def write_reg16(reg, val):
    i2c.writeto(ADDR, bytes([reg]) + struct.pack('<H', val))

# Read measurements
while True:
    freq = read_reg32(0x04)
    duty = read_reg32(0x08)
    print(f"Freq: {freq} Hz, Duty: {duty/100:.2f}%")
    time.sleep(0.5)
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
double periodUs = period * 0.01;  // at 100 MHz, 1 tick = 10 ns = 0.01 us

Console.WriteLine($"Freq: {freq} Hz");
Console.WriteLine($"Duty: {dutyPercent:F2} %");
Console.WriteLine($"Period: {periodUs:F2} us");
Console.WriteLine($"Pulse: {pulse * 0.01:F2} us");
```

---

## Common Operations

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

### 4. Measure Low Frequencies (< 1 Hz)

Increase the timer prescaler to extend the 32-bit counter range:

```
Write 0x11, [PSC_lo, PSC_hi]   → set prescaler (little-endian)
```

Example: PSC = 99 (timer clock = 1 MHz, max period = 4294 seconds)

```
Write 0x11, [0x63, 0x00]
```

### 5. Measure High Frequencies (> 1 MHz)

Use the IC prescaler to reduce interrupt rate:

```
Write 0x13, 0x03   → capture every 8th edge (DIV8)
```

**Note:** With IC prescaler > 0, the FREQ value is still computed correctly by the firmware. The PERIOD and PULSE readings reflect the time between the *captured* edges (i.e., 8 periods apart for DIV8), but FREQ is divided accordingly.

### 6. Configure LEDs

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

### 7. Save Configuration to Flash

To persist the current edge, prescaler, and LED settings across power cycles:

```
Write 0x30, 0x5A   → trigger flash save
```

Settings saved: EDGE, TIM_PSC, IC_PSC, LED_PERIOD, LED_DUTY, LED_G_PERIOD, LED_G_DUTY, LED_R_PERIOD, LED_R_DUTY.

### 8. Read Back Current Configuration

Single reads:
```
Read 0x10 (1 byte) → EDGE
Read 0x11 (2 bytes) → TIM_PSC
Read 0x13 (1 byte) → IC_PSC
Read 0x20 (2 bytes) → LED_PERIOD
Read 0x22 (1 byte) → LED_DUTY
Read 0x23 (2 bytes) → LED_G_PERIOD
Read 0x25 (1 byte) → LED_G_DUTY
Read 0x26 (2 bytes) → LED_R_PERIOD
Read 0x28 (1 byte) → LED_R_DUTY
```

Or burst read 9 bytes from `0x20` to get all LED config at once.

---

## Timing Considerations

- **After writing a config register** (EDGE, TIM_PSC, IC_PSC): The firmware immediately reconfigures the timer. Measurements are cleared (zeroed) and will populate once new edges arrive. Allow at least one signal period before reading.
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
