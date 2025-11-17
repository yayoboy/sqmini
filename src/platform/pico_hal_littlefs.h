#ifndef PICO_HAL_LITTLEFS_H
#define PICO_HAL_LITTLEFS_H

#ifdef SQLITE_PLATFORM_RP2040

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

/* LittleFS configuration for RP2040 */
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)  /* 2MB flash */
#define LFS_FLASH_OFFSET (1024 * 1024)            /* Start at 1MB offset */
#define LFS_FLASH_SIZE (512 * 1024)               /* 512KB for filesystem */

/* Initialize LittleFS on RP2040 flash */
int pico_littlefs_init(void);

/* Mount LittleFS */
int pico_littlefs_mount(const char *mount_point);

/* Unmount LittleFS */
void pico_littlefs_unmount(void);

#endif /* SQLITE_PLATFORM_RP2040 */

#endif /* PICO_HAL_LITTLEFS_H */
