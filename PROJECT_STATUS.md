# PROJECT_STATUS.md

**Project**: STM32F411 FanTestKit (I2C + USB CDC + USB HID)
**Last updated**: 2026-03-26 (GUI state sync on connect, checkbox refactor)
**Branch**: `master`
**Latest commit**: `42b12a7` — GUI 改進：連線時讀取裝置狀態、Capture/PWM 啟用改為 Checkbox

---

## Completed Tasks

- [x] Initial bare-metal firmware: TIM2 input capture (PWM input mode) on PA15, I2C slave at 0x08 with register-based protocol
- [x] Add LED_G (PC14) and LED_R (PB10) with I2C-configurable blink frequency and duty cycle (registers 0x23-0x28)
- [x] Flash config persistence: all writable registers saved to sector 7 via SAVE_CFG (0x30, write 0x5A)
- [x] Refactor to FreeRTOS v10.3.1 (native API, no CMSIS-RTOS wrapper)
  - LedTask: manages 3 LEDs with configurable period/duty
  - MonitorTask: capture timeout detection (zeros measurements after 1s)
  - SysTick shared between HAL and FreeRTOS via custom handler
  - SVC_Handler/PendSV_Handler provided by FreeRTOS port.c (#if 0 around CubeMX stubs)
- [x] Fix I2C slave direction bug: `TransferDirection` check was swapped in `HAL_I2C_AddrCallback`
- [x] Support burst I2C register reads: `I2C_BuildTxBuffer()` builds register map snapshot
- [x] Fix register address capture: read from `i2c_rx_buf[0]` in read-phase AddrCallback
- [x] Add two PWM outputs: TIM1_CH1 on PA8, TIM4_CH1 on PB6 with auto-prescaler, glitch-free updates, trigger pulse on PA7
- [x] Add USB composite device: CDC (SCPI text console) + HID (binary register access)
  - UsbTask (FreeRTOS, P2): processes CDC commands and HID reports from event queue
  - Shared register map via `regmap.c` with FreeRTOS mutex for write protection
  - SCPI command parser: IEEE 488.2 common commands + MEASure/CAPture/SOURce/LED/TRIGger/SYSTem subsystems
- [x] Add capture enable/disable control (CAPTURE_CTRL register 0x14)
- [x] Add device nickname (NICKNAME register 0x60, 16 chars, default = serial number, flash-persistable)
- [x] Refactor CDC commands to SCPI format, add `config.h` for centralized device settings
- [x] PWM Dashboard GUI (`tools/pwm_dashboard.py`): Tkinter app for PWM control, measurement display, CDC console
- [x] PWM Dashboard: add device info bar (VID, PID, serial number), multi-device selection, nickname editing
- [x] PWM Dashboard: add capture edge selector (Rising/Falling dropdown)
- [x] PWM Dashboard: stop auto-refresh polling when capture is off
- [x] Fix INDIRECTTI polarity bug: swap edge condition in `Capture_Reconfigure()` so EDGE=0 (Rising) correctly measures high-time duty
- [x] Update all documentation: CLAUDE.md, README.md, CDC_programming_guide.md, PWM_Dashboard_User_Guide.md
- [x] PWM Dashboard: read device state on connect (capture, edge, PWM freq/duty/enable) via sequential SCPI query queue
- [x] PWM Dashboard: refactor Capture and PWM Enable from toggle buttons to checkboxes

## Current Obstacles

- **`Config_Save()` runs from I2C ISR context** (priority 2): flash sector erase takes ~100ms, blocking all interrupts at priority >= 2 during that time. Consider deferring to a FreeRTOS task.
- **CubeMX regeneration clobbers FreeRTOS handlers**: `SVC_Handler` and `PendSV_Handler` in `stm32f4xx_it.c` must be re-commented after any CubeMX code generation.
- **INDIRECTTI polarity quirk**: With `TIM_ICSELECTION_INDIRECTTI`, the polarity bit has the opposite effect. The condition in `Capture_Reconfigure()` is intentionally swapped (`EDGE_RISING` → falling capture polarity). Do not "fix" this — verified empirically.

## Architecture Summary

```
                    +-----------------+
   PA15 signal ---->| TIM2 CH1/CH2    |  Priority 1 (above FreeRTOS)
                    | Input Capture   |
                    +--------+--------+
                             |
                    volatile globals (period, freq, duty, pulse)
                             |
        +--------------------+--------------------+
        |                    |                    |
+-------+--------+  +-------+--------+  +--------+--------+
| I2C1 Slave     |  | USB CDC (SCPI) |  | USB HID (regs)  |
| (0x08, P2)     |  | UsbTask (P2)   |  | UsbTask (P2)    |
| - reg read/write|  | - text commands|  | - binary reports|
| - config save  |  +-------+--------+  +--------+--------+
+-------+--------+          |                    |
        |            regmap.c (shared register access layer)
        |                    |
+-------+--------+  +-------+--------+
| Flash Sector 7 |  | PWM Outputs    |
| ConfigData_t   |  | TIM1→PA8 PWM1  |
| 0x08060000     |  | TIM4→PB6 PWM2  |
+-----------------+  | PA7 trigger    |
                     +-----------------+
+----------------+
| LedTask  (P1)  |   PC13 (status), PC14 (green), PB10 (red)
+----------------+
| MonitorTask(P1)|   1s capture timeout → zeros measurements
+----------------+
```

## Key Files

| File | Role |
|------|------|
| `Core/Src/main.c` | Application logic: tasks, callbacks, I2C protocol, PWM, flash config |
| `Core/Src/regmap.c` | Shared register access layer for I2C, CDC, and HID |
| `Core/Inc/config.h` | Centralized device settings (VID, PID, manufacturer, model, version) |
| `Core/Src/tim.c` | TIM2 PWM input capture setup + PA15 remap |
| `Core/Src/i2c.c` | I2C1 init + address override to 0x08 |
| `USB_DEVICE/App/usb_cdc_cmd.c` | SCPI command parser for CDC interface |
| `USB_DEVICE/App/usb_hid_regmap.c` | HID binary register read/write protocol |
| `tools/pwm_dashboard.py` | Python/Tkinter test GUI (PWM control, measurements, nickname) |
| `Core/Inc/FreeRTOSConfig.h` | FreeRTOS kernel config (8KB heap, priority thresholds) |

## Gotchas for Next Developer

1. **I2C direction naming**: STM32F4 HAL `I2C_DIRECTION_TRANSMIT` = master writes, `I2C_DIRECTION_RECEIVE` = master reads. Counterintuitive.
2. **ISR priorities vs FreeRTOS**: TIM2 (1) and I2C (2) are above `configMAX_SYSCALL_INTERRUPT_PRIORITY` (5). They must NEVER call FreeRTOS `*FromISR()` APIs. USB OTG_FS (6) is below threshold — CAN call `*FromISR()`.
3. **CubeMX regeneration**: Re-comment `SVC_Handler` and `PendSV_Handler` in `stm32f4xx_it.c` / `.h` after regeneration.
4. **INDIRECTTI polarity**: The edge condition in `Capture_Reconfigure()` looks backwards but is correct. `EDGE_RISING` configures CH1=Falling, CH2=Rising(indirect) to measure high-time duty. See memory note `feedback_indirectti_polarity.md`.
5. **ConfigData_t layout**: Changing the struct requires updating `CONFIG_MAGIC` (currently `0xDEADBEF5`). Old config with mismatched magic silently resets to defaults.
6. **PWM staging protocol**: Writing FREQ_L, FREQ_H, DUTY only updates shadow variables. Writing CTRL commits to hardware and fires trigger on PA7.
7. **USB composite**: CDC and HID share the register map through `regmap.c`. FreeRTOS mutex protects writes from CDC/HID tasks; I2C ISR bypasses mutex (above FreeRTOS threshold, self-serializing).

## Handoff Notes

The firmware and test GUI are fully functional. All interfaces work: frequency/duty measurement, I2C register protocol, USB CDC SCPI console, USB HID binary access, dual PWM outputs, flash persistence, device nickname.

The PWM Dashboard GUI (`tools/pwm_dashboard.py`) now supports:
- Multi-device selection with FanTestKit-411 identification by serial number
- Device info display (VID, PID, serial, nickname)
- Nickname editing (Apply/Reset)
- Capture edge selection (Rising = high-time duty, Falling = low-time duty)
- Auto-refresh pauses when capture is off
- **Read device state on connect**: sequential SCPI query queue reads capture on/off, edge, PWM1/PWM2 frequency, duty, and enable state — GUI controls sync to device
- **Checkbox controls**: Capture and PWM Enable use `ttk.Checkbutton` (replacing toggle buttons), reflecting actual device state

Potential next steps:
- Defer `Config_Save()` from I2C ISR to a FreeRTOS task
- Add HID protocol support to the PWM Dashboard GUI (currently CDC only)
- Add firmware version display in the GUI device info bar (query `*IDN?`)
- Consider hardware one-pulse mode on TIM3 for precise trigger pulse timing
