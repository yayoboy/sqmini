#include "sqlite_micro/sqlite_cache.h"
#include <string.h>
#include <stdlib.h>

#ifdef SQLITE_HAS_FREERTOS
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <esp_log.h>

#define LOG_TAG "SQLiteCache"

/* Cache entry */
typedef struct {
    char *sql;
    sqlite3_stmt *stmt;
    uint32_t access_count;
    uint32_t last_access_time;
    bool in_use;
} cache_entry_t;

/* Cache structure */
struct sqlite_stmt_cache_s {
    sqlite3 *db;
    cache_entry_t *entries;
    uint32_t max_entries;
    uint32_t current_size;
    sqlite_cache_config_t config;
    sqlite_cache_stats_t stats;
    SemaphoreHandle_t mutex;
};

static uint32_t get_tick_count(void) {
    return xTaskGetTickCount();
}

static uint32_t hash_sql(const char *sql) {
    uint32_t hash = 5381;
    int c;
    while ((c = *sql++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

sqlite_stmt_cache_t* sqlite_cache_create(sqlite3 *db, sqlite_cache_config_t *config) {
    if (!db) {
        ESP_LOGE(LOG_TAG, "Invalid database handle");
        return NULL;
    }

    sqlite_stmt_cache_t *cache = (sqlite_stmt_cache_t*)calloc(1, sizeof(sqlite_stmt_cache_t));
    if (!cache) {
        ESP_LOGE(LOG_TAG, "Failed to allocate cache");
        return NULL;
    }

    cache->db = db;

    if (config) {
        memcpy(&cache->config, config, sizeof(sqlite_cache_config_t));
    } else {
        sqlite_cache_config_t default_cfg = SQLITE_CACHE_DEFAULT_CONFIG();
        memcpy(&cache->config, &default_cfg, sizeof(sqlite_cache_config_t));
    }

    cache->max_entries = cache->config.max_statements;

    /* Allocate entries */
    cache->entries = (cache_entry_t*)calloc(cache->max_entries, sizeof(cache_entry_t));
    if (!cache->entries) {
        ESP_LOGE(LOG_TAG, "Failed to allocate cache entries");
        free(cache);
        return NULL;
    }

    /* Create mutex */
    cache->mutex = xSemaphoreCreateMutex();
    if (!cache->mutex) {
        ESP_LOGE(LOG_TAG, "Failed to create mutex");
        free(cache->entries);
        free(cache);
        return NULL;
    }

    ESP_LOGI(LOG_TAG, "Statement cache created with %d slots", cache->max_entries);
    return cache;
}

void sqlite_cache_destroy(sqlite_stmt_cache_t *cache) {
    if (!cache) {
        return;
    }

    /* Finalize all statements */
    for (uint32_t i = 0; i < cache->max_entries; i++) {
        if (cache->entries[i].stmt) {
            sqlite3_finalize(cache->entries[i].stmt);
        }
        if (cache->entries[i].sql) {
            free(cache->entries[i].sql);
        }
    }

    free(cache->entries);
    vSemaphoreDelete(cache->mutex);
    free(cache);

    ESP_LOGI(LOG_TAG, "Cache destroyed");
}

sqlite3_stmt* sqlite_cache_get_stmt(sqlite_stmt_cache_t *cache, const char *sql) {
    if (!cache || !sql) {
        return NULL;
    }

    xSemaphoreTake(cache->mutex, portMAX_DELAY);

    cache->stats.total_lookups++;

    /* Search for cached statement */
    for (uint32_t i = 0; i < cache->current_size; i++) {
        if (cache->entries[i].sql &&
            strcmp(cache->entries[i].sql, sql) == 0 &&
            !cache->entries[i].in_use) {

            /* Cache hit! */
            cache->entries[i].in_use = true;
            cache->entries[i].access_count++;
            cache->entries[i].last_access_time = get_tick_count();

            cache->stats.cache_hits++;
            cache->stats.hit_rate = (float)cache->stats.cache_hits / cache->stats.total_lookups;

            sqlite3_reset(cache->entries[i].stmt);
            sqlite3_clear_bindings(cache->entries[i].stmt);

            xSemaphoreGive(cache->mutex);
            return cache->entries[i].stmt;
        }
    }

    /* Cache miss - prepare new statement */
    cache->stats.cache_misses++;
    cache->stats.hit_rate = (float)cache->stats.cache_hits / cache->stats.total_lookups;

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(cache->db, sql, -1, &stmt, NULL);

    if (rc != SQLITE_OK) {
        ESP_LOGE(LOG_TAG, "Failed to prepare statement: %s", sqlite3_errmsg(cache->db));
        xSemaphoreGive(cache->mutex);
        return NULL;
    }

    /* Find empty slot or evict LRU */
    int slot = -1;

    /* First try to find empty slot */
    for (uint32_t i = 0; i < cache->max_entries; i++) {
        if (!cache->entries[i].stmt) {
            slot = i;
            break;
        }
    }

    /* If no empty slot and cache is full, evict LRU */
    if (slot == -1 && cache->current_size >= cache->max_entries) {
        uint32_t oldest_time = UINT32_MAX;
        uint32_t oldest_idx = 0;

        for (uint32_t i = 0; i < cache->max_entries; i++) {
            if (!cache->entries[i].in_use &&
                cache->entries[i].last_access_time < oldest_time) {
                oldest_time = cache->entries[i].last_access_time;
                oldest_idx = i;
            }
        }

        /* Evict */
        if (cache->entries[oldest_idx].stmt) {
            sqlite3_finalize(cache->entries[oldest_idx].stmt);
        }
        if (cache->entries[oldest_idx].sql) {
            free(cache->entries[oldest_idx].sql);
        }

        slot = oldest_idx;
        cache->stats.evictions++;
        cache->current_size--;
    } else if (slot == -1) {
        /* Use next available slot */
        slot = cache->current_size;
    }

    /* Cache the statement */
    if (slot >= 0 && slot < (int)cache->max_entries) {
        cache->entries[slot].sql = strdup(sql);
        cache->entries[slot].stmt = stmt;
        cache->entries[slot].in_use = true;
        cache->entries[slot].access_count = 1;
        cache->entries[slot].last_access_time = get_tick_count();

        if (slot >= (int)cache->current_size) {
            cache->current_size = slot + 1;
        }
    }

    cache->stats.current_size = cache->current_size;

    xSemaphoreGive(cache->mutex);
    return stmt;
}

int sqlite_cache_release_stmt(sqlite_stmt_cache_t *cache, sqlite3_stmt *stmt) {
    if (!cache || !stmt) {
        return SQLITE_ERROR;
    }

    xSemaphoreTake(cache->mutex, portMAX_DELAY);

    bool found = false;
    for (uint32_t i = 0; i < cache->current_size; i++) {
        if (cache->entries[i].stmt == stmt) {
            cache->entries[i].in_use = false;
            sqlite3_reset(stmt);
            sqlite3_clear_bindings(stmt);
            found = true;
            break;
        }
    }

    xSemaphoreGive(cache->mutex);

    return found ? SQLITE_OK : SQLITE_ERROR;
}

void sqlite_cache_clear(sqlite_stmt_cache_t *cache) {
    if (!cache) {
        return;
    }

    xSemaphoreTake(cache->mutex, portMAX_DELAY);

    for (uint32_t i = 0; i < cache->current_size; i++) {
        if (!cache->entries[i].in_use) {
            if (cache->entries[i].stmt) {
                sqlite3_finalize(cache->entries[i].stmt);
            }
            if (cache->entries[i].sql) {
                free(cache->entries[i].sql);
            }
            memset(&cache->entries[i], 0, sizeof(cache_entry_t));
        }
    }

    cache->current_size = 0;
    cache->stats.current_size = 0;

    xSemaphoreGive(cache->mutex);

    ESP_LOGI(LOG_TAG, "Cache cleared");
}

int sqlite_cache_get_stats(sqlite_stmt_cache_t *cache, sqlite_cache_stats_t *stats) {
    if (!cache || !stats) {
        return SQLITE_ERROR;
    }

    xSemaphoreTake(cache->mutex, portMAX_DELAY);
    memcpy(stats, &cache->stats, sizeof(sqlite_cache_stats_t));
    xSemaphoreGive(cache->mutex);

    return SQLITE_OK;
}

int sqlite_cache_evict_lru(sqlite_stmt_cache_t *cache) {
    if (!cache) {
        return SQLITE_ERROR;
    }

    xSemaphoreTake(cache->mutex, portMAX_DELAY);

    uint32_t oldest_time = UINT32_MAX;
    int oldest_idx = -1;

    for (uint32_t i = 0; i < cache->current_size; i++) {
        if (!cache->entries[i].in_use &&
            cache->entries[i].last_access_time < oldest_time) {
            oldest_time = cache->entries[i].last_access_time;
            oldest_idx = i;
        }
    }

    if (oldest_idx >= 0) {
        if (cache->entries[oldest_idx].stmt) {
            sqlite3_finalize(cache->entries[oldest_idx].stmt);
        }
        if (cache->entries[oldest_idx].sql) {
            free(cache->entries[oldest_idx].sql);
        }
        memset(&cache->entries[oldest_idx], 0, sizeof(cache_entry_t));
        cache->stats.evictions++;
    }

    xSemaphoreGive(cache->mutex);

    return (oldest_idx >= 0) ? SQLITE_OK : SQLITE_ERROR;
}

#endif /* SQLITE_HAS_FREERTOS */
