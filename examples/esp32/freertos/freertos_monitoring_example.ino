/*
 * SQLite ESP32 FreeRTOS Monitoring & Backup Example
 *
 * This example demonstrates:
 * - Real-time monitoring and health checks
 * - Watchdog integration
 * - Automatic database backups
 * - Slow query detection
 * - Error tracking and alerts
 */

#include <Arduino.h>
#include "sqlite_micro/sqlite_micro.h"
#include "sqlite_micro/sqlite_async.h"
#include "sqlite_micro/sqlite_pool.h"
#include "sqlite_micro/sqlite_monitor.h"
#include "sqlite_micro/sqlite_backup.h"

sqlite_pool_t *pool = NULL;
sqlite3 *main_db = NULL;

/* Slow query callback */
void on_slow_query(const char *sql, uint32_t execution_time_ms) {
    Serial.printf("⚠️  SLOW QUERY (%d ms): %s\n", execution_time_ms, sql);
}

/* Error callback */
void on_error(int error_code, const char *error_msg) {
    Serial.printf("❌ DATABASE ERROR [%d]: %s\n", error_code, error_msg);
}

/* Backup progress callback */
void on_backup_progress(sqlite_backup_progress_t *progress, void *user_data) {
    Serial.printf("📦 Backup progress: %d%% (%d/%d pages)\n",
                 progress->percent_complete,
                 progress->pages_copied,
                 progress->total_pages);
}

/* Monitoring task */
void monitoring_task(void *param) {
    TickType_t last_wake_time = xTaskGetTickCount();

    while (true) {
        /* Perform health check */
        sqlite_health_report_t health;
        if (sqlite_monitor_health_check(&health) == SQLITE_OK) {
            const char *status_str[] = {"GOOD", "WARNING", "CRITICAL", "UNKNOWN"};

            Serial.println("\n=== Health Check ===");
            Serial.printf("Status: %s\n", status_str[health.status]);
            Serial.printf("Errors (last hour): %d\n", health.error_count_last_hour);
            Serial.printf("Slow queries (last hour): %d\n", health.slow_queries_last_hour);
            Serial.printf("CPU usage: %.1f%%\n", health.avg_cpu_usage_percent);
            Serial.printf("Memory usage: %.1f%%\n", health.memory_usage_percent);
            Serial.printf("Message: %s\n", health.status_message);

            /* Alert if critical */
            if (health.status == SQLITE_HEALTH_CRITICAL) {
                Serial.println("🚨 CRITICAL HEALTH STATUS - Taking action!");

                /* Trigger emergency backup */
                if (main_db) {
                    sqlite_backup_config_t backup_cfg = {
                        .backup_path = "/littlefs/emergency_backup.db",
                        .type = SQLITE_BACKUP_FULL,
                        .page_size = 512,
                        .compress = false,
                        .encrypt = false,
                        .max_backups = 5
                    };

                    sqlite_backup_database(main_db, backup_cfg.backup_path, &backup_cfg);
                    Serial.println("Emergency backup completed!");
                }
            }
        }

        /* Get performance metrics */
        sqlite_perf_metrics_t metrics;
        if (sqlite_monitor_get_metrics(&metrics) == SQLITE_OK) {
            Serial.println("\n=== Performance Metrics ===");
            Serial.printf("Total queries: %d\n", metrics.query_count);
            Serial.printf("Slow queries: %d\n", metrics.slow_query_count);
            Serial.printf("Error count: %d\n", metrics.error_count);
            Serial.printf("Avg execution: %d ms\n", metrics.avg_execution_time_ms);
            Serial.printf("Max execution: %d ms\n", metrics.max_execution_time_ms);
            Serial.printf("Cache hit rate: %d%%\n", metrics.cache_hit_rate_percent);
            Serial.printf("Active connections: %d\n", metrics.connections_active);
            Serial.printf("Memory used: %d bytes\n", metrics.memory_used_bytes);
        }

        /* Feed watchdog */
        sqlite_watchdog_feed();

        /* Wait 30 seconds */
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(30000));
    }
}

/* Data generation task - creates workload */
void workload_task(void *param) {
    TickType_t last_wake_time = xTaskGetTickCount();
    uint32_t batch = 0;

    while (true) {
        sqlite3 *db = sqlite_pool_acquire(pool, 5000);
        if (!db) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        /* Profile query execution */
        const char *sql = "INSERT INTO data_log (batch_id, value, timestamp) VALUES (?, ?, ?)";

        sqlite_monitor_profile_start(sql);
        uint32_t start = millis();

        /* Begin transaction */
        sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, NULL);

        /* Insert batch of records */
        sqlite3_stmt *stmt;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK) {
            for (int i = 0; i < 100; i++) {
                sqlite3_bind_int(stmt, 1, batch);
                sqlite3_bind_double(stmt, 2, random(0, 1000) / 10.0);
                sqlite3_bind_int(stmt, 3, millis());

                if (sqlite3_step(stmt) != SQLITE_DONE) {
                    Serial.printf("Insert failed: %s\n", sqlite3_errmsg(db));
                }

                sqlite3_reset(stmt);
            }
            sqlite3_finalize(stmt);
        }

        /* Commit */
        sqlite3_exec(db, "COMMIT", NULL, NULL, NULL);

        uint32_t execution_time = millis() - start;
        sqlite_monitor_profile_end(sql, execution_time, 100);

        Serial.printf("Batch %d: Inserted 100 records in %d ms\n", batch, execution_time);

        sqlite_pool_release(pool, db);
        batch++;

        /* Simulate slow query occasionally */
        if (batch % 10 == 0) {
            db = sqlite_pool_acquire(pool, 5000);
            if (db) {
                const char *slow_query =
                    "SELECT COUNT(*), AVG(value), MAX(value), MIN(value) "
                    "FROM data_log WHERE batch_id < 100";

                sqlite_monitor_profile_start(slow_query);
                start = millis();

                sqlite3_exec(db, slow_query, NULL, NULL, NULL);

                execution_time = millis() - start;
                sqlite_monitor_profile_end(slow_query, execution_time, 1);

                sqlite_pool_release(pool, db);
            }
        }

        /* Wait 2 seconds */
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(2000));
    }
}

/* Backup task - periodic backups */
void backup_task(void *param) {
    TickType_t last_wake_time = xTaskGetTickCount();
    uint32_t backup_count = 0;

    while (true) {
        /* Wait 5 minutes for first backup, then every 10 minutes */
        uint32_t delay_ms = (backup_count == 0) ? 300000 : 600000;
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(delay_ms));

        Serial.println("\n📦 Starting automatic backup...");

        if (main_db) {
            char backup_path[128];
            snprintf(backup_path, sizeof(backup_path),
                    "/littlefs/backup_%lu.db", millis());

            sqlite_backup_config_t backup_cfg = {
                .backup_path = backup_path,
                .type = SQLITE_BACKUP_FULL,
                .page_size = 512,
                .compress = false,
                .encrypt = false,
                .max_backups = 3
            };

            uint32_t start = millis();
            int rc = sqlite_backup_database(main_db, backup_path, &backup_cfg);
            uint32_t duration = millis() - start;

            if (rc == SQLITE_OK) {
                Serial.printf("✓ Backup completed in %d ms: %s\n", duration, backup_path);

                /* Verify backup */
                if (sqlite_backup_verify(backup_path) == SQLITE_OK) {
                    Serial.println("✓ Backup verified successfully");
                } else {
                    Serial.println("⚠️  Backup verification failed!");
                }
            } else {
                Serial.printf("✗ Backup failed: %d\n", rc);
            }

            backup_count++;

            /* Cleanup old backups (keep last 3) */
            if (backup_count > 3) {
                sqlite_backup_cleanup("/littlefs", 24);  /* Keep last 24 hours */
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("\n=== SQLite Monitoring & Backup Example ===");

    /* Initialize systems */
    sqlite_micro_init();
    sqlite_micro_init_littlefs_esp32("/littlefs", 5);
    sqlite_micro_register_vfs(SQLITE_STORAGE_LITTLEFS, "littlefs");

    /* Initialize monitoring */
    sqlite_monitor_init(SQLITE_MONITOR_LEVEL_INFO);
    sqlite_monitor_set_slow_query_threshold(500);  /* 500ms threshold */
    sqlite_monitor_set_slow_query_callback(on_slow_query);
    sqlite_monitor_set_error_callback(on_error);

    Serial.println("Monitoring system initialized");

    /* Initialize watchdog */
    sqlite_watchdog_config_t watchdog_cfg = {
        .timeout_ms = 60000,  /* 60 second timeout */
        .auto_reset = true,
        .panic_on_timeout = false
    };
    sqlite_watchdog_init(&watchdog_cfg);

    Serial.println("Watchdog initialized");

    /* Initialize backup system */
    sqlite_backup_init();

    Serial.println("Backup system initialized");

    /* Create connection pool */
    sqlite_vfs_config_t vfs_cfg = {
        .mount_point = "/littlefs",
        .db_path = "/littlefs/monitored.db",
        .storage_type = SQLITE_STORAGE_LITTLEFS,
        .cache_size = 128
    };

    sqlite_pool_config_t pool_cfg = {
        .db_path = "/littlefs/monitored.db",
        .vfs_config = &vfs_cfg,
        .min_connections = 2,
        .max_connections = 4,
        .connection_timeout_ms = 5000,
        .idle_timeout_ms = 120000,
        .enable_wal = true,
        .enable_shared_cache = false
    };

    pool = sqlite_pool_create(&pool_cfg);
    if (!pool) {
        Serial.println("Failed to create pool!");
        return;
    }

    /* Get main connection for backup operations */
    main_db = sqlite_pool_acquire(pool, 5000);
    if (main_db) {
        /* Create tables */
        const char *create_tables =
            "CREATE TABLE IF NOT EXISTS data_log ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "batch_id INTEGER,"
            "value REAL,"
            "timestamp INTEGER);"

            "CREATE INDEX IF NOT EXISTS idx_batch ON data_log(batch_id);"
            "CREATE INDEX IF NOT EXISTS idx_timestamp ON data_log(timestamp);";

        char *err_msg = NULL;
        int rc = sqlite3_exec(main_db, create_tables, NULL, NULL, &err_msg);
        if (rc != SQLITE_OK) {
            Serial.printf("Failed to create tables: %s\n", err_msg);
            sqlite3_free(err_msg);
        } else {
            Serial.println("Database tables ready!");
        }
    }

    /* Initialize async system */
    sqlite_async_config_t async_cfg = SQLITE_ASYNC_DEFAULT_CONFIG();
    sqlite_async_init(&async_cfg);

    /* Create tasks */
    xTaskCreatePinnedToCore(monitoring_task, "Monitor", 8192, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(workload_task, "Workload", 8192, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(backup_task, "Backup", 8192, NULL, 3, NULL, 0);

    Serial.println("\n✓ System ready - Monitoring started!\n");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(5000));

    /* Display slow queries periodically */
    static uint32_t last_display = 0;
    if (millis() - last_display > 60000) {  /* Every minute */
        sqlite_query_profile_t profiles[10];
        int count = sqlite_monitor_get_slow_queries(profiles, 10);

        if (count > 0) {
            Serial.println("\n=== Top Slow Queries ===");
            for (int i = 0; i < count; i++) {
                Serial.printf("%d. %s (%d ms, %d rows)\n",
                             i + 1,
                             profiles[i].sql,
                             profiles[i].execution_time_ms,
                             profiles[i].rows_affected);
            }
        }

        last_display = millis();
    }
}
