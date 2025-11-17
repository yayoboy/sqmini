# Building SQLite Micro

This guide explains how to build and use SQLite Micro on different platforms.

## Prerequisites

### For ESP32

- **PlatformIO** or **Arduino IDE**
- **ESP32 Board Support**
- **USB Driver** for your ESP32 board

### For RP2040

- **PlatformIO** or **Arduino IDE**
- **RP2040 Board Support** (arduino-pico or mbed)
- **USB Cable** for programming

## Building with PlatformIO (Recommended)

### ESP32 Projects

1. Navigate to the example directory:
```bash
cd examples/esp32/littlefs
```

2. Copy the appropriate platformio.ini:
```bash
cp ../../../platformio/esp32/platformio.ini .
```

3. Build the project:
```bash
pio run -e esp32-littlefs
```

4. Upload to your board:
```bash
pio run -e esp32-littlefs -t upload
```

5. Monitor serial output:
```bash
pio device monitor -b 115200
```

### RP2040 Projects

1. Navigate to the example directory:
```bash
cd examples/rp2040/littlefs
```

2. Copy the appropriate platformio.ini:
```bash
cp ../../../platformio/rp2040/platformio.ini .
```

3. Build the project:
```bash
pio run -e pico-littlefs
```

4. Upload to your board:
```bash
pio run -e pico-littlefs -t upload
```

5. Monitor serial output:
```bash
pio device monitor -b 115200
```

## Building with Arduino IDE

### Installation

1. Download or clone this repository
2. Copy the entire `sqmini` folder to your Arduino libraries folder:
   - **Windows**: `Documents\Arduino\libraries\`
   - **macOS**: `~/Documents/Arduino/libraries/`
   - **Linux**: `~/Arduino/libraries/`

### ESP32 Setup

1. Install ESP32 board support:
   - Open Arduino IDE
   - Go to File → Preferences
   - Add to "Additional Board Manager URLs":
     ```
     https://dl.espressif.com/dl/package_esp32_index.json
     ```
   - Go to Tools → Board → Boards Manager
   - Search for "esp32" and install

2. Install required libraries:
   - For LittleFS: Library Manager → "ESP32 LittleFS"

3. Select your board:
   - Tools → Board → ESP32 Arduino → ESP32 Dev Module

4. Open an example:
   - File → Examples → SQLite Micro → esp32_littlefs

5. Compile and upload

### RP2040 Setup

1. Install RP2040 board support:
   - Open Arduino IDE
   - Go to File → Preferences
   - Add to "Additional Board Manager URLs":
     ```
     https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
     ```
   - Go to Tools → Board → Boards Manager
   - Search for "pico" and install "Raspberry Pi Pico/RP2040"

2. Install required libraries:
   - Library Manager → "Adafruit LittleFS"
   - For SD Card: Library Manager → "SdFat"

3. Select your board:
   - Tools → Board → Raspberry Pi Pico/RP2040 → Raspberry Pi Pico

4. Open an example:
   - File → Examples → SQLite Micro → rp2040_littlefs

5. Compile and upload

## Build Configurations

### Memory Optimization Levels

You can adjust memory usage by modifying `sqlite_config.h`:

#### Minimal (16KB RAM)
```c
#define SQLITE_DEFAULT_CACHE_SIZE -16
#define SQLITE_MAX_MEMORY 16384
```

#### Standard (32-64KB RAM)
```c
#define SQLITE_DEFAULT_CACHE_SIZE -32  // or -64
#define SQLITE_MAX_MEMORY 32768        // or 65536
```

#### Extended (128KB+ RAM)
```c
#define SQLITE_DEFAULT_CACHE_SIZE -128
#define SQLITE_MAX_MEMORY 131072
```

### Feature Flags

Enable/disable features in `sqlite_config.h`:

```c
// Enable FTS (Full-Text Search) - adds ~100KB code
// #define SQLITE_ENABLE_FTS5

// Enable JSON functions - adds ~50KB code
// #define SQLITE_ENABLE_JSON1

// Enable Math functions
#define SQLITE_ENABLE_MATH_FUNCTIONS

// Enable detailed error messages
#define SQLITE_ENABLE_EXPLAIN_COMMENTS
```

## Platform-Specific Notes

### ESP32

**Partition Table**: For SPIFFS or LittleFS, ensure your partition table has appropriate space:

Create `partitions.csv`:
```csv
# Name,   Type, SubType, Offset,  Size
nvs,      data, nvs,     0x9000,  0x5000
otadata,  data, ota,     0xe000,  0x2000
app0,     app,  ota_0,   0x10000, 0x140000
app1,     app,  ota_1,   0x150000,0x140000
spiffs,   data, spiffs,  0x290000,0x160000
littlefs, data, spiffs,  0x290000,0x160000
```

In `platformio.ini`:
```ini
board_build.partitions = partitions.csv
```

**Flash Mode**: Use QIO or DIO for best performance:
```ini
board_build.flash_mode = qio
board_build.f_flash = 80000000L
```

### RP2040

**Flash Layout**: Reserve space for LittleFS:

In your sketch:
```cpp
// Reserve last 512KB of 2MB flash for LittleFS
#define LFS_FLASH_OFFSET (1024 * 1024)
#define LFS_FLASH_SIZE (512 * 1024)
```

**Clock Speed**: For better performance:
```ini
board_build.f_cpu = 133000000L  ; 133MHz
```

## Troubleshooting Build Issues

### "sqlite3.c: No such file or directory"

Make sure the library is properly installed and the include path is correct:
```cpp
#include "sqlite_micro/sqlite_micro.h"  // Correct
// NOT: #include <sqlite_micro.h>
```

### "undefined reference to sqlite3_xxx"

Add to your `platformio.ini`:
```ini
build_flags =
    -I./src
    -I./include
lib_ldf_mode = deep+
```

### "section '.text' will not fit in region 'irom0_0_seg'"

Code is too large. Try:
1. Reduce cache size
2. Disable unused SQLite features in `sqlite_config.h`
3. Use a board with more flash

### ESP32: "Brownout detector was triggered"

Increase power supply current or add:
```ini
build_flags =
    -DCONFIG_ESP32_BROWNOUT_DET=0
```

### RP2040: "Not enough RAM"

Reduce cache size in your code:
```cpp
config.cache_size = 16;  // Use only 16KB cache
```

## Performance Tuning

### ESP32
- Use PSRAM if available: `CONFIG_SPIRAM_SUPPORT=y`
- Enable compiler optimization: `-O2` or `-O3`
- Use DMA for SD Card access

### RP2040
- Overclock to 133MHz or 250MHz (experimental)
- Use both cores (one for database, one for tasks)
- Enable cache: `board_build.core = earlephilhower`

## Testing

Run the examples to verify your build:

```bash
# ESP32 SPIFFS test
pio run -e esp32-spiffs -t upload && pio device monitor

# ESP32 LittleFS test
pio run -e esp32-littlefs -t upload && pio device monitor

# RP2040 LittleFS test
pio run -e pico-littlefs -t upload && pio device monitor
```

Expected output:
```
=== SQLite ESP32 LittleFS Example ===
Database opened successfully!
Table created successfully!
Data inserted successfully!
...
=== Example Complete ===
```

## Advanced Build Options

### Cross-Compilation

For custom toolchains, set in `platformio.ini`:
```ini
[env:custom]
platform = ...
platform_packages =
    toolchain-xtensa32@x.x.x
```

### Custom VFS

To create a custom VFS for your storage:

1. Copy `src/vfs/vfs_template.c` (create if needed)
2. Implement VFS callbacks
3. Register in `sqlite_micro.c`
4. Add to build

### Size Optimization

Aggressive size reduction (for <512KB flash):
```ini
build_flags =
    -Os                           ; Optimize for size
    -ffunction-sections           ; Remove unused functions
    -fdata-sections
    -Wl,--gc-sections
    -DSQLITE_OMIT_PROGRESS_CALLBACK
    -DSQLITE_OMIT_DEPRECATED
    -DSQLITE_DEFAULT_MEMSTATUS=0
```

## Getting Help

If you encounter build issues:

1. Check the [README.md](README.md) for requirements
2. Review the [examples/](examples/) for working configurations
3. Search existing GitHub issues
4. Open a new issue with:
   - Platform (ESP32/RP2040)
   - Build system (PlatformIO/Arduino)
   - Full error message
   - Your configuration

---

**Last Updated**: 2024-11-17
