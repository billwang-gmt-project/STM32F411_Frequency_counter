# CDC Programming Guide (Host Side)

USB CDC serial console for the STM32F411 Frequency Counter. The device enumerates as a USB composite device (CDC + HID); this guide covers the **CDC virtual COM port** interface only.

## USB Enumeration

| Field | Value |
|-------|-------|
| VID | `0x0483` (STMicroelectronics) |
| PID | `0x5741` |
| Device class | `0xEF` (Miscellaneous / IAD) |
| Product string | `FC USB Composite` |
| Serial number | Derived from STM32 96-bit UID |
| CDC interface | ACM (Abstract Control Model) |

The device appears as a virtual COM port (e.g. `COMx` on Windows, `/dev/ttyACMx` on Linux).

## Connection Settings

The CDC interface ignores baud rate, parity, and flow control settings from the host -- all `SET_LINE_CODING` requests are accepted but have no effect. Any terminal program will work with any baud rate setting.

**Line endings:** Commands are terminated by `CR` (`\r`), `LF` (`\n`), or `CR+LF`. Responses use `\r\n`.

**Line buffer:** 128 characters max per command line. Characters beyond this are silently dropped.

## Command Reference

All commands are case-insensitive. Leading whitespace is ignored. Empty lines are ignored.

### Read Commands

| Command | Response | Example |
|---------|----------|---------|
| `freq` | `Frequency: <n> Hz` | `Frequency: 1000 Hz` |
| `duty` | `Duty: <n.nn>%` | `Duty: 50.00%` |
| `period` | `Period: <n> ticks` | `Period: 96000 ticks` |
| `pulse` | `Pulse: <n> ticks` | `Pulse: 48000 ticks` |
| `edge` | `Edge: rising` or `Edge: falling` | `Edge: rising` |
| `status` | All four measurements at once | See below |
| `help` | Full command listing | |

**`status` response example:**
```
Frequency: 1000 Hz
Duty: 50.00%
Period: 96000 ticks
Pulse: 48000 ticks
```

### Timer / Capture Configuration

| Command | Range | Description |
|---------|-------|-------------|
| `set edge <0\|1>` | 0 = rising, 1 = falling | Capture edge selection |
| `set tim_psc <n>` | 0 -- 65535 | Timer prescaler (divides 96 MHz clock) |
| `set ic_psc <n>` | 0 -- 3 | Input capture prescaler (0=DIV1, 1=DIV2, 2=DIV4, 3=DIV8) |

### PWM Output Configuration

Two independent PWM outputs: **PWM1** (PA8, TIM1) and **PWM2** (PB6, TIM4).

PWM uses a **stage-then-apply** pattern:
1. Set frequency with `set pwmN freq <hz>`
2. Set duty with `set pwmN duty <value>`
3. Apply with `set pwmN enable 1`

Writing `enable` commits all staged values atomically and fires a trigger pulse on PA7.

| Command | Range | Description |
|---------|-------|-------------|
| `set pwm1 freq <hz>` | ~2 -- ~50,000,000 | Stage PWM1 frequency (Hz) |
| `set pwm1 duty <n>` | 0 -- 10000 | Stage PWM1 duty cycle (0.01% units, e.g. 5000 = 50%) |
| `set pwm1 enable <0\|1>` | 0 or 1 | Apply staged config and enable/disable PWM1 |
| `set pwm2 freq <hz>` | ~2 -- ~50,000,000 | Stage PWM2 frequency |
| `set pwm2 duty <n>` | 0 -- 10000 | Stage PWM2 duty cycle |
| `set pwm2 enable <0\|1>` | 0 or 1 | Apply staged config and enable/disable PWM2 |

**Example -- 1 kHz, 50% duty on PWM1:**
```
set pwm1 freq 1000
set pwm1 duty 5000
set pwm1 enable 1
```
Response:
```
PWM1 freq staged: 1000 Hz
PWM1 duty staged: 5000
PWM1 enabled
```

### LED Control

Three LEDs with independent blink period and on-duty:

| LED | Pin | Active | Prefix |
|-----|-----|--------|--------|
| Status (blue) | PC13 | Low | `led` |
| Green | PC14 | High | `led_g` |
| Red | PB10 | High | `led_r` |

| Command | Range | Description |
|---------|-------|-------------|
| `set led period <ms>` | 0 -- 65535 | Status LED blink period in ms |
| `set led duty <n>` | 0 -- 100 | Status LED on-duty (%) |
| `set led_g period <ms>` | 0 -- 65535 | Green LED blink period |
| `set led_g duty <n>` | 0 -- 100 | Green LED on-duty |
| `set led_r period <ms>` | 0 -- 65535 | Red LED blink period |
| `set led_r duty <n>` | 0 -- 100 | Red LED on-duty |

### Trigger Pulse

| Command | Range | Description |
|---------|-------|-------------|
| `set trig_width <us>` | 1 -- 1000 | Trigger pulse width in microseconds on PA7 |

### Persistence

| Command | Description |
|---------|-------------|
| `save` | Save all current settings to flash (survives power cycle) |

Response: `Config saved`

## Error Handling

Invalid commands return one of:
- `Unknown command. Type 'help'`
- `Unknown set target. Type 'help'`
- `Error: <specific message>` (e.g. `Error: edge must be 0 (rising) or 1 (falling)`)

## Host Programming Examples

### Python (pyserial)

```python
import serial
import time

# Open the CDC virtual COM port
ser = serial.Serial('COM3', timeout=1)  # baud rate is ignored by device
time.sleep(0.5)  # wait for USB enumeration

# Read frequency
ser.write(b'freq\r\n')
print(ser.readline().decode())   # empty echo line
print(ser.readline().decode())   # "Frequency: 1000 Hz\r\n"

# Read all measurements
ser.write(b'status\r\n')
for _ in range(5):               # echo + 4 data lines
    line = ser.readline().decode().strip()
    if line:
        print(line)

# Configure PWM1: 10 kHz, 25% duty
for cmd in ['set pwm1 freq 10000', 'set pwm1 duty 2500', 'set pwm1 enable 1']:
    ser.write(f'{cmd}\r\n'.encode())
    time.sleep(0.05)
    while ser.in_waiting:
        print(ser.readline().decode().strip())

# Save to flash
ser.write(b'save\r\n')
print(ser.readline().decode())
print(ser.readline().decode())

ser.close()
```

### C# (.NET)

```csharp
using System.IO.Ports;

using var port = new SerialPort("COM3") { ReadTimeout = 1000 };
port.Open();
Thread.Sleep(500);

// Read frequency
port.Write("freq\r\n");
Thread.Sleep(50);
Console.WriteLine(port.ReadExisting());

// Configure PWM2: 500 Hz, 75% duty
foreach (var cmd in new[] { "set pwm2 freq 500", "set pwm2 duty 7500", "set pwm2 enable 1" })
{
    port.Write(cmd + "\r\n");
    Thread.Sleep(50);
}
Console.WriteLine(port.ReadExisting());
```

### Command-line (Linux)

```bash
# One-shot read
echo "freq" > /dev/ttyACM0
cat /dev/ttyACM0 &  # background reader
sleep 0.1 && kill %1

# Interactive session with screen/minicom
screen /dev/ttyACM0 115200   # baud rate doesn't matter
minicom -D /dev/ttyACM0
```

## Tick-to-Physical Conversion

The `period` and `pulse` values are in **timer ticks** at the capture clock rate:

```
capture_clock = 96 MHz / (tim_psc + 1)
```

With default `tim_psc = 0`:
- **Period in seconds** = `period_ticks / 96,000,000`
- **Frequency** = `96,000,000 / period_ticks` (the `freq` command does this for you)
- **Pulse width in seconds** = `pulse_ticks / 96,000,000`

If `tim_psc` is set to N, divide the clock by (N+1).

## Data Flow Overview

```
Host serial write
  -> USB bulk OUT transfer
  -> OTG_FS_IRQHandler (priority 6)
  -> CDC_Receive_FS() pushes bytes into RX ring buffer (512 B)
  -> Semaphore wakes UsbTask (FreeRTOS, priority 2)
  -> UsbTask pops bytes, accumulates lines
  -> On CR/LF: CDC_ParseLine() -> response into TX ring buffer (2048 B)
  -> UsbTask drains TX FIFO -> USB bulk IN transfer
  -> Host serial read
```

## Notes

- The device does **not** echo typed characters. Enable local echo in your terminal if needed for interactive use.
- Multiple commands can be sent in a single USB transfer (separated by line endings); all responses are queued and returned in order.
- The TX buffer is 2048 bytes. Long burst responses (e.g. `help`) fit within a single buffer cycle.
- No flow control (hardware or software) is used. The 512-byte RX FIFO drops bytes on overflow -- avoid sending faster than the device can parse.
- The `status` command is an atomic snapshot of all four measurement registers, taken under a single lock.
