# ESP\-IDF portability

# AGENTS.md — ESP-IDF Multi-Board Build Configuration

## Objective

Implement multi-board support for ESP-IDF projects using the standard ESP-IDF VS Code extension.

Requirements:

- Use the extension's existing ESP-IDF chip target selector.
- Select the physical board using **ESP-IDF: Select Project Configuration**.
- Use `MY_BOARD` as the board identifier.
- Never override `IDF_TARGET` when selecting a board.
- Reject incompatible board/chip combinations.
- Maintain separate build directories and `sdkconfig` files per board.
- Preserve the normal Build, Flash, Monitor and Debug workflow.
- Use board-specific CMake definitions rather than `sdkconfig` parameters to identify hardware.
- Keep the central list of boards as the authoritative source of board compatibility.

## 1. Supported Boards

Start with the following board definitions:

| Board ID | ESP-IDF target |
| --- | --- |
| `tab5` | `esp32p4` |
| `waveshare86` | `esp32p4` |
| `jc8012` | `esp32p4` |
| `esp32s3` | `esp32s3` |
| `waveshare185c` | `esp32s3` |
| `linux` | `linux` |

Additional boards must be added to the central board list, CMake presets and board-specific source directories.

In p4Radio, `waveshare86`, `tab5` and `waveshare185c` are implemented. The latter
is the ESP32-S3-Touch-LCD-1.85C V2 and requires `esp32s3`. The remaining entries above
describe planned ports, not selectable configurations. TAB5 uses its own managed
BSP, SDK defaults, 16 MB partition table and `dependencies.tab5.lock`; Waveshare
retains `dependencies.lock`. S3 dependency versions are pinned in
`dependencies.waveshare185c.lock`. A generated `build/active-board` marker makes the
other cached build reconfigure on its next use after dependencies change boards.
Build boards sequentially in a checkout because `managed_components/` is shared.

## 2. Project Structure

Use the following layout:

```
project/
├── AGENTS.md
├── CMakeLists.txt
├── CMakePresets.json
├── sdkconfig.defaults
├── sdkconfig.tab5
├── sdkconfig.waveshare86
├── sdkconfig.jc8012
├── sdkconfig.esp32s3
├── sdkconfig.linux
├── .gitignore
├── cmake/
│   └── boards.cmake
├── main/
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── board.h
│   └── boards/
│       ├── tab5/board.cpp
│       ├── waveshare86/board.cpp
│       ├── jc8012/board.cpp
│       ├── esp32s3/board.cpp
│       └── linux/board.cpp
└── build/
    ├── tab5/
    ├── waveshare86/
    ├── jc8012/
    ├── esp32s3/
    └── linux/
```

The `build/` directory and its contents are generated automatically.

## 3. CMakePresets.json

Create the following file:

```json
{
  "version": 3,
  "cmakeMinimumRequired": {
    "major": 3,
    "minor": 21,
    "patch": 0
  },
  "configurePresets": [
    {
      "name": "tab5",
      "displayName": "M5Stack TAB5",
      "binaryDir": "${sourceDir}/build/tab5",
      "environment": {
        "MY_BOARD": "tab5"
      },
      "cacheVariables": {
        "SDKCONFIG": "${sourceDir}/build/tab5/sdkconfig",
        "SDKCONFIG_DEFAULTS": "sdkconfig.defaults;sdkconfig.tab5"
      }
    },
    {
      "name": "waveshare86",
      "displayName": "Waveshare 86-BOX",
      "binaryDir": "${sourceDir}/build/waveshare86",
      "environment": {
        "MY_BOARD": "waveshare86"
      },
      "cacheVariables": {
        "SDKCONFIG": "${sourceDir}/build/waveshare86/sdkconfig",
        "SDKCONFIG_DEFAULTS": "sdkconfig.defaults;sdkconfig.waveshare86"
      }
    },
    {
      "name": "jc8012",
      "displayName": "Guition JC8012",
      "binaryDir": "${sourceDir}/build/jc8012",
      "environment": {
        "MY_BOARD": "jc8012"
      },
      "cacheVariables": {
        "SDKCONFIG": "${sourceDir}/build/jc8012/sdkconfig",
        "SDKCONFIG_DEFAULTS": "sdkconfig.defaults;sdkconfig.jc8012"
      }
    },
    {
      "name": "esp32s3",
      "displayName": "Generic ESP32-S3",
      "binaryDir": "${sourceDir}/build/esp32s3",
      "environment": {
        "MY_BOARD": "esp32s3"
      },
      "cacheVariables": {
        "SDKCONFIG": "${sourceDir}/build/esp32s3/sdkconfig",
        "SDKCONFIG_DEFAULTS": "sdkconfig.defaults;sdkconfig.esp32s3"
      }
    },
    {
      "name": "linux",
      "displayName": "Linux Host",
      "binaryDir": "${sourceDir}/build/linux",
      "environment": {
        "MY_BOARD": "linux"
      },
      "cacheVariables": {
        "SDKCONFIG": "${sourceDir}/build/linux/sdkconfig",
        "SDKCONFIG_DEFAULTS": "sdkconfig.defaults;sdkconfig.linux"
      }
    }
  ]
}
```

Do not add `IDF_TARGET` to the presets. The ESP-IDF extension must remain responsible for selecting it.

## 4. Central Board List — cmake/boards.cmake

```
# Supported physical boards
set(SUPPORTED_BOARDS
    tab5
    waveshare86
    jc8012
    esp32s3
    linux
)

# Required ESP-IDF chip for each board
set(BOARD_TARGET_tab5        esp32p4)
set(BOARD_TARGET_waveshare86 esp32p4)
set(BOARD_TARGET_jc8012      esp32p4)
set(BOARD_TARGET_esp32s3     esp32s3)
set(BOARD_TARGET_linux       linux)

# Read the board selected by the CMake preset
set(MY_BOARD "$ENV{MY_BOARD}")

if(MY_BOARD STREQUAL "")
    message(FATAL_ERROR
        "MY_BOARD is not set. Select a project configuration.")
endif()

list(FIND SUPPORTED_BOARDS "${MY_BOARD}" BOARD_INDEX)

if(BOARD_INDEX EQUAL -1)
    message(FATAL_ERROR
        "Unsupported board: ${MY_BOARD}")
endif()

# Retrieve the required chip target
set(EXPECTED_TARGET "${BOARD_TARGET_${MY_BOARD}}")

# ESP-IDF resolves IDF_TARGET when project.cmake is included
if(NOT IDF_TARGET STREQUAL EXPECTED_TARGET)
    message(FATAL_ERROR
        "Board '${MY_BOARD}' requires '${EXPECTED_TARGET}', "
        "but the selected chip is '${IDF_TARGET}'")
endif()

# Generate a board-specific macro name
string(TOUPPER "${MY_BOARD}" MY_BOARD_UPPER)
string(REGEX REPLACE "[^A-Z0-9_]" "_"
    MY_BOARD_UPPER "${MY_BOARD_UPPER}")

set(MY_BOARD_DEFINE "BOARD_${MY_BOARD_UPPER}")

message(STATUS "Board:  ${MY_BOARD}")
message(STATUS "Chip:   ${IDF_TARGET}")
message(STATUS "Define: ${MY_BOARD_DEFINE}")
```

All board/chip compatibility rules must be maintained in this file.

## 5. Main CMakeLists.txt

```
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Validate selected board and chip
include(${CMAKE_CURRENT_LIST_DIR}/cmake/boards.cmake)

project(multi_board_app)
```

The compatibility check must run during CMake configuration, before application source files are compiled.

Do not silently select a default board.

## 6. Component CMakeLists.txt

Create `main/CMakeLists.txt`:

```
# Validate that the board source exists
set(BOARD_SOURCE
    "boards/${MY_BOARD}/board.cpp"
)

if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/${BOARD_SOURCE}")
    message(FATAL_ERROR
        "Missing implementation for board '${MY_BOARD}'")
endif()

idf_component_register(
    SRCS
        "main.cpp"
        "${BOARD_SOURCE}"
    INCLUDE_DIRS
        "."
)

# Expose exactly one board macro
target_compile_definitions(
    ${COMPONENT_LIB}
    PRIVATE
        ${MY_BOARD_DEFINE}=1
)
```

Only the selected board's implementation should be compiled.

## 7. Shared Board Interface

Create `main/board.h`:

```
#pragma once

void board_init();
```

Create `main/main.cpp`:

```
#include "sdkconfig.h"
#include "esp_log.h"

#include "board.h"

static const char* TAG = "APP";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Application starting");

    board_init();

    ESP_LOGI(TAG, "Application ready");
}
```

The application must call shared board interfaces rather than depending directly on specific board implementations.

## 8. Board-Specific Implementations

### main/boards/tab5/board.cpp

```
#include "sdkconfig.h"
#include "esp_log.h"
#include "board.h"

#ifndef BOARD_TAB5
#error "Incorrect board configuration for TAB5"
#endif

#ifndef CONFIG_IDF_TARGET_ESP32P4
#error "TAB5 requires ESP32-P4"
#endif

void board_init()
{
    ESP_LOGI("BOARD", "Initialising M5Stack TAB5");
}
```

### main/boards/waveshare86/board.cpp

```
#include "sdkconfig.h"
#include "esp_log.h"
#include "board.h"

#ifndef BOARD_WAVESHARE86
#error "Incorrect board configuration for Waveshare 86"
#endif

#ifndef CONFIG_IDF_TARGET_ESP32P4
#error "Waveshare 86 requires ESP32-P4"
#endif

void board_init()
{
    ESP_LOGI("BOARD", "Initialising Waveshare 86-BOX");
}
```

### main/boards/jc8012/board.cpp

```
#include "sdkconfig.h"
#include "esp_log.h"
#include "board.h"

#ifndef BOARD_JC8012
#error "Incorrect board configuration for JC8012"
#endif

#ifndef CONFIG_IDF_TARGET_ESP32P4
#error "JC8012 requires ESP32-P4"
#endif

void board_init()
{
    ESP_LOGI("BOARD", "Initialising JC8012");
}
```

### main/boards/esp32s3/board.cpp

```
#include "sdkconfig.h"
#include "esp_log.h"
#include "board.h"

#ifndef BOARD_ESP32S3
#error "Incorrect board configuration for ESP32-S3 board"
#endif

#ifndef CONFIG_IDF_TARGET_ESP32S3
#error "This board requires ESP32-S3"
#endif

void board_init()
{
    ESP_LOGI("BOARD", "Initialising ESP32-S3 board");
}
```

### main/boards/linux/board.cpp

```
#include "sdkconfig.h"
#include "esp_log.h"
#include "board.h"

#ifndef BOARD_LINUX
#error "Incorrect board configuration for Linux"
#endif

#ifndef CONFIG_IDF_TARGET_LINUX
#error "Linux board requires the Linux target"
#endif

void board_init()
{
    ESP_LOGI("BOARD", "Initialising Linux host");
}
```

These examples establish the selection and validation mechanism. Replace the initialisation functions with the required hardware setup.

## 9. SDK Configuration Files

Create the following files:

```
sdkconfig.defaults
sdkconfig.tab5
sdkconfig.waveshare86
sdkconfig.jc8012
sdkconfig.esp32s3
sdkconfig.linux
```

They may initially be empty.

Rules:

- Place common ESP-IDF settings in `sdkconfig.defaults`.
- Place hardware-dependent ESP-IDF settings in `sdkconfig.<board>`.
- Keep generated `sdkconfig` files under the board's build directory.
- Do not use `sdkconfig` options to identify the selected physical board.
- Do not change the chip target through board-specific defaults.
- Generate fresh `sdkconfig` files when changing default settings that must override previously generated values.

## 10. Git Configuration

Create `.gitignore`:

```
/build/
CMakeUserPresets.json
sdkconfig
sdkconfig.old
```

Commit all source files, CMake definitions, CMake presets and SDK configuration defaults.

Do not commit generated build outputs.

## 11. VS Code Configuration

Use the existing ESP-IDF extension configuration.

Optional `.vscode/settings.json`:

```json
{
  "idf.saveLastProjectConfiguration": true
}
```

Do not define `MY_BOARD` globally in VS Code settings. It must come from the selected CMake preset.

Do not set a global build directory or SDK configuration path that conflicts with the selected preset.

### Build procedure

1. Open the project folder in VS Code.
2. Run `ESP-IDF: Set Espressif Device Target`.
3. Select the required chip, such as `esp32p4`.
4. Run `ESP-IDF: Select Project Configuration`.
5. Select the required physical board, such as `tab5`.
6. Use the extension's standard Build command.

For Linux, select the preview `linux` target with a compatible ESP-IDF installation before selecting the `linux` project configuration.

## 12. Build Validation

The following combinations must produce these results:

| Selected board | Selected chip | Result |
| --- | --- | --- |
| `tab5` | `esp32p4` | Pass |
| `tab5` | `esp32s3` | Fail |
| `waveshare86` | `esp32p4` | Pass |
| `waveshare185c` | `esp32s3` | Pass |
| `waveshare185c` | `esp32p4` | Fail |
| `jc8012` | `esp32p4` | Pass |
| `esp32s3` | `esp32s3` | Pass |
| `esp32s3` | `esp32p4` | Fail |
| `linux` | `linux` | Pass |
| `linux` | `esp32p4` | Fail |
| Unknown | Any | Fail |
| Missing | Any | Fail |

Each board must have:

- Its own build directory.
- Its own generated `sdkconfig`.
- Its own compiled board implementation.
- Exactly one board-specific compile definition.
- A compile-time check for its required chip.

## 13. Additional Build Rules

- Preserve generated build files when rebuilding the same board.
- Never share a CMake cache between different boards.
- When the selected board changes, use its corresponding build directory.
- If an existing CMake cache contains the wrong chip target, fail rather than silently reusing it.
- If board selection changes without changing build directories, force reconfiguration.
- Keep board/chip validation in CMake and source code.
- When adding a board, update the central board list, presets, SDK defaults and implementation directory.
- Do not modify ESP-IDF itself or patch the VS Code extension.
- Retain standard ESP-IDF toolchain and component management.
- Treat the Linux target as experimental and exclude hardware components unsupported by its host implementation.

## 14. Completion Criteria

The implementation is complete when:

- Every supported board can be selected through the ESP-IDF extension.
- The chip remains controlled by the extension's normal target selector.
- Compatible boards build successfully.
- Incompatible combinations fail with meaningful error messages.
- Each board retains its own independent build output and SDK configuration.
- Switching back to a previously built board reuses its existing build environment.
- Board-specific source files compile only for their intended boards.
- The normal VS Code Build, Flash, Monitor and Debug workflow remains available for supported hardware.
