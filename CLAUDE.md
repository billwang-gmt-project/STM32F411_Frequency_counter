# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

STM32F411CEUx frequency counter with I2C slave interface. Measures signal frequency, period, duty cycle, and pulse width on PA15, exposable via I2C register-based protocol at slave address 0x08.

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

- HSI 16MHz → PLL (M=8, N=100, P=2) → **SYSCLK 100MHz**
- APB1: 50MHz, APB2: 100MHz
- TIM2 clock: **100MHz** (APB1 timer multiplier ×2)

### Peripheral Configuration

| Peripheral | Purpose | Pins |
|-----------|---------|------|
| TIM2 CH1 | Input capture (frequency input) | PA15 (AF1) — remapped from CubeMX's PA0 in USER CODE |
| TIM2 CH2 | Indirect capture (pulse width) | Same TI1 input, opposite edge |
| I2C1 | Slave interface (addr 0x08) | PB7 (SDA), PB8 (SCL) — addr overridden from CubeMX's 0x7E in USER CODE |
| GPIO PC13 | Status LED (active-low) | PC13 — initialized manually in USER CODE, not in CubeMX |
| GPIO PC14 | Green LED (active-high) | PC14 — initialized manually in USER CODE |
| GPIO PB10 | Red LED (active-high) | PB10 — initialized manually in USER CODE |
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
```

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
| 0x14-0x1F | (reserved) | 12B | — | Zero-filled gap |
| 0x20 | LED_PERIOD | 2B | R/W | Status LED blink period in ms (default: 1000) |
| 0x22 | LED_DUTY | 1B | R/W | Status LED on-duty 0-100% (default: 50) |
| 0x23 | LED_G_PERIOD | 2B | R/W | Green LED blink period in ms (default: 1000) |
| 0x25 | LED_G_DUTY | 1B | R/W | Green LED on-duty 0-100% (default: 50) |
| 0x26 | LED_R_PERIOD | 2B | R/W | Red LED blink period in ms (default: 1000) |
| 0x28 | LED_R_DUTY | 1B | R/W | Red LED on-duty 0-100% (default: 50) |
| 0x30 | SAVE_CFG | 1B | W | Write 0x5A to save all config to flash |

All multi-byte values are little-endian. Config persists across power cycles via flash sector 7.

**Burst reads**: Registers are contiguous in a 41-byte map (0x00–0x28). A single read can span multiple registers — the slave builds a snapshot and sends from the start address onward. E.g., read 16 bytes from 0x00 returns PERIOD+FREQ+DUTY+PULSE. The master NACKs/STOPs to end.

### Key Code Locations

- **Application logic** (callbacks, reconfigure, timeout): `Core/Src/main.c` USER CODE sections
- **TIM2 PWM Input setup** (CH2 + slave reset + PA15 remap): `Core/Src/tim.c` USER CODE sections
- **I2C address override**: `Core/Src/i2c.c` USER CODE BEGIN I2C1_Init 2
- **IRQ handlers**: `Core/Src/stm32f4xx_it.c` USER CODE BEGIN 1
- **HAL module enables**: `Core/Inc/stm32f4xx_hal_conf.h`
- **CubeMX config**: `Frequency_Counter.ioc`

### FreeRTOS Integration

- **Kernel**: FreeRTOS v10.3.1 (native API, no CMSIS-RTOS wrapper), source in `Middlewares/Third_Party/FreeRTOS/Source/`
- **Config**: `Core/Inc/FreeRTOSConfig.h` — 8KB heap, heap_4 allocator
- **SysTick**: Custom `SysTick_Handler` in `stm32f4xx_it.c` calls `HAL_IncTick()` always, then `xPortSysTickHandler()` once scheduler is running

| Task | Priority | Stack | Purpose |
|------|----------|-------|---------|
| LedTask | 1 | 256 words | Blinks all 3 LEDs with configurable period/duty |
| MonitorTask | 1 | 256 words | Zeros measurements on capture timeout (1s) |
| Idle (auto) | 0 | 128 words | FreeRTOS idle task |

### NVIC Priorities

| IRQ | Priority | Rationale |
|-----|----------|-----------|
| TIM2 | 1 (highest) | Capture must not miss edges; above FreeRTOS API threshold |
| I2C1_EV | 2 | I2C clock stretching; above FreeRTOS API threshold |
| I2C1_ER | 2 | Error recovery; above FreeRTOS API threshold |
| SysTick | 15 | FreeRTOS tick + HAL tick via hook |

`configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5`. TIM2 (1) and I2C (2) are above this threshold — they must NOT call any FreeRTOS `*FromISR()` API (and they don't).

### CubeMX Regeneration Caveat

After CubeMX regeneration, these changes outside USER CODE blocks must be re-applied:
1. **`stm32f4xx_it.c`**: Re-comment `SVC_Handler` and `PendSV_Handler` (FreeRTOS provides them via `#define` mapping)
2. **`stm32f4xx_it.h`**: Re-comment prototypes for `SVC_Handler` and `PendSV_Handler`
3. **`SysTick_Handler`** in `stm32f4xx_it.c` is kept but the FreeRTOS call in USER CODE block survives regeneration

## Conventions

- **I2C direction**: STM32F4 HAL `I2C_DIRECTION_TRANSMIT` = master writes, `I2C_DIRECTION_RECEIVE` = master reads. Check `TransferDirection == I2C_DIRECTION_TRANSMIT` for slave receive path.
- **I2C register address capture**: `i2c_reg_addr` is set in the read-phase `AddrCallback` from `i2c_rx_buf[0]`, not in `SlaveRxCpltCallback` (which doesn't fire when master sends only 1 byte before repeated start).
- All custom code lives in `/* USER CODE BEGIN/END */` sections — never edit generated code outside these markers
- CubeMX overrides (GPIO remap, I2C address) are done in USER CODE sections after the generated init, not by modifying the `.ioc`
- HAL callback pattern: thin IRQ handlers in `stm32f4xx_it.c` delegate to `HAL_*_IRQHandler()`, which dispatches to `HAL_*_Callback()` overrides in `main.c`
- `FreqCounter_Reconfigure()` in `main.c` is the single function to call when any timer parameter changes (edge, prescalers) — it stops, reconfigures, and restarts both channels
- `Config_Save()` erases flash sector 7 and writes a `ConfigData_t` struct with magic number — called via I2C write 0x5A to reg 0x30
- `Config_Load()` runs at boot before peripherals start — validates magic, applies saved settings or keeps defaults
