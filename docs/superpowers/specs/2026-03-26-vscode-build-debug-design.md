# VS Code Build & Debug Migration Design

**Date:** 2026-03-26
**Status:** Draft
**Approach:** CMake + CMake Tools + Cortex-Debug + OpenOCD

## Goal

Enable building and debugging the FanTestKit STM32F411 project entirely within VS Code, while preserving full STM32CubeIDE compatibility (no existing files modified).

## Prerequisites (Windows)

Install all four tools and ensure they are on PATH:

| Tool | Install Command | Verify |
|------|----------------|--------|
| ARM GNU Toolchain | `winget install Arm.GnuToolchain` | `arm-none-eabi-gcc --version` |
| CMake (>= 3.20) | `winget install Kitware.CMake` | `cmake --version` |
| Ninja | `winget install Ninja-build.Ninja` | `ninja --version` |
| OpenOCD | `winget install xpack-dev-tools.openocd` | `openocd --version` |

After installing, restart your terminal and verify each command works.

## Files to Create

| File | Purpose |
|------|---------|
| `CMakeLists.txt` | Root CMake build definition |
| `cmake/arm-none-eabi-gcc.cmake` | Cross-compilation toolchain file |
| `CMakePresets.json` | Debug/Release build presets |
| `.vscode/settings.json` | CMake Tools project settings |
| `.vscode/launch.json` | Cortex-Debug SWD debug configuration |
| `.vscode/tasks.json` | Build, clean, flash, erase tasks |
| `.vscode/extensions.json` | Recommended extensions |
| `.vscode/c_cpp_properties.json` | Fallback IntelliSense config |

No existing files are modified. STM32CubeIDE files remain untouched.

## CMake Build Configuration

### Toolchain File (`cmake/arm-none-eabi-gcc.cmake`)

- `CMAKE_SYSTEM_NAME Generic` / `CMAKE_SYSTEM_PROCESSOR arm`
- Compiler: `arm-none-eabi-gcc` (C and ASM)
- `CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY` (skip link test)
- `CMAKE_FIND_ROOT_PATH_MODE_*` set to `ONLY`

### Root `CMakeLists.txt`

**Project:** `FanTestKit`, languages: C, ASM
**Minimum CMake:** 3.20

**Common compiler flags:**
```
-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
-fdata-sections -ffunction-sections
-Wall
```

**Per-config flags:**
- Debug: `-g3 -DDEBUG -O0`
- Release: `-g0 -Os`

**Preprocessor defines:** `USE_HAL_DRIVER`, `STM32F411xE`

**Include paths (10 directories):**
```
Core/Inc
Drivers/STM32F4xx_HAL_Driver/Inc
Drivers/STM32F4xx_HAL_Driver/Inc/Legacy
Drivers/CMSIS/Device/ST/STM32F4xx/Include
Drivers/CMSIS/Include
Middlewares/Third_Party/FreeRTOS/Source/include
Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F
USB_DEVICE/App
Middlewares/ST/STM32_USB_Device_Library/Core/Inc
Middlewares/ST/STM32_USB_Device_Library/Class/Composite/Inc
```

**Source file groups:**

- `CORE_SOURCES` (11 files): Core/Src/*.c
- `HAL_SOURCES` (20 files): All HAL driver .c files in Drivers/STM32F4xx_HAL_Driver/Src/
- `FREERTOS_SOURCES` (6 files): list.c, queue.c, tasks.c, timers.c, port.c, heap_4.c
- `USB_SOURCES` (10 files): USB device library core (3) + composite class (1) + USB_DEVICE/App (6)
- `STARTUP` (1 file): Core/Startup/startup_stm32f411ceux.s

**Linker configuration:**
```
-T${CMAKE_SOURCE_DIR}/STM32F411CEUX_FLASH.ld
-Wl,--gc-sections
-Wl,--print-memory-usage
--specs=nano.specs
--specs=nosys.specs
-lc -lm
```

**Post-build steps:**
- `arm-none-eabi-objcopy -O ihex` → `.hex`
- `arm-none-eabi-objcopy -O binary` → `.bin`
- `arm-none-eabi-size` → print flash/RAM usage

### CMake Presets (`CMakePresets.json`)

Two configure presets, two build presets:

| Preset | Build Type | Output Dir | Flags |
|--------|-----------|------------|-------|
| `debug` | Debug | `build/Debug` | `-g3 -O0 -DDEBUG` |
| `release` | Release | `build/Release` | `-g0 -Os` |

Both set `CMAKE_TOOLCHAIN_FILE` and `CMAKE_EXPORT_COMPILE_COMMANDS=ON`.

## VS Code Configuration

### Debug (`launch.json`)

- Extension: Cortex-Debug (`marus25.cortex-debug`)
- Server type: OpenOCD
- OpenOCD config: `-f interface/stlink.cfg -f target/stm32f4x.cfg`
- Executable: `${workspaceFolder}/build/Debug/FanTestKit.elf`
- GDB: `arm-none-eabi-gdb`
- Behavior: flash, reset, halt at `main()`

### Tasks (`tasks.json`)

| Task | Command | Shortcut |
|------|---------|----------|
| Build (Debug) | `cmake --build --preset debug` | Ctrl+Shift+B |
| Build (Release) | `cmake --build --preset release` | — |
| Clean | `cmake --build --preset debug --target clean` | — |
| Flash | `openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program build/Debug/FanTestKit.elf verify reset exit"` | — |
| Erase | `openocd ... -c "init; reset halt; stm32f4x mass_erase 0; exit"` | — |

### Settings (`settings.json`)

- `cmake.configureOnOpen: true`
- `C_Cpp.default.configurationProvider: ms-vscode.cmake-tools`

### Extensions (`extensions.json`)

- `ms-vscode.cpptools` — C/C++ IntelliSense
- `ms-vscode.cmake-tools` — CMake Tools
- `marus25.cortex-debug` — SWD debugging

### IntelliSense Fallback (`c_cpp_properties.json`)

Configures IntelliSense for the case CMake Tools isn't providing it:
- Compiler path: `arm-none-eabi-gcc`
- Defines: `USE_HAL_DRIVER`, `STM32F411xE`, `DEBUG`
- All 10 include paths
- IntelliSense mode: `gcc-arm`

## .gitignore Update

Add `build/` to `.gitignore` (CMake output directory). The existing `.gitignore` only covers Eclipse's `Debug/` folder.

## What Is NOT Changed

- `.cproject`, `.project`, `.launch` — STM32CubeIDE files remain untouched
- `FanTestKit.ioc` — CubeMX config unchanged
- All source code — zero modifications
- `Debug/` folder — Eclipse build output untouched

## Workflow After Setup

1. Open project in VS Code
2. Extensions prompt installs automatically
3. CMake Tools auto-configures on open (Debug preset by default)
4. **Build:** Ctrl+Shift+B or status bar button
5. **Debug:** F5 — flashes board, breaks at main()
6. **Flash only:** Run "Flash" task from command palette
7. **Switch to Release:** Change preset in CMake Tools status bar
