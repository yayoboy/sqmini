/*
 * SQLite on ESP32 with SD Card Example
 *
 * This example demonstrates how to use SQLite on ESP32 with SD Card storage.
 * SD cards provide large storage capacity for extensive data logging.
 *
 * Wiring (SPI mode):
 * - CS:   GPIO 5
 * - MOSI: GPIO 23
 * - MISO: GPIO 19
 * - SCK:  GPIO 18
 */

#include <Arduino.h>
#include "sqlite_micro/sqlite_micro.h"
#include "sqlite_micro/sqlite_config.h"

#define PIN_SD_CS   5
#define PIN_SD_MOSI 23
#define PIN_SD_MISO 19
#define PIN_SD_SCK  18

sqlite3 *db;
const char *db_path = "/sdcard/datalog.db";

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=== SQLite ESP32 SD Card Example ===");

    // Initialize SQLite
    int rc = sqlite_micro_init();
    if (rc != SQLITE_OK) {
        Serial.printf("Failed to initialize SQLite: %d\n", rc);
        return;
    }

    // Initialize SD Card
    Serial.println("Initializing SD card...");
    rc = sqlite_micro_init_sdcard(PIN_SD_CS, PIN_SD_MOSI, PIN_SD_MISO, PIN_SD_SCK);
    if (rc != SQLITE_OK) {
        Serial.println("Failed to initialize SD card!");
        Serial.println("Please check:");
        Serial.println("1. SD card is inserted");
        Serial.println("2. Wiring is correct");
        Serial.println("3. SD card is formatted (FAT32)");
        return;
    }

    Serial.println("SD card initialized!");

    // Register SD Card VFS
    rc = sqlite_micro_register_vfs(SQLITE_STORAGE_SDCARD, "sdcard");
    if (rc != SQLITE_OK) {
        Serial.println("Failed to register SD card VFS");
        return;
    }

    // Open database
    sqlite_vfs_config_t config = {
        .mount_point = "/sdcard",
        .db_path = db_path,
        .storage_type = SQLITE_STORAGE_SDCARD,
        .cache_size = 128  // 128KB cache - SD cards can handle larger cache
    };

    rc = sqlite_micro_open(db_path, &db, &config);
    if (rc != SQLITE_OK) {
        Serial.printf("Failed to open database: %d\n", rc);
        return;
    }

    Serial.println("Database opened successfully!");

    // Create a data logging table
    const char *sql_create = "CREATE TABLE IF NOT EXISTS datalog ("
                            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                            "device_id TEXT NOT NULL,"
                            "temperature REAL,"
                            "humidity REAL,"
                            "pressure REAL,"
                            "battery_voltage REAL,"
                            "rssi INTEGER,"
                            "timestamp INTEGER);";

    char *err_msg = NULL;
    rc = sqlite3_exec(db, sql_create, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        Serial.printf("SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return;
    }

    Serial.println("Table created successfully!");

    // Simulate data logging with batch insert
    Serial.println("\nSimulating data logging...");
    rc = sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, &err_msg);

    unsigned long start_time = millis();

    for (int i = 0; i < 100; i++) {
        char sql[512];
        snprintf(sql, sizeof(sql),
                "INSERT INTO datalog (device_id, temperature, humidity, pressure, "
                "battery_voltage, rssi, timestamp) "
                "VALUES ('ESP32-%03d', %.2f, %.2f, %.2f, %.2f, %d, %d);",
                i % 10,
                20.0 + (i % 15) * 0.5,
                50.0 + (i % 30),
                1013.0 + (i % 20) * 0.1,
                3.7 + (i % 5) * 0.1,
                -60 + (i % 40),
                1234567890 + i * 60);

        rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
        if (rc != SQLITE_OK) {
            Serial.printf("Insert error: %s\n", err_msg);
            sqlite3_free(err_msg);
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            return;
        }
    }

    rc = sqlite3_exec(db, "COMMIT;", NULL, NULL, &err_msg);
    unsigned long end_time = millis();

    Serial.printf("100 records inserted in %lu ms\n", end_time - start_time);

    // Query recent data
    const char *sql_query = "SELECT device_id, temperature, humidity, timestamp "
                           "FROM datalog ORDER BY id DESC LIMIT 5;";
    sqlite3_stmt *stmt;

    rc = sqlite3_prepare_v2(db, sql_query, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        Serial.printf("Prepare error: %s\n", sqlite3_errmsg(db));
        return;
    }

    Serial.println("\n--- Last 5 Records ---");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *device_id = (const char*)sqlite3_column_text(stmt, 0);
        double temp = sqlite3_column_double(stmt, 1);
        double humidity = sqlite3_column_double(stmt, 2);
        int timestamp = sqlite3_column_int(stmt, 3);

        Serial.printf("%s: T=%.1f°C, H=%.1f%%, Time=%d\n",
                     device_id, temp, humidity, timestamp);
    }

    sqlite3_finalize(stmt);

    // Get total record count
    const char *sql_count = "SELECT COUNT(*) FROM datalog;";
    rc = sqlite3_prepare_v2(db, sql_count, -1, &stmt, NULL);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        int count = sqlite3_column_int(stmt, 0);
        Serial.printf("\nTotal records in database: %d\n", count);
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
