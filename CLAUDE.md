# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

STM32F411CEUx frequency counter with I2C slave + USB CDC + USB HID interfaces. Measures signal frequency, period, duty cycle, and pulse width on PA15, plus two independent PWM outputs (PA8, PB6) with auto-prescaler — all controllable via I2C register-based protocol at slave address 0x08, USB CDC text console, or USB HID binary register access.

## Build

This is an **STM32CubeIDE** managed project (no standalone Makefile/CMake). Build via the IDE or using the headless builder:

```bash
# Build from command line (requires STM32CubeIDE installed)
stm32cubeide --launcher.suppressErrors -nosplash \
  -application org.eclipse.cdt.managedbuilder.core.headlessbuild \
  -data /tmp/workspace -import . -build Frequency_Counter/Debug
```

Target: STM32F411CEUx (Cortex-M4, 512KB Flash, 128KB RAM)
Toolchain: arm-none-eabi-gcc, FPUv4-SP-D16, hard float ABI
Linker script: `STM32F411CEUX_FLASH.ld`

## Architecture

### CubeMX Code Generation

The `.ioc` file (`Frequency_Counter.ioc`) drives code generation. All peripheral init files are CubeMX-generated. **Custom code must go inside `/* USER CODE BEGIN/END */` blocks** to survive regeneration.

### Clock Tree

- HSE 25MHz → PLL (M=25, N=192, P=2, Q=4) → **SYSCLK 96MHz**, **USB 48MHz**
- APB1: 48MHz, APB2: 96MHz
- TIM2 clock: **96MHz** (APB1 timer multiplier ×2)
- USB OTG FS: **48MHz** (from PLLQ)

### Peripheral Configuration

| Peripheral | Purpose | Pins |
|-----------|---------|------|
| TIM2 CH1 | Input capture (frequency input) | PA15 (AF1) — remapped from CubeMX's PA0 in USER CODE |
| TIM2 CH2 | Indirect capture (pulse width) | Same TI1 input, opposite edge |
| I2C1 | Slave interface (addr 0x08) | PB7 (SDA), PB8 (SCL) — addr overridden from CubeMX's 0x7E in USER CODE |
| GPIO PC13 | Status LED (active-low) | PC13 — initialized manually in USER CODE, not in CubeMX |
| GPIO PC14 | Green LED (active-high) | PC14 — initialized manually in USER CODE |
| GPIO PB10 | Red LED (active-high) | PB10 — initialized manually in USER CODE |
| TIM1 CH1 | PWM output 1 | PA8 (AF1) — initialized manually in USER CODE |
| TIM4 CH1 | PWM output 2 | PB6 (AF2) — initialized manually in USER CODE |
| GPIO PA7 | Trigger pulse output | PA7 — fires on PWM parameter apply |
| USB_OTG_FS | USB CDC + HID composite | PA11 (DM), PA12 (DP) — CDC serial console + HID binary register access |
| Flash Sector 7 | Config storage (0x08060000) | Stores all settings with magic number validation |

### Data Flow

```
PA15 signal edge → TIM2 slave-reset mode auto-resets counter
  → CH1 captures period, CH2 captures pulse width
  → HAL_TIM_IC_CaptureCallback computes freq/duty/period/pulse
  → volatile globals updated atomically (32-bit Cortex-M4)

I2C master read → I2C1_EV_IRQHandler → HAL_I2C_AddrCallback
  → register-based protocol: master writes reg addr, reads data
  → I2C_BuildTxBuffer() snapshots register map for burst reads
  → ListenCpltCallback re-arms for next transaction

USB CDC/HID → OTG_FS_IRQHandler (priority 6, can call FreeRTOS APIs)
  → HAL_PCD callbacks → USBD_LL callbacks → composite class dispatch
  → CDC_Receive / HID OUT callback → USB_EnqueueCdcRx/HidRx (xQueueSendFromISR)
  → UsbTask dequeues → CDC_ProcessRxData / HID_ProcessReport
  → RegMap_BuildSnapshot (reads) / RegMap_Write (writes, mutex-protected)
```

### USB Composite Device

**CDC** (text console, bulk endpoints): SCPI commands like `*IDN?`, `SYST:NAME "BENCH-1"`, `SOUR:PWM1:FREQ 1000`, `*SAV`, `SYST:HELP?`.
**HID** (binary register access, interrupt endpoints): 64-byte reports with read/write protocol.

Both interfaces share the register map through `regmap.c` with FreeRTOS mutex protection for writes.

### I2C Register Map

| Addr | Name | Size | R/W | Description |
|------|------|------|-----|-------------|
| 0x00 | PERIOD | 4B | R | Period in timer ticks |
| 0x04 | FREQ | 4B | R | Frequency in Hz |
| 0x08 | DUTY | 4B | R | Duty cycle in 0.01% units |
| 0x0C | PULSE | 4B | R | Pulse width in ticks |
| 0x10 | EDGE | 1B | R/W | Capture edge (0=rising, 1=falling) |
| 0x11 | TIM_PSC | 2B | R/W | Timer prescaler (0-65535) |
| 0x13 | IC_PSC | 1B | R/W | IC prescaler (0=DIV1..3=DIV8) |
| 0x14 | CAPTURE_CTRL | 1B | R/W | Capture enable (0=off, 1=on, default: 1) |
| 0x15-0x1F | (reserved) | 11B | — | Zero-filled gap |
| 0x20 | LED_PERIOD | 2B | R/W | Status LED blink period in ms (default: 1000) |
| 0x22 | LED_DUTY | 1B | R/W | Status LED on-duty 0-100% (default: 50) |
| 0x23 | LED_G_PERIOD | 2B | R/W | Green LED blink period in ms (default: 1000) |
| 0x25 | LED_G_DUTY | 1B | R/W | Green LED on-duty 0-100% (default: 50) |
| 0x26 | LED_R_PERIOD | 2B | R/W | Red LED blink period in ms (default: 1000) |
| 0x28 | LED_R_DUTY | 1B | R/W | Red LED on-duty 0-100% (default: 50) |
| 0x30 | SAVE_CFG | 1B | W | Write 0x5A to save all config to flash |
| 0x40 | PWM1_FREQ_L | 2B | R/W | PWM1 target frequency low 16 bits (Hz) |
| 0x42 | PWM1_FREQ_H | 2B | R/W | PWM1 target frequency high 16 bits (Hz) |
| 0x44 | PWM1_DUTY | 2B | R/W | PWM1 duty cycle 0-10000 (0.01% units) |
| 0x46 | PWM1_CTRL | 1B | R/W | bit0=enable; writing applies staged FREQ+DUTY |
| 0x47 | PWM1_PSC | 2B | R | Auto-computed prescaler (debug) |
| 0x49 | PWM1_ARR | 2B | R | Auto-computed ARR (debug) |
| 0x4B | PWM2_FREQ_L | 2B | R/W | PWM2 target frequency low 16 bits (Hz) |
| 0x4D | PWM2_FREQ_H | 2B | R/W | PWM2 target frequency high 16 bits (Hz) |
| 0x4F | PWM2_DUTY | 2B | R/W | PWM2 duty cycle 0-10000 (0.01% units) |
| 0x51 | PWM2_CTRL | 1B | R/W | bit0=enable; writing applies staged FREQ+DUTY |
| 0x52 | PWM2_PSC | 2B | R | Auto-computed prescaler (debug) |
| 0x54 | PWM2_ARR | 2B | R | Auto-computed ARR (debug) |
| 0x56 | TRIG_WIDTH | 2B | R/W | Trigger pulse width in us (1-1000, default: 10) |
| 0x58-0x5F | (reserved) | 8B | — | Zero-filled gap |
| 0x60 | NICKNAME | 16B | R/W | Device nickname, NUL-padded ASCII (default: serial number hex) |

All multi-byte values are little-endian. Config persists across power cycles via flash sector 7.

**PWM staging**: Writing FREQ_L, FREQ_H, DUTY only stages values. Writing CTRL commits them atomically to hardware and fires a trigger pulse on PA7. 32-bit frequency is split into two 16-bit registers because I2C writes send at most 2 data bytes per transaction.

**Auto-prescaler**: PWM_ComputeParams() maximizes ARR (duty resolution) for the target frequency. Both TIM1 and TIM4 run at 100 MHz. Frequency range: ~2 Hz to ~50 MHz.

**Glitch-free updates**: TIM1/TIM4 use ARR preload (ARPE) and OC preload. New PSC/ARR/CCR are written to shadow registers, then a forced update event loads them atomically.

**Burst reads**: Registers are contiguous in a 112-byte map (0x00-0x6F). A single read can span multiple registers — the slave builds a snapshot and sends from the start address onward. E.g., read 16 bytes from 0x00 returns PERIOD+FREQ+DUTY+PULSE. The master NACKs/STOPs to end.

### Key Code Locations

- **Application logic** (callbacks, reconfigure, timeout, UsbTask): `Core/Src/main.c` USER CODE sections
- **Shared register map** (read/write for I2C + USB): `Core/Src/regmap.c`, `Core/Inc/regmap.h`
- **TIM2 PWM Input setup** (CH2 + slave reset + PA15 remap): `Core/Src/tim.c` USER CODE sections
- **I2C address override**: `Core/Src/i2c.c` USER CODE BEGIN I2C1_Init 2
- **IRQ handlers** (TIM2, I2C, OTG_FS): `Core/Src/stm32f4xx_it.c` USER CODE BEGIN 1
- **HAL module enables**: `Core/Inc/stm32f4xx_hal_conf.h`
- **USB composite class** (CDC + HID): `Middlewares/ST/STM32_USB_Device_Library/Class/Composite/`
- **USB application layer** (conf, desc, CDC cmd parser, HID handler): `USB_DEVICE/App/`
- **CubeMX config**: `Frequency_Counter.ioc`

### FreeRTOS Integration

- **Kernel**: FreeRTOS v10.3.1 (native API, no CMSIS-RTOS wrapper), source in `Middlewares/Third_Party/FreeRTOS/Source/`
- **Config**: `Core/Inc/FreeRTOSConfig.h` — 8KB heap, heap_4 allocator
- **SysTick**: Custom `SysTick_Handler` in `stm32f4xx_it.c` calls `HAL_IncTick()` always, then `xPortSysTickHandler()` once scheduler is running

| Task | Priority | Stack | Purpose |
|------|----------|-------|---------|
| UsbTask | 2 | 512 words | Processes CDC commands and HID reports from event queue |
| LedTask | 1 | 256 words | Blinks all 3 LEDs with configurable period/duty |
| MonitorTask | 1 | 256 words | Zeros measurements on capture timeout (1s) |
| Idle (auto) | 0 | 128 words | FreeRTOS idle task |

### NVIC Priorities

| IRQ | Priority | Rationale |
|-----|----------|-----------|
| TIM2 | 1 (highest) | Capture must not miss edges; above FreeRTOS API threshold |
| I2C1_EV | 2 | I2C clock stretching; above FreeRTOS API threshold |
| I2C1_ER | 2 | Error recovery; above FreeRTOS API threshold |
| OTG_FS | 6 | USB device; below FreeRTOS threshold — CAN call `*FromISR()` APIs |
| SysTick | 15 | FreeRTOS tick + HAL tick via hook |

`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`. TIM2 (1) and I2C (2) are above this threshold — they must NOT call any FreeRTOS `*FromISR()` API (and they don't).

### CubeMX Regeneration Caveat

After CubeMX regeneration, these changes outside USER CODE blocks must be re-applied:
1. **`stm32f4xx_it.c`**: Re-comment `SVC_Handler` and `PendSV_Handler` (FreeRTOS provides them via `#define` mapping)
2. **`stm32f4xx_it.h`**: Re-comment prototypes for `SVC_Handler` and `PendSV_Handler`
3. **`SysTick_Handler`** in `stm32f4xx_it.c` is kept but the FreeRTOS call in USER CODE block survives regeneration
4. **USB middleware** in `Middlewares/ST/STM32_USB_Device_Library/` and `USB_DEVICE/` are NOT CubeMX-managed — they survive regeneration

## Conventions

- **I2C direction**: STM32F4 HAL `I2C_DIRECTION_TRANSMIT` = master writes, `I2C_DIRECTION_RECEIVE` = master reads. Check `TransferDirection == I2C_DIRECTION_TRANSMIT` for slave receive path.
- **I2C register address capture**: `i2c_reg_addr` is set in the read-phase `AddrCallback` from `i2c_rx_buf[0]`, not in `SlaveRxCpltCallback` (which doesn't fire when master sends only 1 byte before repeated start).
- All custom code lives in `/* USER CODE BEGIN/END */` sections — never edit generated code outside these markers
- CubeMX overrides (GPIO remap, I2C address) are done in USER CODE sections after the generated init, not by modifying the `.ioc`
- HAL callback pattern: thin IRQ handlers in `stm32f4xx_it.c` delegate to `HAL_*_IRQHandler()`, which dispatches to `HAL_*_Callback()` overrides in `main.c`
- `FreqCounter_Reconfigure()` in `main.c` is the single function to call when any timer parameter changes (edge, prescalers) — it stops, reconfigures, and restarts both channels
- `Config_Save()` erases flash sector 7 and writes a `ConfigData_t` struct with magic number — called via I2C write 0x5A to reg 0x30
- `Config_Load()` runs at boot before peripherals start — validates magic, applies saved settings or keeps defaults
- `PWM_Apply()` is the single function for glitch-free PWM updates — computes PSC/ARR/CCR, writes shadow registers, forces UEV
- `PWM_ComputeParams()` auto-selects optimal prescaler to maximize ARR (duty resolution) for target frequency
- `Trigger_Pulse()` outputs a configurable-width pulse on PA7 when PWM parameters are applied via CTRL register write
- PWM timer handles (`htim1_pwm`, `htim4_pwm`) and all PWM logic are `static` in `main.c` — no external references needed
- CONFIG_MAGIC bumped to `0xDEADBEF5` when nickname register added (previously `0xDEADBEF4` for capture_ctrl) — old config auto-resets to defaults
- USB composite device uses I-CUBE-USBD-Composite architecture pattern (https://github.com/alambe94/I-CUBE-USBD-Composite)
- `regmap.c` is the shared register access layer — I2C, CDC, and HID all go through `RegMap_BuildSnapshot()` / `RegMap_Write()`
- FreeRTOS mutex protects concurrent register writes from CDC/HID tasks; I2C ISR bypasses mutex (above FreeRTOS threshold, self-serializing)
