# VS Code Build & Debug Migration — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable building and debugging the FanTestKit STM32F411 project in VS Code using CMake + Cortex-Debug + OpenOCD, while preserving STM32CubeIDE compatibility.

**Architecture:** CMake cross-compilation with a toolchain file targeting arm-none-eabi-gcc. CMake Presets define Debug/Release configurations. VS Code extensions (CMake Tools, Cortex-Debug, C/C++) provide build UI, IntelliSense, and SWD debugging via OpenOCD + ST-Link V2.

**Tech Stack:** CMake 3.20+, Ninja, arm-none-eabi-gcc, OpenOCD, Cortex-Debug extension, CMake Tools extension

**Spec:** `docs/superpowers/specs/2026-03-26-vscode-build-debug-design.md`

---

## File Structure

| Action | File | Responsibility |
|--------|------|---------------|
| Create | `cmake/arm-none-eabi-gcc.cmake` | Cross-compilation toolchain definition |
| Create | `CMakeLists.txt` | Root build — sources, includes, defines, linker |
| Create | `CMakePresets.json` | Debug/Release configure+build presets |
| Create | `.vscode/extensions.json` | Recommended extensions for auto-install prompt |
| Create | `.vscode/settings.json` | CMake Tools + IntelliSense project settings |
| Create | `.vscode/c_cpp_properties.json` | Fallback IntelliSense configuration |
| Create | `.vscode/launch.json` | Cortex-Debug SWD debug configuration |
| Create | `.vscode/tasks.json` | Build, clean, flash, erase tasks |
| Modify | `.gitignore` | Add `build/` directory |

---

### Task 1: CMake Toolchain File

**Files:**
- Create: `cmake/arm-none-eabi-gcc.cmake`

- [ ] **Step 1: Create the `cmake/` directory and toolchain file**

```cmake
# cmake/arm-none-eabi-gcc.cmake
# Cross-compilation toolchain for ARM Cortex-M4 (STM32F411)

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Specify the cross compiler
set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

# Skip compiler test — cross-compiled ELF won't run on the host
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Search for programs on the host, libraries/headers on the target
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
```

- [ ] **Step 2: Commit**

```bash
git add cmake/arm-none-eabi-gcc.cmake
git commit -m "feat: add CMake cross-compilation toolchain file for ARM Cortex-M4"
```

---

### Task 2: Root CMakeLists.txt

**Files:**
- Create: `CMakeLists.txt`

- [ ] **Step 1: Create `CMakeLists.txt` with project setup and compiler flags**

```cmake
cmake_minimum_required(VERSION 3.20)

project(FanTestKit C ASM)

# --- MCU flags (Cortex-M4 with hardware FPU) ---
set(MCU_FLAGS "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${MCU_FLAGS} -fdata-sections -ffunction-sections -Wall")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${MCU_FLAGS}")

# Per-configuration flags
set(CMAKE_C_FLAGS_DEBUG "-g3 -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-g0 -Os")

# --- Preprocessor defines ---
add_compile_definitions(
    USE_HAL_DRIVER
    STM32F411xE
)

# --- Include paths ---
set(PROJECT_INCLUDES
    ${CMAKE_SOURCE_DIR}/Core/Inc
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Inc
    ${CMAKE_SOURCE_DIR}/Drivers/STM32F4xx_HAL_Driver/Inc/Legacy
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32F4xx/Include
    ${CMAKE_SOURCE_DIR}/Drivers/CMSIS/Include
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source/include
    ${CMAKE_SOURCE_DIR}/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F
    ${CMAKE_SOURCE_DIR}/USB_DEVICE/App
    ${CMAKE_SOURCE_DIR}/Middlewares/ST/STM32_USB_Device_Library/Core/Inc
    ${CMAKE_SOURCE_DIR}/Middlewares/ST/STM32_USB_Device_Library/Class/Composite/Inc
)

# --- Source files ---
set(CORE_SOURCES
    Core/Src/main.c
    Core/Src/gpio.c
    Core/Src/i2c.c
    Core/Src/tim.c
    Core/Src/usb_otg.c
    Core/Src/regmap.c
    Core/Src/stm32f4xx_hal_msp.c
    Core/Src/stm32f4xx_it.c
    Core/Src/syscalls.c
    Core/Src/sysmem.c
    Core/Src/system_stm32f4xx.c
)

set(HAL_SOURCES
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_dma_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_exti.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ramfunc.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_i2c_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pcd.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pcd_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_tim_ex.c
    Drivers/STM32F4xx_HAL_Driver/Src/stm32f4xx_ll_usb.c
)

set(FREERTOS_SOURCES
    Middlewares/Third_Party/FreeRTOS/Source/list.c
    Middlewares/Third_Party/FreeRTOS/Source/queue.c
    Middlewares/Third_Party/FreeRTOS/Source/tasks.c
    Middlewares/Third_Party/FreeRTOS/Source/timers.c
    Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F/port.c
    Middlewares/Third_Party/FreeRTOS/Source/portable/MemMang/heap_4.c
)

set(USB_SOURCES
    Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_core.c
    Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ctlreq.c
    Middlewares/ST/STM32_USB_Device_Library/Core/Src/usbd_ioreq.c
    Middlewares/ST/STM32_USB_Device_Library/Class/Composite/Src/usbd_composite.c
    USB_DEVICE/App/usb_device.c
    USB_DEVICE/App/usbd_conf.c
    USB_DEVICE/App/usbd_desc.c
    USB_DEVICE/App/usb_cdc_cmd.c
    USB_DEVICE/App/usb_hid_regmap.c
    USB_DEVICE/App/cdc_fifo.c
)

set(STARTUP
    Core/Startup/startup_stm32f411ceux.s
)

# --- Executable target ---
add_executable(${PROJECT_NAME}
    ${CORE_SOURCES}
    ${HAL_SOURCES}
    ${FREERTOS_SOURCES}
    ${USB_SOURCES}
    ${STARTUP}
)

target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_INCLUDES})

# --- Linker configuration ---
set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/STM32F411CEUX_FLASH.ld)

target_link_options(${PROJECT_NAME} PRIVATE
    -T${LINKER_SCRIPT}
    ${MCU_FLAGS}
    -Wl,--gc-sections
    -Wl,--print-memory-usage
    --specs=nano.specs
    --specs=nosys.specs
    -lc
    -lm
)

# Re-link when linker script changes
set_target_properties(${PROJECT_NAME} PROPERTIES
    LINK_DEPENDS ${LINKER_SCRIPT}
    SUFFIX ".elf"
)

# --- Post-build: generate .hex, .bin, print size ---
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND arm-none-eabi-objcopy -O ihex $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.hex
    COMMAND arm-none-eabi-objcopy -O binary $<TARGET_FILE:${PROJECT_NAME}> ${PROJECT_NAME}.bin
    COMMAND arm-none-eabi-size $<TARGET_FILE:${PROJECT_NAME}>
    COMMENT "Generating .hex and .bin, printing size"
)
```

- [ ] **Step 2: Commit**

```bash
git add CMakeLists.txt
git commit -m "feat: add root CMakeLists.txt for STM32F411 cross-compilation"
```

---

### Task 3: CMake Presets

**Files:**
- Create: `CMakePresets.json`

- [ ] **Step 1: Create `CMakePresets.json`**

```json
{
    "version": 6,
    "configurePresets": [
        {
            "name": "debug",
            "displayName": "Debug",
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build/Debug",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Debug",
                "CMAKE_TOOLCHAIN_FILE": "${sourceDir}/cmake/arm-none-eabi-gcc.cmake",
                "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
            }
        },
        {
            "name": "release",
            "displayName": "Release",
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build/Release",
            "cacheVariables": {
                "CMAKE_BUILD_TYPE": "Release",
                "CMAKE_TOOLCHAIN_FILE": "${sourceDir}/cmake/arm-none-eabi-gcc.cmake",
                "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
            }
        }
    ],
    "buildPresets": [
        {
            "name": "debug",
            "displayName": "Debug Build",
            "configurePreset": "debug"
        },
        {
            "name": "release",
            "displayName": "Release Build",
            "configurePreset": "release"
        }
    ]
}
```

- [ ] **Step 2: Commit**

```bash
git add CMakePresets.json
git commit -m "feat: add CMake presets for Debug and Release builds"
```

---

### Task 4: Update .gitignore

**Files:**
- Modify: `.gitignore`

- [ ] **Step 1: Append `build/` to `.gitignore`**

Add these lines at the end of the existing `.gitignore`:

```gitignore
# CMake build output
build/
```

The file should NOT have a trailing blank line after `build/`. Do not modify any existing lines.

- [ ] **Step 2: Commit**

```bash
git add .gitignore
git commit -m "chore: add CMake build/ directory to .gitignore"
```

---

### Task 5: Test the CMake Build

This task verifies the build system works before adding VS Code integration.

- [ ] **Step 1: Run CMake configure (Debug preset)**

```bash
cmake --preset debug
```

Expected output: ends with `-- Build files have been written to: .../build/Debug`. No errors.

If `arm-none-eabi-gcc` is not found, the toolchain is not on PATH. Check:
```bash
arm-none-eabi-gcc --version
```

- [ ] **Step 2: Run CMake build**

```bash
cmake --build --preset debug
```

Expected output: compiles all 48 source files, links `FanTestKit.elf`, prints memory usage like:
```
Memory region         Used Size  Region Size  %age Used
           FLASH:       xxxxx B       512 KB      x.xx%
             RAM:       xxxxx B       128 KB      x.xx%
```
No errors or warnings beyond what STM32CubeIDE already produces.

- [ ] **Step 3: Verify output files exist**

```bash
ls -la build/Debug/FanTestKit.elf build/Debug/FanTestKit.hex build/Debug/FanTestKit.bin
```

All three files should exist and be non-zero.

- [ ] **Step 4: Fix any compilation errors**

If there are errors, they likely come from:
- Missing source files in `CMakeLists.txt` → add the missing file
- Missing include paths → add to `PROJECT_INCLUDES`
- Missing defines → add to `add_compile_definitions()`

Iterate until the build succeeds with zero errors.

- [ ] **Step 5: Commit any fixes (if needed)**

```bash
git add CMakeLists.txt
git commit -m "fix: resolve CMake build errors"
```

---

### Task 6: VS Code Extensions Recommendation

**Files:**
- Create: `.vscode/extensions.json`

- [ ] **Step 1: Create `.vscode/` directory and `extensions.json`**

```json
{
    "recommendations": [
        "ms-vscode.cpptools",
        "ms-vscode.cmake-tools",
        "marus25.cortex-debug"
    ]
}
```

- [ ] **Step 2: Commit**

```bash
git add .vscode/extensions.json
git commit -m "feat: add recommended VS Code extensions for embedded development"
```

---

### Task 7: VS Code Settings

**Files:**
- Create: `.vscode/settings.json`

- [ ] **Step 1: Create `.vscode/settings.json`**

```json
{
    "cmake.configureOnOpen": true,
    "cmake.defaultConfigurePreset": "debug",
    "cmake.defaultBuildPreset": "debug",
    "C_Cpp.default.configurationProvider": "ms-vscode.cmake-tools"
}
```

- [ ] **Step 2: Commit**

```bash
git add .vscode/settings.json
git commit -m "feat: add VS Code settings for CMake Tools integration"
```

---

### Task 8: IntelliSense Fallback Configuration

**Files:**
- Create: `.vscode/c_cpp_properties.json`

- [ ] **Step 1: Create `.vscode/c_cpp_properties.json`**

This provides IntelliSense when CMake Tools hasn't configured yet (e.g., first open before configure runs).

```json
{
    "configurations": [
        {
            "name": "STM32F411",
            "compilerPath": "arm-none-eabi-gcc",
            "cStandard": "c99",
            "defines": [
                "USE_HAL_DRIVER",
                "STM32F411xE",
                "DEBUG"
            ],
            "includePath": [
                "${workspaceFolder}/Core/Inc",
                "${workspaceFolder}/Drivers/STM32F4xx_HAL_Driver/Inc",
                "${workspaceFolder}/Drivers/STM32F4xx_HAL_Driver/Inc/Legacy",
                "${workspaceFolder}/Drivers/CMSIS/Device/ST/STM32F4xx/Include",
                "${workspaceFolder}/Drivers/CMSIS/Include",
                "${workspaceFolder}/Middlewares/Third_Party/FreeRTOS/Source/include",
                "${workspaceFolder}/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F",
                "${workspaceFolder}/USB_DEVICE/App",
                "${workspaceFolder}/Middlewares/ST/STM32_USB_Device_Library/Core/Inc",
                "${workspaceFolder}/Middlewares/ST/STM32_USB_Device_Library/Class/Composite/Inc"
            ],
            "intelliSenseMode": "gcc-arm"
        }
    ],
    "version": 4
}
```

- [ ] **Step 2: Commit**

```bash
git add .vscode/c_cpp_properties.json
git commit -m "feat: add fallback IntelliSense configuration for STM32F411"
```

---

### Task 9: Debug Configuration (Cortex-Debug + OpenOCD)

**Files:**
- Create: `.vscode/launch.json`

- [ ] **Step 1: Create `.vscode/launch.json`**

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Debug (OpenOCD)",
            "type": "cortex-debug",
            "request": "launch",
            "servertype": "openocd",
            "executable": "${workspaceFolder}/build/Debug/FanTestKit.elf",
            "cwd": "${workspaceFolder}",
            "device": "STM32F411CE",
            "configFiles": [
                "interface/stlink.cfg",
                "target/stm32f4x.cfg"
            ],
            "runToEntryPoint": "main",
            "showDevDebugOutput": "none",
            "preLaunchTask": "Build (Debug)"
        }
    ]
}
```

Key settings:
- `servertype: openocd` — Cortex-Debug launches OpenOCD automatically
- `configFiles` — uses OpenOCD's built-in ST-Link interface + STM32F4 target configs
- `runToEntryPoint: main` — flashes firmware, resets, breaks at `main()`
- `preLaunchTask` — auto-builds before debugging (references task from Task 10)

- [ ] **Step 2: Commit**

```bash
git add .vscode/launch.json
git commit -m "feat: add Cortex-Debug launch config for OpenOCD + ST-Link V2"
```

---

### Task 10: Build & Flash Tasks

**Files:**
- Create: `.vscode/tasks.json`

- [ ] **Step 1: Create `.vscode/tasks.json`**

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "label": "Build (Debug)",
            "type": "shell",
            "command": "cmake",
            "args": ["--build", "--preset", "debug"],
            "group": {
                "kind": "build",
                "isDefault": true
            },
            "problemMatcher": "$gcc",
            "detail": "Build firmware (Debug configuration)"
        },
        {
            "label": "Build (Release)",
            "type": "shell",
            "command": "cmake",
            "args": ["--build", "--preset", "release"],
            "group": "build",
            "problemMatcher": "$gcc",
            "detail": "Build firmware (Release configuration)"
        },
        {
            "label": "Clean",
            "type": "shell",
            "command": "cmake",
            "args": ["--build", "--preset", "debug", "--target", "clean"],
            "problemMatcher": [],
            "detail": "Clean Debug build artifacts"
        },
        {
            "label": "Flash",
            "type": "shell",
            "command": "openocd",
            "args": [
                "-f", "interface/stlink.cfg",
                "-f", "target/stm32f4x.cfg",
                "-c", "program build/Debug/FanTestKit.elf verify reset exit"
            ],
            "problemMatcher": [],
            "detail": "Flash firmware to board via ST-Link (no debug)"
        },
        {
            "label": "Erase Chip",
            "type": "shell",
            "command": "openocd",
            "args": [
                "-f", "interface/stlink.cfg",
                "-f", "target/stm32f4x.cfg",
                "-c", "init; reset halt; stm32f4x mass_erase 0; exit"
            ],
            "problemMatcher": [],
            "detail": "Full chip erase via ST-Link"
        }
    ]
}
```

- [ ] **Step 2: Commit**

```bash
git add .vscode/tasks.json
git commit -m "feat: add VS Code tasks for build, clean, flash, and erase"
```

---

### Task 11: End-to-End Verification

This task verifies the full workflow works in VS Code.

- [ ] **Step 1: Open the project in VS Code**

```bash
code .
```

VS Code should prompt to install recommended extensions (C/C++, CMake Tools, Cortex-Debug). Install them.

- [ ] **Step 2: Verify CMake auto-configure**

After opening, CMake Tools should auto-configure using the `debug` preset. Check the Output panel (CMake/Build tab) for:
```
[cmake] -- Build files have been written to: .../build/Debug
```

If it doesn't auto-configure, run from command palette: `CMake: Configure`.

- [ ] **Step 3: Verify build via Ctrl+Shift+B**

Press `Ctrl+Shift+B`. The default "Build (Debug)" task should compile and link successfully.

- [ ] **Step 4: Verify IntelliSense**

Open `Core/Src/main.c`. Check that:
- No red squiggles on `#include` directives
- Go-to-definition works on HAL functions (e.g., Ctrl+click on `HAL_Init()`)
- Autocomplete shows STM32 HAL types

- [ ] **Step 5: Verify debug session (requires board connected)**

Connect the STM32F411 board via ST-Link. Press `F5`. Check that:
- Build runs automatically (preLaunchTask)
- OpenOCD connects to the ST-Link
- Firmware is flashed
- Execution halts at `main()`
- Breakpoints, stepping, and variable watch work

- [ ] **Step 6: Verify flash task (requires board connected)**

From command palette (`Ctrl+Shift+P`): `Tasks: Run Task` → `Flash`. Check that OpenOCD programs the board and resets it.

- [ ] **Step 7: Final commit (if any fixes were needed)**

```bash
git add -u
git commit -m "fix: resolve VS Code integration issues found during verification"
```
