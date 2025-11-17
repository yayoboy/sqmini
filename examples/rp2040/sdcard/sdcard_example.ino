/*
 * SQLite on RP2040 with SD Card Example
 *
 * This example demonstrates how to use SQLite on RP2040 with SD Card storage.
 *
 * Default SPI pins (can be changed):
 * - CS:   GPIO 17
 * - MOSI: GPIO 19
 * - MISO: GPIO 16
 * - SCK:  GPIO 18
 */

#include <Arduino.h>
#include "sqlite_micro/sqlite_micro.h"
#include "sqlite_micro/sqlite_config.h"

#define PIN_SD_CS   17
#define PIN_SD_MOSI 19
#define PIN_SD_MISO 16
#define PIN_SD_SCK  18

sqlite3 *db;
const char *db_path = "/sd/datalog.db";

void setup() {
    Serial.begin(115200);
    delay(2000);  // Wait for serial connection

    Serial.println("\n=== SQLite RP2040 SD Card Example ===");

    // Initialize built-in LED
    pinMode(LED_BUILTIN, OUTPUT);

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
        .mount_point = "/sd",
        .db_path = db_path,
        .storage_type = SQLITE_STORAGE_SDCARD,
        .cache_size = 64  // 64KB cache
    };

    rc = sqlite_micro_open(db_path, &db, &config);
    if (rc != SQLITE_OK) {
        Serial.printf("Failed to open database: %d\n", rc);
        return;
    }

    Serial.println("Database opened successfully!");

    // Create environmental monitoring table
    const char *sql_create = "CREATE TABLE IF NOT EXISTS environment ("
                            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                            "location TEXT NOT NULL,"
                            "temperature REAL,"
                            "humidity REAL,"
                            "air_quality INTEGER,"
                            "timestamp INTEGER);";

    char *err_msg = NULL;
    rc = sqlite3_exec(db, sql_create, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        Serial.printf("SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return;
    }

    Serial.println("Table created successfully!");

    // Insert sample environmental data with transaction
    Serial.println("\nInserting environmental data...");
    rc = sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, &err_msg);

    const char *locations[] = {"Living Room", "Bedroom", "Kitchen", "Office"};
    int num_locations = sizeof(locations) / sizeof(locations[0]);

    for (int i = 0; i < 20; i++) {
        char sql[256];
        snprintf(sql, sizeof(sql),
                "INSERT INTO environment (location, temperature, humidity, air_quality, timestamp) "
                "VALUES ('%s', %.2f, %.2f, %d, %d);",
                locations[i % num_locations],
                18.0 + (i % 10) * 0.8,
                45.0 + (i % 25),
                80 + (i % 20),
                1000 + i * 300);

        rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
        if (rc != SQLITE_OK) {
            Serial.printf("Insert error: %s\n", err_msg);
            sqlite3_free(err_msg);
            sqlite3_exec(db, "ROLLBACK;", NULL, NULL, NULL);
            return;
        }
    }

    rc = sqlite3_exec(db, "COMMIT;", NULL, NULL, &err_msg);
    Serial.println("20 environmental records inserted!");

    // Query data by location
    const char *sql_query = "SELECT location, AVG(temperature), AVG(humidity), COUNT(*) "
                           "FROM environment GROUP BY location;";
    sqlite3_stmt *stmt;

    rc = sqlite3_prepare_v2(db, sql_query, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        Serial.printf("Prepare error: %s\n", sqlite3_errmsg(db));
        return;
    }

    Serial.println("\n--- Environmental Averages by Location ---");
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *location = (const char*)sqlite3_column_text(stmt, 0);
        double avg_temp = sqlite3_column_double(stmt, 1);
        double avg_humidity = sqlite3_column_double(stmt, 2);
        int count = sqlite3_column_int(stmt, 3);

        Serial.printf("%s: Temp=%.1f°C, Humidity=%.1f%%, Samples=%d\n",
                     location, avg_temp, avg_humidity, count);
    }

    sqlite3_finalize(stmt);

    // Memory statistics
    sqlite_micro_stats_t stats;
    sqlite_micro_get_stats(&stats);
    Serial.printf("\n--- Memory Stats ---\n");
    Serial.printf("Current: %d bytes\n", stats.current_memory);
    Serial.printf("Peak: %d bytes\n", stats.peak_memory);

    Serial.println("\n=== Example Complete ===");
    digitalWrite(LED_BUILTIN, HIGH);
}

void loop() {
    delay(1000);
}
