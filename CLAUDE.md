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

### Data Flow

```
PA15 signal edge → TIM2 slave-reset mode auto-resets counter
  → CH1 captures period, CH2 captures pulse width
  → HAL_TIM_IC_CaptureCallback computes freq/duty/period/pulse
  → volatile globals updated atomically (32-bit Cortex-M4)

I2C master read → I2C1_EV_IRQHandler → HAL_I2C_AddrCallback
  → register-based protocol: master writes reg addr, reads data
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

All multi-byte values are little-endian.

### Key Code Locations

- **Application logic** (callbacks, reconfigure, timeout): `Core/Src/main.c` USER CODE sections
- **TIM2 PWM Input setup** (CH2 + slave reset + PA15 remap): `Core/Src/tim.c` USER CODE sections
- **I2C address override**: `Core/Src/i2c.c` USER CODE BEGIN I2C1_Init 2
- **IRQ handlers**: `Core/Src/stm32f4xx_it.c` USER CODE BEGIN 1
- **HAL module enables**: `Core/Inc/stm32f4xx_hal_conf.h`
- **CubeMX config**: `Frequency_Counter.ioc`

### NVIC Priorities

| IRQ | Priority | Rationale |
|-----|----------|-----------|
| TIM2 | 1 (highest) | Capture must not miss edges |
| I2C1_EV | 2 | I2C clock stretching tolerates brief delays |
| I2C1_ER | 2 | Error recovery is not time-critical |
| SysTick | 15 (default) | HAL tick increment |

## Conventions

- All custom code lives in `/* USER CODE BEGIN/END */` sections — never edit generated code outside these markers
- CubeMX overrides (GPIO remap, I2C address) are done in USER CODE sections after the generated init, not by modifying the `.ioc`
- HAL callback pattern: thin IRQ handlers in `stm32f4xx_it.c` delegate to `HAL_*_IRQHandler()`, which dispatches to `HAL_*_Callback()` overrides in `main.c`
- `FreqCounter_Reconfigure()` in `main.c` is the single function to call when any timer parameter changes (edge, prescalers) — it stops, reconfigures, and restarts both channels
