# PWM Dashboard User Guide

GUI tool for verifying PWM output and frequency capture on the STM32F411 Frequency Counter board. Connect a PWM output pin to the capture input pin with a jumper wire, then use the dashboard to control PWM parameters and observe captured measurements in real time.

## Prerequisites

- **Hardware**: STM32F411 Frequency Counter board with USB cable
- **Python 3.x** with `tkinter` (included with most Python installations)
- **pyserial**: Install with `pip install pyserial`

## Quick Start

```bash
cd tools
python pwm_dashboard.py
```

## Hardware Wiring

The board has two PWM outputs and one capture input:

| Pin  | Function | Header |
|------|----------|--------|
| PA8  | PWM1 output (TIM1 CH1) | — |
| PB6  | PWM2 output (TIM4 CH1) | — |
| PA15 | Capture input (TIM2 CH1) | — |

**Loopback test**: Connect **PA8** (or PB6) to **PA15** with a jumper wire. The dashboard will output a PWM signal on PA8 and read it back on PA15.

```
  STM32F411 Board
  ┌──────────────┐
  │         PA8  ├──────┐   Jumper wire
  │         PB6  ├──┐   │
  │              │  │   │
  │        PA15  ├──┼───┘   (connect one PWM output to PA15)
  │              │  │
  │  USB ──────  │  └── (optional: test PWM2 by moving wire here)
  └──────────────┘
```

## GUI Layout

```
┌──────────────────────────────────────────────────────────────┐
│  Connection           [Port ▾]  [Refresh] [Connect]  Status │
├─────────────────────────────┬────────────────────────────────┤
│  PWM1 (PA8)                 │  PWM2 (PB6)                    │
│  Freq (Hz): [1000] Step:[▾] │  Freq (Hz): [1000] Step:[▾]   │
│  ═══════════════════════     │  ═══════════════════════       │
│  Duty (%):  [50.00] Step:[▾]│  Duty (%):  [50.00] Step:[▾]  │
│  ═══════════════════════     │  ═══════════════════════       │
│  [       Enable       ]     │  [       Enable       ]        │
├──────────────────────────────────────────────────────────────┤
│  Captured Measurements                                       │
│    Freq: 1000 Hz              Duty: 50.00%                   │
│    Period: 96000 ticks        Pulse: 48000 ticks             │
│  [Capture: ON] │ [✓ Auto-refresh]  Interval: [500 ms ▾]  [Refresh Now] │
├──────────────────────────────────────────────────────────────┤
│  CDC Console                                                 │
│  TX: [________________________] [Send]                       │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ > set pwm1 freq 1000                                  │   │
│  │ PWM1 freq staged: 1000 Hz                             │   │
│  │ > status                                              │   │
│  │ Frequency: 1000 Hz                                    │   │
│  │ Duty: 50.00%                                          │   │
│  └──────────────────────────────────────────────────────┘   │
│  [Clear]                                                     │
└──────────────────────────────────────────────────────────────┘
```

## Step-by-Step Test Procedure

### 1. Connect the Board

1. Plug the STM32F411 board into your PC via USB.
2. Launch the dashboard: `python tools/pwm_dashboard.py`
3. The **Connection** panel auto-detects the board (VID `0x0483`, PID `0x5741`). The correct COM port is pre-selected.
4. If the port is not auto-detected, click **Refresh** to rescan, or select the port manually from the dropdown.
5. Click **Connect**. The status indicator turns green and shows "Connected".

### 2. Basic PWM Loopback Test

1. Wire **PA8** to **PA15** with a jumper.
2. In the **PWM1 (PA8)** panel:
   - Type `1000` in the **Freq (Hz)** field.
   - Type `50.00` in the **Duty (%)** field.
   - Click **Enable**.
3. The **Captured Measurements** panel updates automatically (default: every 500 ms):
   - **Freq** should show approximately `1000 Hz`
   - **Duty** should show approximately `50.00%`
4. The CDC Console at the bottom shows all commands sent and responses received.

### 3. Using the Frequency Slider

1. Select a **Step** size from the dropdown next to the frequency field. Available steps: `1`, `10`, `100`, `1000`, `10000` Hz.
2. The slider range adjusts automatically based on the step:

   | Step | Slider range |
   |------|-------------|
   | 1 Hz | 0 -- 1,000 Hz |
   | 10 Hz | 0 -- 10,000 Hz |
   | 100 Hz | 0 -- 100,000 Hz |
   | 1,000 Hz | 0 -- 1,000,000 Hz |
   | 10,000 Hz | 0 -- 10,000,000 Hz |

3. Drag the slider. The frequency field updates in real time as you drag.
4. Commands are sent 300 ms after you stop moving the slider (debounced to avoid flooding).
5. Alternatively, type a value directly in the field and press **Enter** to apply immediately. The entry field accepts any frequency, even values outside the slider range.

### 4. Using the Duty Slider

1. Select a **Step** size: `0.1`, `1.0`, `5.0`, or `10.0` percent.
2. Drag the slider (range 0 -- 100%). Values snap to the selected step.
3. Or type a value like `25.50` in the field and press **Enter**.
4. Duty is sent to the device in 0.01% units internally (e.g., `50.00%` = `5000`).

### 5. Testing PWM2

1. In the **PWM2 (PB6)** panel, set a different frequency (e.g., `5000` Hz) and duty (e.g., `25.00%`).
2. Click **Enable** in the PWM2 panel.
3. Move the jumper wire from PA8 to **PB6** → PA15.
4. Observe the measurements update to match PWM2 settings.
5. Both PWM channels are fully independent -- you can enable/disable them separately.

### 6. Enabling / Disabling Capture

The **Capture: ON / OFF** button in the measurements panel controls whether the board is actively measuring the input signal on PA15.

- Click the button to toggle capture on or off. When off, all measurements read zero.
- The button state syncs from the device on each status refresh — if another interface (I2C, HID, or CDC console) changes the capture state, the button updates automatically.

### 7. Adjusting Measurement Refresh

- **Auto-refresh** is enabled by default. The dashboard sends the `status` command periodically.
- Change the **Interval** dropdown to adjust refresh rate: `100`, `200`, `500`, `1000`, or `2000` ms.
  - Use `100 ms` for watching fast parameter sweeps.
  - Use `1000 ms` or `2000 ms` for steady-state observation (less console traffic).
- Uncheck **Auto-refresh** to stop automatic polling.
- Click **Refresh Now** at any time for a one-shot measurement update.

### 8. Using the CDC Console

The console at the bottom shows all serial traffic and allows sending raw commands.

**Viewing traffic**: Every command the dashboard sends appears in blue with a `>` prefix. Device responses appear in gray. All auto-refresh `status` queries and their responses are visible here.

**Sending raw commands**: Type any command in the **TX** field and press **Enter** or click **Send**. Examples:

```
help              -- show all available commands
freq              -- read frequency only
duty              -- read duty cycle only
set capture off   -- disable input capture
set capture on    -- enable input capture
set edge 1        -- switch to falling edge capture
set tim_psc 95    -- set timer prescaler to 95 (1 MHz tick rate)
set ic_psc 2      -- set input capture prescaler to DIV4
save              -- save current configuration to flash
```

**Clear**: Click **Clear** to empty the console output. The console keeps a maximum of 5000 lines.

### 8. Disable PWM Output

- Click the **Disable** button in the PWM panel to stop that channel's output.
- While disabled, slider and entry changes do not send commands to the device.
- Click **Enable** again to re-apply the current frequency and duty settings.

## Typical Test Scenarios

### Frequency Sweep

1. Enable PWM1, set duty to `50.00%`.
2. Set frequency step to `1000`.
3. Drag the frequency slider from left to right.
4. Watch the Captured Frequency track the PWM1 output as you sweep.

### Duty Cycle Sweep

1. Enable PWM1, set frequency to `10000` Hz.
2. Set duty step to `1.0`.
3. Drag the duty slider from 0% to 100%.
4. Watch the Captured Duty track the slider value.

### High Frequency Test

1. Type `1000000` (1 MHz) in the PWM1 frequency field, press **Enter**.
2. Verify the captured frequency reads approximately `1000000 Hz`.
3. Note: At very high frequencies, duty cycle resolution decreases due to fewer timer ticks per period.

### Low Frequency Test

1. Set frequency step to `1`, type `10` in the frequency field, press **Enter**.
2. Verify the captured frequency reads approximately `10 Hz`.
3. Measurement updates may take slightly longer at low frequencies (the capture needs at least one full period).

### Compare Two PWM Channels

1. Enable PWM1 at 1000 Hz, 50%.
2. Enable PWM2 at 5000 Hz, 25%.
3. Wire PA8 → PA15, note the measurements.
4. Move wire to PB6 → PA15, note the measurements change.
5. This confirms both PWM outputs are independently generating correct signals.

## Settings Persistence

The dashboard saves your last-used settings to `~/.stm32_pwm_dashboard.json` when you close the window:

- Last selected COM port
- PWM1 and PWM2 frequency, duty, step sizes, and enable state
- Auto-refresh toggle and interval

These are restored automatically the next time you launch the dashboard.

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| No COM port in dropdown | Board not connected or driver missing | Reconnect USB, click **Refresh**. On Windows, check Device Manager for the COM port. |
| "Connected" but no measurements | No wire between PWM output and PA15 | Connect PA8 or PB6 to PA15 with a jumper wire |
| Measurements show 0 Hz | PWM not enabled, capture off, or wire disconnected | Check **Capture: ON** button, click **Enable** in the PWM panel, check jumper wire |
| Frequency reads wrong value | Timer prescaler changed | Send `set tim_psc 0` in the console to reset prescaler |
| Duty reads wrong at high freq | Limited timer resolution at high frequency | Expected -- fewer ticks per period reduces duty accuracy |
| `[DISCONNECTED]` in console | USB cable unplugged or board reset | Reconnect USB, click **Connect** again |
| `pyserial is required` error | pyserial not installed | Run `pip install pyserial` |

## CDC Command Quick Reference

| Command | Description |
|---------|-------------|
| `status` | Read all measurements (capture state, freq, duty, period, pulse) |
| `freq` | Read frequency only |
| `duty` | Read duty cycle only |
| `set pwmN freq <hz>` | Stage PWM frequency (N = 1 or 2) |
| `set pwmN duty <0-10000>` | Stage PWM duty in 0.01% units |
| `set pwmN enable <0\|1>` | Apply staged config and enable/disable |
| `capture` | Read capture state (on/off) |
| `set capture <on\|off>` | Enable or disable input capture |
| `set edge <0\|1>` | Set capture edge (0 = rising, 1 = falling) |
| `set tim_psc <0-65535>` | Set timer prescaler |
| `set ic_psc <0-3>` | Set input capture prescaler |
| `save` | Save all config to flash |
| `help` | Show full command listing |
