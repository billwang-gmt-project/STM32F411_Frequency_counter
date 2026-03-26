# CDC Programming Guide (Host Side)

USB CDC serial console for the STM32F411 Frequency Counter. The device enumerates as a USB composite device (CDC + HID); this guide covers the **CDC virtual COM port** interface only.

Commands follow **IEEE 488.2 / SCPI** conventions.

## USB Enumeration

| Field | Value |
|-------|-------|
| VID | `0x0483` (STMicroelectronics) |
| PID | `0x5741` |
| Device class | `0xEF` (Miscellaneous / IAD) |
| Product string | `FC-411 USB Composite` |
| Serial number | Derived from STM32 96-bit UID |
| CDC interface | ACM (Abstract Control Model) |

The device appears as a virtual COM port (e.g. `COMx` on Windows, `/dev/ttyACMx` on Linux).

## Connection Settings

The CDC interface ignores baud rate, parity, and flow control settings from the host -- all `SET_LINE_CODING` requests are accepted but have no effect. Any terminal program will work with any baud rate setting.

**Line endings:** Commands are terminated by `CR` (`\r`), `LF` (`\n`), or `CR+LF`. Responses use `\r\n`.

**Line buffer:** 128 characters max per command line. Characters beyond this are silently dropped.

## SCPI Command Syntax

- Commands are **case-insensitive**: `MEAS:FREQ?`, `meas:freq?`, `Meas:Freq?` are all valid.
- Subsystem levels are separated by **colons** (`:`).
- A trailing **`?`** marks a **query** (read). Without `?`, the command **sets** a value.
- **Abbreviated keywords** are supported. Uppercase letters in the reference below show the mandatory short form; the rest is optional. Example: `FREQuency` accepts `FREQ`, `FREQU`, ..., `FREQUENCY`.

## IEEE 488.2 Common Commands

| Command | Response | Description |
|---------|----------|-------------|
| `*IDN?` | `<Manufacturer>,<Model>,<Serial>,<Version>` | Device identification |
| `*SAV` | `OK` | Save all settings to flash |
| `*RST` | *(device resets)* | Software reset (MCU reboot) |

**Items saved by `*SAV`:**

| # | Item | SCPI Command | Description |
|---|------|-------------|-------------|
| 1 | EDGE | `CAPT:EDGE` | Capture edge (rising/falling) |
| 2 | TIM_PSC | `CAPT:TIM:PSC` | Timer prescaler (0–65535) |
| 3 | IC_PSC | `CAPT:IC:PSC` | Input capture prescaler (DIV1–DIV8) |
| 4 | CAPTURE_CTRL | `CAPT:ENAB` | Capture enable/disable |
| 5 | LED_PERIOD | `LED:PER` | Status LED (PC13) blink period (ms) |
| 6 | LED_DUTY | `LED:DUTY` | Status LED on-duty (%) |
| 7 | LED_G_PERIOD | `LED:G:PER` | Green LED (PC14) blink period (ms) |
| 8 | LED_G_DUTY | `LED:G:DUTY` | Green LED on-duty (%) |
| 9 | LED_R_PERIOD | `LED:R:PER` | Red LED (PB10) blink period (ms) |
| 10 | LED_R_DUTY | `LED:R:DUTY` | Red LED on-duty (%) |
| 11 | PWM1_FREQ | `SOUR:PWM1:FREQ` | PWM1 frequency (Hz) |
| 12 | PWM1_DUTY | `SOUR:PWM1:DUTY` | PWM1 duty cycle (0–10000, 0.01% units) |
| 13 | PWM1_CTRL | `SOUR:PWM1:ENAB` | PWM1 enable/disable |
| 14 | PWM2_FREQ | `SOUR:PWM2:FREQ` | PWM2 frequency (Hz) |
| 15 | PWM2_DUTY | `SOUR:PWM2:DUTY` | PWM2 duty cycle (0–10000, 0.01% units) |
| 16 | PWM2_CTRL | `SOUR:PWM2:ENAB` | PWM2 enable/disable |
| 17 | TRIG_WIDTH | `TRIG:WIDT` | Trigger pulse width (1–1000 µs) |
| 18 | NICKNAME | `SYST:NAME` | Device nickname (max 16 chars) |

**`*IDN?` response example:**
```
STMicroelectronics,FC-411,A1B2C3D4E5F6G7H8,26032600
```

## MEASure Subsystem (Read-Only)

| Command | Short Form | Response |
|---------|------------|----------|
| `MEASure:FREQuency?` | `MEAS:FREQ?` | Frequency in Hz |
| `MEASure:DUTY?` | `MEAS:DUTY?` | Duty cycle in 0.01% units (e.g. `50.00`) |
| `MEASure:PERiod?` | `MEAS:PER?` | Period in timer ticks |
| `MEASure:PULSe?` | `MEAS:PULS?` | Pulse width in timer ticks |
| `MEASure:ALL?` | `MEAS:ALL?` | All measurements, comma-separated |

**`MEAS:ALL?` response format:** `<capture>,<freq>,<duty>,<period>,<pulse>`

```
ON,1000,50.00,96000,48000
```

## CAPture Subsystem

| Command | Short Form | Range | Description |
|---------|------------|-------|-------------|
| `CAPture:EDGE[?]` | `CAPT:EDGE` | 0 = rising, 1 = falling | Capture edge selection |
| `CAPture:ENABle[?]` | `CAPT:ENAB` | ON/OFF or 0/1 | Enable/disable input capture |
| `CAPture:TIM:PSC[?]` | `CAPT:TIM:PSC` | 0 -- 65535 | Timer prescaler (divides 96 MHz clock) |
| `CAPture:IC:PSC[?]` | `CAPT:IC:PSC` | 0 -- 3 | IC prescaler (0=DIV1, 1=DIV2, 2=DIV4, 3=DIV8) |

Query returns the current value; command sets a new value and echoes it back.

## SOURce Subsystem (PWM Outputs)

Two independent PWM outputs: **PWM1** (PA8, TIM1) and **PWM2** (PB6, TIM4).

PWM uses a **stage-then-apply** pattern:
1. Set frequency with `SOUR:PWMn:FREQ <hz>`
2. Set duty with `SOUR:PWMn:DUTY <value>`
3. Apply with `SOUR:PWMn:ENAB 1`

Writing `ENABle` commits all staged values atomically and fires a trigger pulse on PA7.

| Command | Short Form | Range | Description |
|---------|------------|-------|-------------|
| `SOURce:PWM1:FREQuency[?]` | `SOUR:PWM1:FREQ` | ~2 -- ~50,000,000 | PWM1 frequency (Hz) |
| `SOURce:PWM1:DUTY[?]` | `SOUR:PWM1:DUTY` | 0 -- 10000 | PWM1 duty (0.01% units) |
| `SOURce:PWM1:ENABle[?]` | `SOUR:PWM1:ENAB` | 0 or 1 | Apply staged config, enable/disable |
| `SOURce:PWM2:FREQuency[?]` | `SOUR:PWM2:FREQ` | ~2 -- ~50,000,000 | PWM2 frequency (Hz) |
| `SOURce:PWM2:DUTY[?]` | `SOUR:PWM2:DUTY` | 0 -- 10000 | PWM2 duty (0.01% units) |
| `SOURce:PWM2:ENABle[?]` | `SOUR:PWM2:ENAB` | 0 or 1 | Apply staged config, enable/disable |

**Example -- 1 kHz, 50% duty on PWM1:**
```
SOUR:PWM1:FREQ 1000
SOUR:PWM1:DUTY 5000
SOUR:PWM1:ENAB 1
```

## LED Subsystem

Three LEDs with independent blink period and on-duty:

| LED | Pin | Active | Prefix |
|-----|-----|--------|--------|
| Status (blue) | PC13 | Low | `LED:` |
| Green | PC14 | High | `LED:G:` |
| Red | PB10 | High | `LED:R:` |

| Command | Short Form | Range | Description |
|---------|------------|-------|-------------|
| `LED:PERiod[?]` | `LED:PER` | 1 -- 65535 | Status LED blink period (ms) |
| `LED:DUTY[?]` | `LED:DUTY` | 0 -- 100 | Status LED on-duty (%) |
| `LED:G:PERiod[?]` | `LED:G:PER` | 1 -- 65535 | Green LED period |
| `LED:G:DUTY[?]` | `LED:G:DUTY` | 0 -- 100 | Green LED duty |
| `LED:R:PERiod[?]` | `LED:R:PER` | 1 -- 65535 | Red LED period |
| `LED:R:DUTY[?]` | `LED:R:DUTY` | 0 -- 100 | Red LED duty |

## TRIGger Subsystem

| Command | Short Form | Range | Description |
|---------|------------|-------|-------------|
| `TRIGger:WIDTh[?]` | `TRIG:WIDT` | 1 -- 1000 | Trigger pulse width in microseconds (PA7) |

## SYSTem Subsystem

| Command | Short Form | Description |
|---------|------------|-------------|
| `SYSTem:NAME[?]` | `SYST:NAME` | Query or set device nickname (max 16 chars, default: serial number) |
| `SYSTem:NAME:DEFault` | `SYST:NAME:DEF` | Reset nickname to serial number |
| `SYSTem:VERSion?` | `SYST:VERS?` | Firmware version (date-encoded, e.g. `26032600`) |
| `SYSTem:HELP?` | `SYST:HELP?` | Full command reference |

**Device nickname** — Each board has a configurable nickname for multi-device identification. Default is the 16-char hex serial number derived from the STM32 UID.

```
SYST:NAME?               → "A1B2C3D4E5F6G7H8"   (default = serial)
SYST:NAME "BENCH-1"      → "BENCH-1"
SYST:NAME BENCH-1        → "BENCH-1"             (quotes optional)
SYST:NAME:DEF            → "A1B2C3D4E5F6G7H8"   (reset to serial)
```

Use `*SAV` to persist the nickname to flash.

## Error Handling

Invalid commands or out-of-range values return a SCPI standard error:

```
-100,"Command error"
```

## Host Programming Examples

### Python (NI VISA / pyvisa)

```python
import pyvisa

rm = pyvisa.ResourceManager()
inst = rm.open_resource('ASRL3::INSTR')  # COM port number varies
inst.read_termination = '\r\n'
inst.write_termination = '\n'

# Identify device
print(inst.query('*IDN?'))

# Read frequency
freq = int(inst.query('MEAS:FREQ?'))
print(f'Frequency: {freq} Hz')

# Read all measurements
print(inst.query('MEAS:ALL?'))

# Configure PWM1: 10 kHz, 25% duty
inst.write('SOUR:PWM1:FREQ 10000')
inst.write('SOUR:PWM1:DUTY 2500')
inst.write('SOUR:PWM1:ENAB 1')

# Save to flash
inst.write('*SAV')

inst.close()
```

### Python (pyserial)

```python
import serial
import time

ser = serial.Serial('COM3', timeout=1)  # baud rate is ignored by device
time.sleep(0.5)

# Read frequency
ser.write(b'MEAS:FREQ?\n')
ser.readline()                    # echo blank line
freq = ser.readline().decode().strip()
print(f'Frequency: {freq} Hz')

# Configure PWM1: 10 kHz, 25% duty
for cmd in ['SOUR:PWM1:FREQ 10000', 'SOUR:PWM1:DUTY 2500', 'SOUR:PWM1:ENAB 1']:
    ser.write(f'{cmd}\n'.encode())
    time.sleep(0.05)
    while ser.in_waiting:
        print(ser.readline().decode().strip())

# Save and reset
ser.write(b'*SAV\n')
time.sleep(0.1)
print(ser.readline().decode().strip())

ser.close()
```

### C# (.NET)

```csharp
using System.IO.Ports;

using var port = new SerialPort("COM3") { ReadTimeout = 1000 };
port.Open();
Thread.Sleep(500);

// Identify device
port.Write("*IDN?\n");
Thread.Sleep(50);
Console.WriteLine(port.ReadExisting());

// Configure PWM2: 500 Hz, 75% duty
foreach (var cmd in new[] { "SOUR:PWM2:FREQ 500", "SOUR:PWM2:DUTY 7500", "SOUR:PWM2:ENAB 1" })
{
    port.Write(cmd + "\n");
    Thread.Sleep(50);
}
Console.WriteLine(port.ReadExisting());
```

### Command-line (Linux)

```bash
# Interactive session
screen /dev/ttyACM0 115200   # baud rate doesn't matter
minicom -D /dev/ttyACM0
```

## Tick-to-Physical Conversion

The `MEAS:PER?` and `MEAS:PULS?` values are in **timer ticks** at the capture clock rate:

```
capture_clock = 96 MHz / (tim_psc + 1)
```

With default `tim_psc = 0`:
- **Period in seconds** = `period_ticks / 96,000,000`
- **Frequency** = `96,000,000 / period_ticks` (the `MEAS:FREQ?` command does this for you)
- **Pulse width in seconds** = `pulse_ticks / 96,000,000`

If `CAPT:TIM:PSC` is set to N, divide the clock by (N+1).

### IC Prescaler Effect

When `CAPT:IC:PSC > 0`, the hardware captures every N-th edge (N = 1, 2, 4, or 8). The `MEAS:FREQ?` and `MEAS:DUTY?` responses are **automatically compensated** -- they always report the true single-cycle values. The `MEAS:PER?` and `MEAS:PULS?` tick values span N signal cycles; to get single-cycle values, divide by N (where N = 2^ic_psc).

## Data Flow Overview

```
Host serial write
  -> USB bulk OUT transfer
  -> OTG_FS_IRQHandler (priority 6)
  -> CDC_Receive_FS() pushes bytes into RX ring buffer (512 B)
  -> Semaphore wakes UsbTask (FreeRTOS, priority 2)
  -> UsbTask pops bytes, accumulates lines
  -> On CR/LF: CDC_ParseLine() -> SCPI parser -> response into TX ring buffer (2048 B)
  -> UsbTask drains TX FIFO -> USB bulk IN transfer
  -> Host serial read
```

## Notes

- The device does **not** echo typed characters. Enable local echo in your terminal if needed for interactive use.
- Multiple commands can be sent in a single USB transfer (separated by line endings); all responses are queued and returned in order.
- The TX buffer is 2048 bytes. Long burst responses (e.g. `SYST:HELP?`) fit within a single buffer cycle.
- No flow control (hardware or software) is used. The 512-byte RX FIFO drops bytes on overflow -- avoid sending faster than the device can parse.
- The `MEAS:ALL?` command is an atomic snapshot of all measurement registers, taken under a single lock.
- **NI VISA compatible:** The device works with `pyvisa` and NI VISA as an `ASRL` (serial) resource. SCPI queries work with `inst.query()` and commands with `inst.write()`.
