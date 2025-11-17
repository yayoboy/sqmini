/*
 * SQLite on RP2040 with LittleFS Example
 *
 * This example demonstrates how to use SQLite on RP2040 with LittleFS storage.
 * LittleFS uses a portion of the RP2040's onboard flash memory.
 */

#include <Arduino.h>
#include "sqlite_micro/sqlite_micro.h"
#include "sqlite_micro/sqlite_config.h"

sqlite3 *db;
const char *db_path = "/lfs/sensor.db";

void setup() {
    Serial.begin(115200);
    delay(2000);  // Wait for serial connection

    Serial.println("\n=== SQLite RP2040 LittleFS Example ===");

    // Initialize SQLite
    int rc = sqlite_micro_init();
    if (rc != SQLITE_OK) {
        Serial.printf("Failed to initialize SQLite: %d\n", rc);
        return;
    }

    // Initialize LittleFS on RP2040
    rc = sqlite_micro_init_littlefs_rp2040();
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
        .mount_point = "/lfs",
        .db_path = db_path,
        .storage_type = SQLITE_STORAGE_LITTLEFS,
        .cache_size = 32  // 32KB cache for RP2040
    };

    rc = sqlite_micro_open(db_path, &db, &config);
    if (rc != SQLITE_OK) {
        Serial.printf("Failed to open database: %d\n", rc);
        return;
    }

    Serial.println("Database opened successfully!");

    // Create a table for IoT sensor data
    const char *sql_create = "CREATE TABLE IF NOT EXISTS iot_sensors ("
                            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                            "sensor_type TEXT NOT NULL,"
                            "value REAL NOT NULL,"
                            "timestamp INTEGER);";

    char *err_msg = NULL;
    rc = sqlite3_exec(db, sql_create, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        Serial.printf("SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return;
    }

    Serial.println("Table created successfully!");

    // Insert sensor readings
    const char *sql_insert = "INSERT INTO iot_sensors (sensor_type, value, timestamp) VALUES "
                            "('Temperature', 22.5, 1000),"
                            "('Humidity', 58.3, 1001),"
                            "('Light', 450.0, 1002),"
                            "('Motion', 1.0, 1003);";

    rc = sqlite3_exec(db, sql_insert, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        Serial.printf("Insert error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return;
    }

    Serial.println("Data inserted successfully!");

    // Query all sensor data
    const char *sql_query = "SELECT * FROM iot_sensors;";
    sqlite3_stmt *stmt;

    rc = sqlite3_prepare_v2(db, sql_query, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        Serial.printf("Prepare error: %s\n", sqlite3_errmsg(db));
        return;
    }

    Serial.println("\n--- IoT Sensor Data ---");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const char *type = (const char*)sqlite3_column_text(stmt, 1);
        double value = sqlite3_column_double(stmt, 2);
        int timestamp = sqlite3_column_int(stmt, 3);

        Serial.printf("ID: %d, Type: %s, Value: %.2f, Time: %d\n",
                     id, type, value, timestamp);
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
    // Blink LED to show we're running
    static bool led_state = false;
    digitalWrite(LED_BUILTIN, led_state);
    led_state = !led_state;
    delay(1000);
}
