/*
 * SQLite on ESP32 with LittleFS Example
 *
 * This example demonstrates how to use SQLite on ESP32 with LittleFS storage.
 * LittleFS is a more robust filesystem compared to SPIFFS.
 */

#include <Arduino.h>
#include "sqlite_micro/sqlite_micro.h"
#include "sqlite_micro/sqlite_config.h"

sqlite3 *db;
const char *db_path = "/littlefs/sensors.db";

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=== SQLite ESP32 LittleFS Example ===");

    // Initialize SQLite
    int rc = sqlite_micro_init();
    if (rc != SQLITE_OK) {
        Serial.printf("Failed to initialize SQLite: %d\n", rc);
        return;
    }

    // Initialize LittleFS
    rc = sqlite_micro_init_littlefs_esp32("/littlefs", 5);
    if (rc != SQLITE_OK) {
        Serial.println("Failed to initialize LittleFS");
        return;
    }

    // Register LittleFS VFS
    rc = sqlite_micro_register_vfs(SQLITE_STORAGE_LITTLEFS, "littlefs");
    if (rc != SQLITE_OK) {
        Serial.println("Failed to register LittleFS VFS");
        return;
    }

    // Open database
    sqlite_vfs_config_t config = {
        .mount_point = "/littlefs",
        .db_path = db_path,
        .storage_type = SQLITE_STORAGE_LITTLEFS,
        .cache_size = 64  // 64KB cache
    };

    rc = sqlite_micro_open(db_path, &db, &config);
    if (rc != SQLITE_OK) {
        Serial.printf("Failed to open database: %d\n", rc);
        return;
    }

    Serial.println("Database opened successfully!");

    // Create a more complex table
    const char *sql_create = "CREATE TABLE IF NOT EXISTS measurements ("
                            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                            "sensor_id INTEGER NOT NULL,"
                            "reading REAL NOT NULL,"
                            "unit TEXT,"
                            "quality INTEGER,"
                            "timestamp INTEGER);";

    char *err_msg = NULL;
    rc = sqlite3_exec(db, sql_create, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        Serial.printf("SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return;
    }

    Serial.println("Table created successfully!");

    // Insert sample data with transaction for better performance
    rc = sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, &err_msg);

    for (int i = 0; i < 10; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql),
                "INSERT INTO measurements (sensor_id, reading, unit, quality, timestamp) "
                "VALUES (%d, %.2f, '%s', %d, %d);",
                i % 3 + 1, 20.0 + i * 0.5, "°C", 90 + i, 1234567890 + i);

        rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
        if (rc != SQLITE_OK) {
            Serial.printf("Insert error: %s\n", err_msg);
            sqlite3_free(err_msg);
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            return;
        }
    }

    rc = sqlite3_exec(db, "COMMIT;", NULL, NULL, &err_msg);
    Serial.println("10 measurements inserted successfully!");

    // Query with aggregation
    const char *sql_query = "SELECT sensor_id, AVG(reading), COUNT(*) "
                           "FROM measurements GROUP BY sensor_id;";
    sqlite3_stmt *stmt;

    rc = sqlite3_prepare_v2(db, sql_query, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        Serial.printf("Prepare error: %s\n", sqlite3_errmsg(db));
        return;
    }

    Serial.println("\n--- Sensor Averages ---");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int sensor_id = sqlite3_column_int(stmt, 0);
        double avg_reading = sqlite3_column_double(stmt, 1);
        int count = sqlite3_column_int(stmt, 2);

        Serial.printf("Sensor %d: Avg=%.2f°C, Count=%d\n",
                     sensor_id, avg_reading, count);
    }

    sqlite3_finalize(stmt);

    // Memory statistics
    sqlite_micro_stats_t stats;
    sqlite_micro_get_stats(&stats);
    Serial.printf("\n--- Memory Stats ---\n");
    Serial.printf("Current: %d bytes\n", stats.current_memory);
    Serial.printf("Peak: %d bytes\n", stats.peak_memory);

    Serial.println("\n=== Example Complete ===");
}

void loop() {
    delay(1000);
}
