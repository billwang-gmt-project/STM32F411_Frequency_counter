# PROJECT_STATUS.md

**Project**: STM32F411 Frequency Counter with I2C Slave Interface
**Last updated**: 2026-03-25 (PWM outputs added)
**Branch**: `master`
**Latest commit**: `1e2a605` — Support burst I2C register reads and fix register address capture

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
- [x] Fix I2C slave direction bug: `TransferDirection` check was swapped in `HAL_I2C_AddrCallback` — STM32F4 HAL uses `I2C_DIRECTION_TRANSMIT` for "master writes" and `I2C_DIRECTION_RECEIVE` for "master reads"
- [x] Update CLAUDE.md, README.md with FreeRTOS architecture, register map, CubeMX regeneration caveats
- [x] Support burst I2C register reads: `I2C_BuildTxBuffer()` builds 41-byte register map snapshot, master can read any number of consecutive bytes
- [x] Fix register address capture: read `i2c_reg_addr` from `i2c_rx_buf[0]` in read-phase AddrCallback (SlaveRxCpltCallback doesn't fire on partial receive before repeated start)
- [x] Add two PWM outputs: TIM1_CH1 on PA8, TIM4_CH1 on PB6
  - Auto-prescaler: `PWM_ComputeParams()` maximizes ARR for best duty resolution at target frequency
  - Glitch-free updates: ARPE + OC preload + forced UEV via `PWM_Apply()`
  - I2C staged writes: FREQ_L, FREQ_H, DUTY are staging registers; CTRL write commits atomically
  - Trigger pulse on PA7: configurable width (1–1000 µs, default 10 µs), fires on each CTRL write
  - Full I2C register map extension (0x40–0x57): PWM1 config, PWM2 config, trigger width
  - Flash persistence: PWM settings saved/restored via extended `ConfigData_t` (magic changed to 0xDEADBEF2)
  - PSC and ARR readable via I2C for debugging
- [x] Update all documentation: CLAUDE.md, README.md, HOST_PROGRAMMING_GUIDE.md, PROJECT_STATUS.md

## Current Obstacles

- **`Config_Save()` runs from I2C ISR context** (priority 2): flash sector erase takes ~100ms, blocking all interrupts at priority >= 2 during that time. TIM2 at priority 1 is unaffected, but I2C is blocked. Consider deferring flash save to a lower-priority FreeRTOS task in a future revision.
- **CubeMX regeneration clobbers FreeRTOS handlers**: `SVC_Handler` and `PendSV_Handler` in `stm32f4xx_it.c` must be re-commented after any CubeMX code generation. The `SysTick_Handler` FreeRTOS call survives regeneration (inside USER CODE block).
- **Register address gaps**: 0x14–0x1F and 0x29–0x3F are reserved (zero-filled in burst reads). Future registers could use this space.

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
        |                                         |
+-------+--------+                    +-----------+-----------+
| MonitorTask    |                    | I2C1 Slave (0x08)    |
| (FreeRTOS, P1) |                    | Priority 2            |
| - timeout check|                    | - register read/write |
| - zeros on 1s  |                    | - config save to flash|
+----------------+                    | - PWM apply + trigger |
                                      +-----------+-----------+
+----------------+                                |
| LedTask        |                    +-----------+-----------+
| (FreeRTOS, P1) |                    | Flash Sector 7       |
| - PC13 (status)|                    | ConfigData_t         |
| - PC14 (green) |                    | 0x08060000           |
| - PB10 (red)   |                    +-----------------------+
+----------------+
                         +---------------------------+
   PA8 PWM1 <------------| TIM1 CH1 (PWM output)    |
   PB6 PWM2 <------------| TIM4 CH1 (PWM output)    |
   PA7 trigger <----------| GPIO (pulse on apply)    |
                         +---------------------------+
```

## Key Files

| File | Role |
|------|------|
| `Core/Src/main.c` | All application logic: tasks, callbacks, I2C protocol, flash config |
| `Core/Inc/FreeRTOSConfig.h` | FreeRTOS kernel config (8KB heap, priority thresholds) |
| `Core/Src/stm32f4xx_it.c` | IRQ handlers (TIM2, I2C, SysTick with FreeRTOS) |
| `Core/Src/i2c.c` | I2C1 init + address override to 0x08 |
| `Core/Src/tim.c` | TIM2 PWM input capture setup + PA15 remap |
| `Middlewares/Third_Party/FreeRTOS/Source/` | FreeRTOS v10.3.1 kernel (CM4F port, heap_4) |

## Gotchas for Next Developer

1. **I2C direction naming**: STM32F4 HAL `I2C_DIRECTION_TRANSMIT` = master writes, `I2C_DIRECTION_RECEIVE` = master reads. Counterintuitive. See `HAL_I2C_AddrCallback` in main.c.
2. **ISR priorities vs FreeRTOS**: TIM2 (1) and I2C (2) are above `configMAX_SYSCALL_INTERRUPT_PRIORITY` (5). They must NEVER call FreeRTOS `*FromISR()` APIs.
3. **CubeMX regeneration**: Re-comment `SVC_Handler` and `PendSV_Handler` in `stm32f4xx_it.c` / `.h` after regeneration. See CLAUDE.md for full checklist.
4. **`USE_RTOS` must stay 0**: STM32F4 HAL v1.28.3 hard-errors (`#error`) if `USE_RTOS=1`. FreeRTOS works fine with it at 0.
5. **ConfigData_t layout**: Changing the struct requires updating `CONFIG_MAGIC` (currently `0xDEADBEF2`). Old config with mismatched magic is silently ignored → defaults used. This is safe but be aware.
6. **I2C burst reads**: `I2C_BuildTxBuffer()` builds an 88-byte snapshot every read. Register address is captured from `i2c_rx_buf[0]` in the read-phase `AddrCallback`, not from `SlaveRxCpltCallback` (which doesn't fire on partial receive).
7. **PWM staging protocol**: Writing FREQ_L, FREQ_H, DUTY only updates shadow variables. Writing CTRL commits to hardware. The trigger pulse on PA7 fires on every CTRL write (even disable). `PWM_Apply()` runs from I2C callback (priority 2) — no FreeRTOS API calls.
8. **TIM1 is an advanced timer**: HAL_TIM_PWM_Start automatically handles MOE (Main Output Enable). TIM4 is a general-purpose timer with the same code path.
9. **Trigger pulse timing**: The busy-loop in `Trigger_Pulse()` is approximate (~25 iterations/µs at 100 MHz). Actual width varies with compiler optimization and cache state.

## Handoff Notes

The firmware is fully functional on FreeRTOS. All features work: frequency measurement, I2C single and burst register reads/writes, LED control, PWM output with auto-prescaler and glitch-free updates, trigger pulse, flash config save/restore.

Potential next steps:
- Defer `Config_Save()` from I2C ISR to a FreeRTOS task (use task notification from callback)
- Add more FreeRTOS tasks if new features are needed (UART debug output, watchdog, etc.)
- Consider adding I2C error recovery with peripheral reset on repeated failures
- Use the reserved gaps (0x14–0x1F, 0x29–0x3F) for future registers
- Consider hardware one-pulse mode on TIM3 for precise trigger pulse timing (currently software loop)
