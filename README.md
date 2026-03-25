# STM32F411 Frequency Counter

A frequency counter with I2C slave interface built on the STM32F411CEUx (WeAct BlackPill or similar). Measures frequency, period, duty cycle, and pulse width using TIM2 input capture in PWM Input mode. A host MCU reads measurements over I2C.

## Hardware

| Pin | Function |
|-----|----------|
| PA15 | Signal input (TIM2_CH1) |
| PB7 | I2C1 SDA |
| PB8 | I2C1 SCL |

- MCU: STM32F411CEUx, 100 MHz (HSI + PLL)
- TIM2 timer clock: 100 MHz (default prescaler = 0)
- I2C1: 400 kHz fast mode, 7-bit addressing

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
| 0x10 | EDGE | 1 byte | Read/Write | Capture edge: 0 = rising (default), 1 = falling |
| 0x11 | TIM_PSC | 2 bytes | Read/Write | Timer prescaler, 0-65535 (default: 0) |
| 0x13 | IC_PSC | 1 byte | Read/Write | Input capture prescaler: 0=DIV1, 1=DIV2, 2=DIV4, 3=DIV8 |

All multi-byte values are **little-endian** (native ARM byte order).

Timer clock = 100,000,000 / (TIM_PSC + 1). With default TIM_PSC=0, each tick = 10 ns.

### Reading a Register

Write the register address, then read the data bytes:

```
START → 0x08 W → [reg_addr] → RESTART → 0x08 R → [data bytes...] → STOP
```

### Writing a Register

Write the register address followed by the data:

```
START → 0x08 W → [reg_addr] [data bytes...] → STOP
```

### Host Examples

**Arduino:**
```cpp
#include <Wire.h>

#define FREQ_COUNTER_ADDR 0x08

uint32_t readRegister32(uint8_t reg) {
    Wire.beginTransmission(FREQ_COUNTER_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);  // repeated start
    Wire.requestFrom(FREQ_COUNTER_ADDR, 4);
    uint32_t val = 0;
    for (int i = 0; i < 4 && Wire.available(); i++) {
        val |= (uint32_t)Wire.read() << (i * 8);
    }
    return val;
}

void writeRegister8(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(FREQ_COUNTER_ADDR);
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

## Measurement Specifications

| Parameter | Value |
|-----------|-------|
| Frequency range | ~2.3 Hz to ~50 MHz (with default PSC=0) |
| Period resolution | 10 ns per tick (at 100 MHz timer clock) |
| Duty resolution | 0.01% |
| No-signal timeout | 1 second (all registers read 0) |
| Minimum measurable period | 2 timer ticks (50 MHz max input) |
| Maximum measurable period | 2^32 ticks = ~42.9 seconds (at 100 MHz) |

### Prescaler Usage

For very high frequency signals where resolution is sufficient, increase TIM_PSC to extend the measurable range or reduce interrupt rate. For example, TIM_PSC=99 gives a 1 MHz timer clock (1 us resolution).

The IC prescaler captures every Nth edge (DIV1/2/4/8), useful for reducing interrupt load on high-frequency signals.

## Building

Requires **STM32CubeIDE**. Import the project and build the Debug or Release configuration.

The CubeMX configuration file is `Frequency_Counter.ioc`. Regenerating code from CubeMX is safe — all custom logic is inside `/* USER CODE BEGIN/END */` sections.

## License

This project uses STM32 HAL drivers licensed under the ST SLA0044 license. See the LICENSE file in the Drivers directory for details.
