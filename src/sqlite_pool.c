#include "sqlite_micro/sqlite_pool.h"
#include <string.h>
#include <stdlib.h>

#ifdef SQLITE_HAS_FREERTOS
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <esp_log.h>

#define LOG_TAG "SQLitePool"

/* Connection wrapper */
typedef struct {
    sqlite3 *db;
    bool in_use;
    uint32_t last_used_time;
    uint32_t acquire_count;
} pool_connection_t;

/* Pool structure */
struct sqlite_pool_s {
    pool_connection_t *connections;
    uint32_t num_connections;
    sqlite_pool_config_t config;
    SemaphoreHandle_t mutex;
    SemaphoreHandle_t available_sem;
    sqlite_pool_stats_t stats;
};

static uint32_t get_tick_ms(void) {
    return xTaskGetTickCount() * portTICK_PERIOD_MS;
}

sqlite_pool_t* sqlite_pool_create(sqlite_pool_config_t *config) {
    if (!config || !config->db_path) {
        ESP_LOGE(LOG_TAG, "Invalid configuration");
        return NULL;
    }

    sqlite_pool_t *pool = (sqlite_pool_t*)calloc(1, sizeof(sqlite_pool_t));
    if (!pool) {
        ESP_LOGE(LOG_TAG, "Failed to allocate pool");
        return NULL;
    }

    memcpy(&pool->config, config, sizeof(sqlite_pool_config_t));

    /* Create mutex */
    pool->mutex = xSemaphoreCreateMutex();
    if (!pool->mutex) {
        ESP_LOGE(LOG_TAG, "Failed to create mutex");
        free(pool);
        return NULL;
    }

    /* Create semaphore for available connections */
    pool->available_sem = xSemaphoreCreateCounting(
        config->max_connections,
        config->min_connections
    );
    if (!pool->available_sem) {
        ESP_LOGE(LOG_TAG, "Failed to create semaphore");
        vSemaphoreDelete(pool->mutex);
        free(pool);
        return NULL;
    }

    /* Allocate connection array */
    pool->connections = (pool_connection_t*)calloc(
        config->max_connections,
        sizeof(pool_connection_t)
    );
    if (!pool->connections) {
        ESP_LOGE(LOG_TAG, "Failed to allocate connections");
        vSemaphoreDelete(pool->mutex);
        vSemaphoreDelete(pool->available_sem);
        free(pool);
        return NULL;
    }

    /* Initialize minimum connections */
    for (uint32_t i = 0; i < config->min_connections; i++) {
        sqlite3 *db = NULL;
        int rc;

        if (config->vfs_config) {
            rc = sqlite_micro_open(config->db_path, &db, config->vfs_config);
        } else {
            rc = sqlite3_open(config->db_path, &db);
        }

        if (rc != SQLITE_OK) {
            ESP_LOGE(LOG_TAG, "Failed to open connection %d: %d", i, rc);
            continue;
        }

        /* Enable WAL mode if requested */
        if (config->enable_wal) {
            sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
            sqlite3_exec(db, "PRAGMA synchronous=NORMAL", NULL, NULL, NULL);
        }

        pool->connections[i].db = db;
        pool->connections[i].in_use = false;
        pool->connections[i].last_used_time = get_tick_ms();
        pool->num_connections++;
    }

    pool->stats.total_connections = pool->num_connections;
    pool->stats.idle_connections = pool->num_connections;

    ESP_LOGI(LOG_TAG, "Pool created with %d connections", pool->num_connections);
    return pool;
}

void sqlite_pool_destroy(sqlite_pool_t *pool) {
    if (!pool) {
        return;
    }

    /* Close all connections */
    for (uint32_t i = 0; i < pool->config.max_connections; i++) {
        if (pool->connections[i].db) {
            sqlite3_close(pool->connections[i].db);
        }
    }

    free(pool->connections);
    vSemaphoreDelete(pool->mutex);
    vSemaphoreDelete(pool->available_sem);
    free(pool);

    ESP_LOGI(LOG_TAG, "Pool destroyed");
}

sqlite3* sqlite_pool_acquire(sqlite_pool_t *pool, uint32_t timeout_ms) {
    if (!pool) {
        return NULL;
    }

    /* Wait for available connection */
    if (xSemaphoreTake(pool->available_sem, pdMS_TO_TICKS(timeout_ms)) != pdPASS) {
        xSemaphoreTake(pool->mutex, portMAX_DELAY);
        pool->stats.acquire_timeouts++;
        xSemaphoreGive(pool->mutex);
        ESP_LOGW(LOG_TAG, "Acquire timeout");
        return NULL;
    }

    xSemaphoreTake(pool->mutex, portMAX_DELAY);

    sqlite3 *db = NULL;

    /* Find available connection */
    for (uint32_t i = 0; i < pool->config.max_connections; i++) {
        if (pool->connections[i].db && !pool->connections[i].in_use) {
            pool->connections[i].in_use = true;
            pool->connections[i].last_used_time = get_tick_ms();
            pool->connections[i].acquire_count++;
            db = pool->connections[i].db;

            pool->stats.active_connections++;
            pool->stats.idle_connections--;
            pool->stats.total_acquires++;

            if (pool->stats.active_connections > pool->stats.peak_connections) {
                pool->stats.peak_connections = pool->stats.active_connections;
            }

            break;
        }
    }

    /* If no existing connection, create new one if under max */
    if (!db && pool->num_connections < pool->config.max_connections) {
        for (uint32_t i = 0; i < pool->config.max_connections; i++) {
            if (!pool->connections[i].db) {
                int rc;
                if (pool->config.vfs_config) {
                    rc = sqlite_micro_open(pool->config.db_path, &db, pool->config.vfs_config);
                } else {
                    rc = sqlite3_open(pool->config.db_path, &db);
                }

                if (rc == SQLITE_OK) {
                    if (pool->config.enable_wal) {
                        sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
                        sqlite3_exec(db, "PRAGMA synchronous=NORMAL", NULL, NULL, NULL);
                    }

                    pool->connections[i].db = db;
                    pool->connections[i].in_use = true;
                    pool->connections[i].last_used_time = get_tick_ms();
                    pool->connections[i].acquire_count = 1;

                    pool->num_connections++;
                    pool->stats.total_connections++;
                    pool->stats.active_connections++;
                    pool->stats.total_acquires++;

                    ESP_LOGI(LOG_TAG, "Created new connection, total: %d", pool->num_connections);
                }
                break;
            }
        }
    }

    xSemaphoreGive(pool->mutex);
    return db;
}

int sqlite_pool_release(sqlite_pool_t *pool, sqlite3 *db) {
    if (!pool || !db) {
        return SQLITE_ERROR;
    }

    xSemaphoreTake(pool->mutex, portMAX_DELAY);

    bool found = false;
    for (uint32_t i = 0; i < pool->config.max_connections; i++) {
        if (pool->connections[i].db == db && pool->connections[i].in_use) {
            pool->connections[i].in_use = false;
            pool->connections[i].last_used_time = get_tick_ms();

            pool->stats.active_connections--;
            pool->stats.idle_connections++;
            pool->stats.total_releases++;

            /* Reset connection state */
            sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);

            found = true;
            break;
        }
    }

    xSemaphoreGive(pool->mutex);

    if (found) {
        xSemaphoreGive(pool->available_sem);
        return SQLITE_OK;
    }

    return SQLITE_ERROR;
}

int sqlite_pool_get_stats(sqlite_pool_t *pool, sqlite_pool_stats_t *stats) {
    if (!pool || !stats) {
        return SQLITE_ERROR;
    }

    xSemaphoreTake(pool->mutex, portMAX_DELAY);
    memcpy(stats, &pool->stats, sizeof(sqlite_pool_stats_t));
    xSemaphoreGive(pool->mutex);

    return SQLITE_OK;
}

int sqlite_pool_shrink(sqlite_pool_t *pool) {
    if (!pool) {
        return SQLITE_ERROR;
    }

    xSemaphoreTake(pool->mutex, portMAX_DELAY);

    uint32_t current_time = get_tick_ms();
    uint32_t removed = 0;

    for (uint32_t i = 0; i < pool->config.max_connections; i++) {
        if (pool->connections[i].db &&
            !pool->connections[i].in_use &&
            pool->num_connections > pool->config.min_connections) {

            uint32_t idle_time = current_time - pool->connections[i].last_used_time;
            if (idle_time > pool->config.idle_timeout_ms) {
                sqlite3_close(pool->connections[i].db);
                memset(&pool->connections[i], 0, sizeof(pool_connection_t));
                pool->num_connections--;
                pool->stats.idle_connections--;
                removed++;
            }
        }
    }

    xSemaphoreGive(pool->mutex);

    if (removed > 0) {
        ESP_LOGI(LOG_TAG, "Shrunk pool by %d connections", removed);
    }

    return SQLITE_OK;
}

int sqlite_pool_validate(sqlite_pool_t *pool) {
    if (!pool) {
        return SQLITE_ERROR;
    }

    int valid = 0;
    int invalid = 0;

    xSemaphoreTake(pool->mutex, portMAX_DELAY);

    for (uint32_t i = 0; i < pool->config.max_connections; i++) {
        if (pool->connections[i].db) {
            /* Try a simple query to validate connection */
            sqlite3_stmt *stmt;
            int rc = sqlite3_prepare_v2(pool->connections[i].db, "SELECT 1", -1, &stmt, NULL);
            if (rc == SQLITE_OK) {
                sqlite3_finalize(stmt);
                valid++;
            } else {
                invalid++;
                ESP_LOGW(LOG_TAG, "Invalid connection detected at index %d", i);
            }
        }
    }

    xSemaphoreGive(pool->mutex);

    ESP_LOGI(LOG_TAG, "Pool validation: %d valid, %d invalid", valid, invalid);
    return (invalid == 0) ? SQLITE_OK : SQLITE_ERROR;
}

#endif /* SQLITE_HAS_FREERTOS */
