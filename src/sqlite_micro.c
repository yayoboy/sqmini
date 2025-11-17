#include "sqlite_micro/sqlite_micro.h"
#include "sqlite3.h"
#include <string.h>
#include <stdlib.h>

#ifdef SQLITE_PLATFORM_ESP32
#include <esp_log.h>
#include <esp_system.h>
#define LOG_TAG "SQLiteMicro"
#define LOG_INFO(fmt, ...) ESP_LOGI(LOG_TAG, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) ESP_LOGE(LOG_TAG, fmt, ##__VA_ARGS__)
#else
#include <stdio.h>
#define LOG_INFO(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#endif

static int sqlite_initialized = 0;

int sqlite_micro_init(void) {
    if (sqlite_initialized) {
        return SQLITE_OK;
    }

    int rc = sqlite3_initialize();
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to initialize SQLite: %d", rc);
        return rc;
    }

    sqlite_initialized = 1;
    LOG_INFO("SQLite initialized successfully");
    return SQLITE_OK;
}

int sqlite_micro_register_vfs(sqlite_storage_type_t storage_type, const char *vfs_name) {
    extern int sqlite_vfs_register_spiffs(const char *name);
    extern int sqlite_vfs_register_littlefs(const char *name);
    extern int sqlite_vfs_register_sdcard(const char *name);

    int rc = SQLITE_ERROR;

    switch (storage_type) {
        case SQLITE_STORAGE_SPIFFS:
            #ifdef SQLITE_PLATFORM_ESP32
            rc = sqlite_vfs_register_spiffs(vfs_name);
            #else
            LOG_ERROR("SPIFFS not supported on this platform");
            rc = SQLITE_ERROR;
            #endif
            break;

        case SQLITE_STORAGE_LITTLEFS:
            rc = sqlite_vfs_register_littlefs(vfs_name);
            break;

        case SQLITE_STORAGE_SDCARD:
            rc = sqlite_vfs_register_sdcard(vfs_name);
            break;

        case SQLITE_STORAGE_FLASH:
            LOG_ERROR("Flash VFS not yet implemented");
            rc = SQLITE_ERROR;
            break;

        default:
            LOG_ERROR("Unknown storage type: %d", storage_type);
            rc = SQLITE_ERROR;
    }

    if (rc == SQLITE_OK) {
        LOG_INFO("VFS registered: %s", vfs_name);
    }

    return rc;
}

int sqlite_micro_open(const char *filename, sqlite3 **ppDb, sqlite_vfs_config_t *config) {
    if (!sqlite_initialized) {
        int rc = sqlite_micro_init();
        if (rc != SQLITE_OK) {
            return rc;
        }
    }

    /* Initialize storage if config provided */
    if (config != NULL) {
        int rc = sqlite_micro_storage_init(config);
        if (rc != SQLITE_OK) {
            return rc;
        }
    }

    /* Open database */
    int rc = sqlite3_open(filename, ppDb);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to open database: %s", sqlite3_errmsg(*ppDb));
        sqlite3_close(*ppDb);
        return rc;
    }

    /* Set cache size if specified */
    if (config != NULL && config->cache_size > 0) {
        char pragma[64];
        snprintf(pragma, sizeof(pragma), "PRAGMA cache_size=-%zu", config->cache_size);
        sqlite3_exec(*ppDb, pragma, NULL, NULL, NULL);
    }

    /* Set other optimizations for embedded systems */
    sqlite3_exec(*ppDb, "PRAGMA synchronous=NORMAL", NULL, NULL, NULL);
    sqlite3_exec(*ppDb, "PRAGMA journal_mode=MEMORY", NULL, NULL, NULL);
    sqlite3_exec(*ppDb, "PRAGMA temp_store=MEMORY", NULL, NULL, NULL);

    LOG_INFO("Database opened: %s", filename);
    return SQLITE_OK;
}

int sqlite_micro_storage_init(sqlite_vfs_config_t *config) {
    if (config == NULL) {
        return SQLITE_ERROR;
    }

    int rc = SQLITE_OK;

    switch (config->storage_type) {
        case SQLITE_STORAGE_SPIFFS:
            #ifdef SQLITE_PLATFORM_ESP32
            rc = sqlite_micro_init_spiffs(config->mount_point, 5);
            #else
            LOG_ERROR("SPIFFS not supported on this platform");
            rc = SQLITE_ERROR;
            #endif
            break;

        case SQLITE_STORAGE_LITTLEFS:
            #ifdef SQLITE_PLATFORM_ESP32
            rc = sqlite_micro_init_littlefs_esp32(config->mount_point, 5);
            #elif defined(SQLITE_PLATFORM_RP2040)
            rc = sqlite_micro_init_littlefs_rp2040();
            #endif
            break;

        case SQLITE_STORAGE_SDCARD:
            LOG_INFO("SD Card initialization should be done separately");
            rc = SQLITE_OK;
            break;

        default:
            LOG_ERROR("Unknown storage type");
            rc = SQLITE_ERROR;
    }

    return rc;
}

void sqlite_micro_shutdown(void) {
    if (sqlite_initialized) {
        sqlite3_shutdown();
        sqlite_initialized = 0;
        LOG_INFO("SQLite shutdown complete");
    }
}

int sqlite_micro_get_stats(sqlite_micro_stats_t *stats) {
    if (stats == NULL) {
        return SQLITE_ERROR;
    }

    stats->current_memory = sqlite3_memory_used();
    stats->peak_memory = sqlite3_memory_highwater(0);
    stats->page_cache_size = 0; /* Would need to track separately */

    return SQLITE_OK;
}
