/*
 * SQLite on ESP32 with SPIFFS Example
 *
 * This example demonstrates how to use SQLite on ESP32 with SPIFFS storage.
 * SPIFFS is the native flash filesystem for ESP32.
 */

#include <Arduino.h>
#include "sqlite_micro/sqlite_micro.h"
#include "sqlite_micro/sqlite_config.h"

sqlite3 *db;
const char *db_path = "/spiffs/test.db";

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=== SQLite ESP32 SPIFFS Example ===");

    // Initialize SQLite
    int rc = sqlite_micro_init();
    if (rc != SQLITE_OK) {
        Serial.printf("Failed to initialize SQLite: %d\n", rc);
        return;
    }

    // Initialize SPIFFS
    rc = sqlite_micro_init_spiffs("/spiffs", 5);
    if (rc != SQLITE_OK) {
        Serial.println("Failed to initialize SPIFFS");
        return;
    }

    // Register SPIFFS VFS
    rc = sqlite_micro_register_vfs(SQLITE_STORAGE_SPIFFS, "spiffs");
    if (rc != SQLITE_OK) {
        Serial.println("Failed to register SPIFFS VFS");
        return;
    }

    // Open database
    sqlite_vfs_config_t config = {
        .mount_point = "/spiffs",
        .db_path = db_path,
        .storage_type = SQLITE_STORAGE_SPIFFS,
        .cache_size = 64  // 64KB cache
    };

    rc = sqlite_micro_open(db_path, &db, &config);
    if (rc != SQLITE_OK) {
        Serial.printf("Failed to open database: %d\n", rc);
        return;
    }

    Serial.println("Database opened successfully!");

    // Create a table
    const char *sql_create = "CREATE TABLE IF NOT EXISTS sensors ("
                            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                            "name TEXT NOT NULL,"
                            "value REAL,"
                            "timestamp INTEGER);";

    char *err_msg = NULL;
    rc = sqlite3_exec(db, sql_create, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        Serial.printf("SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return;
    }

    Serial.println("Table created successfully!");

    // Insert some data
    const char *sql_insert = "INSERT INTO sensors (name, value, timestamp) VALUES "
                            "('Temperature', 23.5, 1234567890),"
                            "('Humidity', 65.2, 1234567891),"
                            "('Pressure', 1013.25, 1234567892);";

    rc = sqlite3_exec(db, sql_insert, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        Serial.printf("Insert error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return;
    }

    Serial.println("Data inserted successfully!");

    // Query data
    const char *sql_query = "SELECT * FROM sensors;";
    sqlite3_stmt *stmt;

    rc = sqlite3_prepare_v2(db, sql_query, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        Serial.printf("Prepare error: %s\n", sqlite3_errmsg(db));
        return;
    }

    Serial.println("\n--- Sensor Data ---");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char *name = (const char*)sqlite3_column_text(stmt, 1);
        double value = sqlite3_column_double(stmt, 2);
        int timestamp = sqlite3_column_int(stmt, 3);

        Serial.printf("ID: %d, Name: %s, Value: %.2f, Time: %d\n",
                     id, name, value, timestamp);
    }

    sqlite3_finalize(stmt);

    // Display memory statistics
    sqlite_micro_stats_t stats;
    sqlite_micro_get_stats(&stats);
    Serial.printf("\n--- Memory Stats ---\n");
    Serial.printf("Current: %d bytes\n", stats.current_memory);
    Serial.printf("Peak: %d bytes\n", stats.peak_memory);

    Serial.println("\n=== Example Complete ===");
}

void loop() {
    // Nothing to do in loop
    delay(1000);
}
