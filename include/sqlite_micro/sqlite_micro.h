#ifndef SQLITE_MICRO_H
#define SQLITE_MICRO_H

#include "sqlite3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Storage types */
typedef enum {
    SQLITE_STORAGE_SPIFFS,    /* ESP32 SPIFFS */
    SQLITE_STORAGE_LITTLEFS,  /* LittleFS (ESP32/RP2040) */
    SQLITE_STORAGE_SDCARD,    /* SD Card */
    SQLITE_STORAGE_FLASH      /* Raw Flash */
} sqlite_storage_type_t;

/* VFS Configuration */
typedef struct {
    const char *mount_point;  /* Mount point for filesystem */
    const char *db_path;      /* Path to database file */
    sqlite_storage_type_t storage_type;
    size_t cache_size;        /* Cache size in KB */
} sqlite_vfs_config_t;

/* Initialize SQLite for microcontroller */
int sqlite_micro_init(void);

/* Register VFS for specific storage type */
int sqlite_micro_register_vfs(sqlite_storage_type_t storage_type, const char *vfs_name);

/* Open database with specific storage backend */
int sqlite_micro_open(const char *filename, sqlite3 **ppDb, sqlite_vfs_config_t *config);

/* Helper function to initialize storage */
int sqlite_micro_storage_init(sqlite_vfs_config_t *config);

/* Cleanup */
void sqlite_micro_shutdown(void);

/* ESP32 specific - initialize SPIFFS */
#ifdef SQLITE_PLATFORM_ESP32
int sqlite_micro_init_spiffs(const char *base_path, size_t max_files);
int sqlite_micro_init_littlefs_esp32(const char *base_path, size_t max_files);
#endif

/* RP2040 specific - initialize LittleFS */
#ifdef SQLITE_PLATFORM_RP2040
int sqlite_micro_init_littlefs_rp2040(void);
#endif

/* Common SD card initialization (ESP32/RP2040) */
int sqlite_micro_init_sdcard(int cs_pin, int mosi_pin, int miso_pin, int sck_pin);

/* Memory statistics */
typedef struct {
    size_t current_memory;
    size_t peak_memory;
    size_t page_cache_size;
} sqlite_micro_stats_t;

int sqlite_micro_get_stats(sqlite_micro_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* SQLITE_MICRO_H */
