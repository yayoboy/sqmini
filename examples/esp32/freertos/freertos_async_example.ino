/*
 * SQLite ESP32 FreeRTOS Async Example
 *
 * This example demonstrates:
 * - Non-blocking async API with FreeRTOS
 * - Connection pooling
 * - Prepared statement caching
 * - Multiple concurrent tasks
 * - WAL mode for performance
 */

#include <Arduino.h>
#include "sqlite_micro/sqlite_micro.h"
#include "sqlite_micro/sqlite_async.h"
#include "sqlite_micro/sqlite_pool.h"
#include "sqlite_micro/sqlite_cache.h"

/* Global handles */
sqlite_pool_t *pool = NULL;
sqlite_stmt_cache_t *cache = NULL;

/* Task handles */
TaskHandle_t sensor_task_handle = NULL;
TaskHandle_t logger_task_handle = NULL;
TaskHandle_t stats_task_handle = NULL;

/* Callback for async operations */
void async_callback(sqlite_async_result_t *result, void *user_data) {
    const char *operation = (const char*)user_data;

    if (result->status == SQLITE_ASYNC_COMPLETED) {
        Serial.printf("[%s] ✓ Completed in %d ms, rows: %d\n",
                     operation,
                     result->execution_time_ms,
                     result->rows_affected);
    } else {
        Serial.printf("[%s] ✗ Failed: %s\n",
                     operation,
                     result->error_msg ? result->error_msg : "Unknown error");
    }
}

/* Row callback for queries */
void row_callback(sqlite3_stmt *stmt, void *user_data) {
    int id = sqlite3_column_int(stmt, 0);
    const char *name = (const char*)sqlite3_column_text(stmt, 1);
    double value = sqlite3_column_double(stmt, 2);

    Serial.printf("  → ID=%d, Sensor=%s, Value=%.2f\n", id, name, value);
}

/* Sensor reading task - simulates IoT sensor data */
void sensor_task(void *param) {
    TickType_t last_wake_time = xTaskGetTickCount();
    uint32_t reading_count = 0;

    while (true) {
        /* Acquire connection from pool */
        sqlite3 *db = sqlite_pool_acquire(pool, 5000);
        if (!db) {
            Serial.println("Failed to acquire connection!");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        /* Simulate sensor readings */
        float temperature = 20.0 + (reading_count % 10) * 0.5;
        float humidity = 50.0 + (reading_count % 20);

        char sql[256];
        snprintf(sql, sizeof(sql),
                "INSERT INTO sensors (name, value, timestamp) VALUES "
                "('Temperature', %.2f, %lu), "
                "('Humidity', %.2f, %lu)",
                temperature, millis(),
                humidity, millis());

        /* Async insert */
        sqlite_async_exec(db, sql, async_callback, (void*)"SensorTask", SQLITE_PRIORITY_NORMAL);

        /* Release connection back to pool */
        sqlite_pool_release(pool, db);

        reading_count++;

        /* Wait 5 seconds */
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(5000));
    }
}

/* Logger task - queries recent data */
void logger_task(void *param) {
    TickType_t last_wake_time = xTaskGetTickCount();

    while (true) {
        /* Acquire connection */
        sqlite3 *db = sqlite_pool_acquire(pool, 5000);
        if (!db) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        /* Query recent readings */
        const char *query = "SELECT * FROM sensors ORDER BY id DESC LIMIT 5";

        Serial.println("\n--- Recent Sensor Readings ---");
        sqlite_async_query(db, query, row_callback, async_callback,
                          (void*)"LoggerTask", SQLITE_PRIORITY_NORMAL);

        sqlite_pool_release(pool, db);

        /* Wait 10 seconds */
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(10000));
    }
}

/* Statistics task - monitors system health */
void stats_task(void *param) {
    TickType_t last_wake_time = xTaskGetTickCount();

    while (true) {
        /* Get async statistics */
        sqlite_async_stats_t async_stats;
        sqlite_async_get_stats(&async_stats);

        /* Get pool statistics */
        sqlite_pool_stats_t pool_stats;
        sqlite_pool_get_stats(pool, &pool_stats);

        /* Get cache statistics */
        sqlite_cache_stats_t cache_stats;
        sqlite_cache_get_stats(cache, &cache_stats);

        /* Display statistics */
        Serial.println("\n=== System Statistics ===");
        Serial.printf("Async Queue: %d pending, %d completed, %d failed\n",
                     async_stats.pending_requests,
                     async_stats.completed_requests,
                     async_stats.failed_requests);

        Serial.printf("Pool: %d active, %d idle, %d peak\n",
                     pool_stats.active_connections,
                     pool_stats.idle_connections,
                     pool_stats.peak_connections);

        Serial.printf("Cache: %.1f%% hit rate, %d entries, %d evictions\n",
                     cache_stats.hit_rate * 100,
                     cache_stats.current_size,
                     cache_stats.evictions);

        Serial.printf("Memory: %d bytes used, %d bytes peak\n",
                     sqlite3_memory_used(),
                     sqlite3_memory_highwater(0));

        Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());

        /* Wait 15 seconds */
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(15000));
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("\n=== SQLite FreeRTOS Async Example ===");

    /* Initialize SQLite */
    if (sqlite_micro_init() != SQLITE_OK) {
        Serial.println("Failed to initialize SQLite!");
        return;
    }

    /* Initialize LittleFS */
    if (sqlite_micro_init_littlefs_esp32("/littlefs", 5) != SQLITE_OK) {
        Serial.println("Failed to initialize LittleFS!");
        return;
    }

    /* Register VFS */
    sqlite_micro_register_vfs(SQLITE_STORAGE_LITTLEFS, "littlefs");

    /* Configure connection pool with WAL mode */
    sqlite_vfs_config_t vfs_cfg = {
        .mount_point = "/littlefs",
        .db_path = "/littlefs/iot.db",
        .storage_type = SQLITE_STORAGE_LITTLEFS,
        .cache_size = 64
    };

    sqlite_pool_config_t pool_cfg = {
        .db_path = "/littlefs/iot.db",
        .vfs_config = &vfs_cfg,
        .min_connections = 2,
        .max_connections = 4,
        .connection_timeout_ms = 5000,
        .idle_timeout_ms = 60000,
        .enable_wal = true,
        .enable_shared_cache = false
    };

    /* Create connection pool */
    pool = sqlite_pool_create(&pool_cfg);
    if (!pool) {
        Serial.println("Failed to create connection pool!");
        return;
    }

    Serial.println("Connection pool created!");

    /* Initialize async system */
    sqlite_async_config_t async_cfg = SQLITE_ASYNC_DEFAULT_CONFIG();
    async_cfg.queue_size = 64;
    async_cfg.max_concurrent_ops = 8;

    if (sqlite_async_init(&async_cfg) != SQLITE_OK) {
        Serial.println("Failed to initialize async system!");
        return;
    }

    Serial.println("Async system initialized!");

    /* Create prepared statement cache */
    sqlite3 *db = sqlite_pool_acquire(pool, 5000);
    if (db) {
        sqlite_cache_config_t cache_cfg = SQLITE_CACHE_DEFAULT_CONFIG();
        cache = sqlite_cache_create(db, &cache_cfg);

        /* Create table */
        const char *create_table =
            "CREATE TABLE IF NOT EXISTS sensors ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT NOT NULL,"
            "value REAL NOT NULL,"
            "timestamp INTEGER);";

        char *err_msg = NULL;
        int rc = sqlite3_exec(db, create_table, NULL, NULL, &err_msg);
        if (rc != SQLITE_OK) {
            Serial.printf("Failed to create table: %s\n", err_msg);
            sqlite3_free(err_msg);
        } else {
            Serial.println("Database table ready!");
        }

        sqlite_pool_release(pool, db);
    }

    /* Create FreeRTOS tasks */
    xTaskCreatePinnedToCore(
        sensor_task,
        "SensorTask",
        4096,
        NULL,
        5,
        &sensor_task_handle,
        0  /* Core 0 */
    );

    xTaskCreatePinnedToCore(
        logger_task,
        "LoggerTask",
        4096,
        NULL,
        4,
        &logger_task_handle,
        1  /* Core 1 */
    );

    xTaskCreatePinnedToCore(
        stats_task,
        "StatsTask",
        4096,
        NULL,
        3,
        &stats_task_handle,
        1  /* Core 1 */
    );

    Serial.println("\nAll tasks started! System running...\n");
}

void loop() {
    /* Main loop does nothing - all work in tasks */
    vTaskDelay(pdMS_TO_TICKS(1000));

    /* Optional: Shrink pool periodically to free idle connections */
    static uint32_t last_shrink = 0;
    if (millis() - last_shrink > 300000) {  /* Every 5 minutes */
        sqlite_pool_shrink(pool);
        last_shrink = millis();
    }
}
