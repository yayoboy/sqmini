# SQLite Micro v2.0 - SQLite per Microcontrollori con FreeRTOS

Una implementazione **completa e ottimizzata** di SQLite per microcontrollori **ESP32** e **RP2040** con supporto **FreeRTOS**, **API asincrona**, **connection pooling** e molto altro.

## 🚀 Novità v2.0

### ⚡ API Non-Bloccante con FreeRTOS
- **Operazioni asincrone** con callback
- **Query queue** con priorità configurabili
- **Background worker task** dedicato
- **Nessun blocco** del task principale

### 🔄 Connection Pooling Avanzato
- **Pool di connessioni** gestito automaticamente
- **Min/Max connections** configurabili
- **Timeout e retry** automatici
- **Statistiche real-time** sul pool

### 💾 Prepared Statement Cache
- **Cache LRU** per statement preparati
- **Eviction automatica** degli statement meno usati
- **Hit rate tracking** e statistiche
- **Riduzione overhead** di preparazione query

### 📊 Monitoring e Health Check
- **Watchdog integration** per affidabilità
- **Slow query detection** con alert
- **Performance metrics** in tempo reale
- **Health checks** automatici

### 💿 Sistema di Backup Automatico
- **Backup schedulati** (hourly/daily/weekly)
- **Backup incrementali** e differenziali
- **Retention policy** configurabile
- **Verifica integrità** automatica

### ⚙️ WAL Mode & Ottimizzazioni
- **Write-Ahead Logging** per performance
- **Transazioni ottimizzate** per flash
- **Memory pool** dedicato
- **Logging avanzato** con livelli

## Caratteristiche Complete

### Piattaforme & Storage
- ✅ **ESP32** (tutte le varianti: S2, S3, C3, C6)
- ✅ **RP2040** (Raspberry Pi Pico)
- ✅ **SPIFFS** (ESP32)
- ✅ **LittleFS** (ESP32 e RP2040)
- ✅ **SD Card** (ESP32 e RP2040)

### FreeRTOS Integration
- ✅ **Mutex e Semaphores** nativi FreeRTOS
- ✅ **Task dedicati** per operazioni database
- ✅ **Event groups** per sincronizzazione
- ✅ **Queue** per richieste asincrone
- ✅ **Multi-core support** (ESP32)

### Performance & Reliability
- ✅ **WAL mode** per scritture veloci
- ✅ **Connection pooling** per accesso concorrente
- ✅ **Statement caching** per query ripetitive
- ✅ **Watchdog** per monitoraggio affidabilità
- ✅ **Auto-backup** per protezione dati

## Installazione Rapida

```bash
git clone https://github.com/yayoboy/sqmini.git
cd sqmini
```

### PlatformIO
```bash
pio run -e esp32-freertos -t upload
```

### Arduino IDE
1. Scarica come ZIP
2. Sketch → Include Library → Add .ZIP Library
3. Includi: `#include "sqlite_micro/sqlite_async.h"`

## Quick Start - API Asincrona

### Esempio Base FreeRTOS

```cpp
#include "sqlite_micro/sqlite_async.h"
#include "sqlite_micro/sqlite_pool.h"

sqlite_pool_t *pool;

// Callback per operazioni async
void on_complete(sqlite_async_result_t *result, void *user_data) {
    if (result->status == SQLITE_ASYNC_COMPLETED) {
        Serial.printf("✓ Query completata in %d ms\n",
                     result->execution_time_ms);
    }
}

void setup() {
    // 1. Inizializza SQLite Micro
    sqlite_micro_init();
    sqlite_micro_init_littlefs_esp32("/littlefs", 5);

    // 2. Crea connection pool con WAL
    sqlite_pool_config_t pool_cfg = {
        .db_path = "/littlefs/app.db",
        .min_connections = 2,
        .max_connections = 4,
        .enable_wal = true  // WAL mode!
    };
    pool = sqlite_pool_create(&pool_cfg);

    // 3. Inizializza sistema async
    sqlite_async_config_t async_cfg = SQLITE_ASYNC_DEFAULT_CONFIG();
    sqlite_async_init(&async_cfg);

    // 4. Esegui query async (non-bloccante!)
    sqlite3 *db = sqlite_pool_acquire(pool, 5000);

    sqlite_async_exec(db,
        "INSERT INTO sensors (temp, humidity) VALUES (23.5, 65.2)",
        on_complete,
        NULL,
        SQLITE_PRIORITY_NORMAL
    );

    sqlite_pool_release(pool, db);
}
```

### Connection Pooling

```cpp
// Acquisisci connessione dal pool
sqlite3 *db = sqlite_pool_acquire(pool, 5000);  // timeout 5s

if (db) {
    // Usa il database
    sqlite3_exec(db, "INSERT INTO ...", NULL, NULL, NULL);

    // Rilascia sempre la connessione!
    sqlite_pool_release(pool, db);
}

// Statistiche pool
sqlite_pool_stats_t stats;
sqlite_pool_get_stats(pool, &stats);
Serial.printf("Active: %d, Idle: %d, Peak: %d\n",
             stats.active_connections,
             stats.idle_connections,
             stats.peak_connections);
```

### Prepared Statement Cache

```cpp
// Crea cache per statement
sqlite_cache_config_t cache_cfg = SQLITE_CACHE_DEFAULT_CONFIG();
sqlite_stmt_cache_t *cache = sqlite_cache_create(db, &cache_cfg);

// Ottieni statement dalla cache (o prepara se non presente)
sqlite3_stmt *stmt = sqlite_cache_get_stmt(cache,
    "SELECT * FROM sensors WHERE id = ?");

if (stmt) {
    sqlite3_bind_int(stmt, 1, sensor_id);
    sqlite3_step(stmt);

    // Rilascia statement (torna in cache, non viene finalizzato!)
    sqlite_cache_release_stmt(cache, stmt);
}

// Statistiche cache
sqlite_cache_stats_t cache_stats;
sqlite_cache_get_stats(cache, &cache_stats);
Serial.printf("Hit rate: %.1f%%, Entries: %d\n",
             cache_stats.hit_rate * 100,
             cache_stats.current_size);
```

### Query Asincrone con Callback

```cpp
// Callback chiamato per ogni riga
void on_row(sqlite3_stmt *stmt, void *user_data) {
    int id = sqlite3_column_int(stmt, 0);
    const char *name = (const char*)sqlite3_column_text(stmt, 1);
    Serial.printf("Row: %d - %s\n", id, name);
}

// Callback di completamento
void on_query_done(sqlite_async_result_t *result, void *user_data) {
    Serial.printf("Query terminata: %d righe\n",
                 result->rows_affected);
}

// Esegui query async
sqlite_async_query(db,
    "SELECT * FROM sensors ORDER BY timestamp DESC LIMIT 10",
    on_row,           // chiamato per ogni riga
    on_query_done,    // chiamato alla fine
    NULL,
    SQLITE_PRIORITY_HIGH
);
```

### Transazioni Asincrone

```cpp
// Begin transaction (async)
uint32_t req_id = sqlite_async_begin_transaction(db, on_complete, NULL);

// Inserimenti multipli
for (int i = 0; i < 100; i++) {
    char sql[256];
    snprintf(sql, sizeof(sql),
            "INSERT INTO data VALUES (%d, %f)", i, value);
    sqlite_async_exec(db, sql, NULL, NULL, SQLITE_PRIORITY_NORMAL);
}

// Commit transaction (async)
sqlite_async_commit_transaction(db, on_complete, NULL);
```

### Monitoring e Health Check

```cpp
// Inizializza monitoring
sqlite_monitor_init(SQLITE_MONITOR_LEVEL_INFO);

// Configura slow query detection (500ms threshold)
sqlite_monitor_set_slow_query_threshold(500);
sqlite_monitor_set_slow_query_callback([](const char *sql, uint32_t ms) {
    Serial.printf("⚠️ Slow query (%d ms): %s\n", ms, sql);
});

// Health check periodico
sqlite_health_report_t health;
sqlite_monitor_health_check(&health);

if (health.status == SQLITE_HEALTH_CRITICAL) {
    Serial.println("🚨 Sistema in stato critico!");
}

// Performance metrics
sqlite_perf_metrics_t metrics;
sqlite_monitor_get_metrics(&metrics);
Serial.printf("Queries: %d, Errors: %d, Avg time: %d ms\n",
             metrics.query_count,
             metrics.error_count,
             metrics.avg_execution_time_ms);
```

### Backup Automatico

```cpp
// Inizializza backup system
sqlite_backup_init();

// Configura auto-backup
sqlite_auto_backup_config_t backup_cfg = {
    .schedule = SQLITE_BACKUP_SCHED_DAILY,
    .retention_hours = 168,  // 7 giorni
    .backup_on_close = true,
    .backup_directory = "/littlefs/backups"
};

sqlite_backup_configure_auto(db, &backup_cfg);
sqlite_backup_auto_start(db);

// Backup manuale con progress
sqlite_backup_async(db, "/littlefs/manual_backup.db", NULL,
    [](sqlite_backup_progress_t *progress, void *data) {
        Serial.printf("Backup: %d%%\n", progress->percent_complete);
    },
    NULL
);

// Verifica backup
if (sqlite_backup_verify("/littlefs/backup.db") == SQLITE_OK) {
    Serial.println("✓ Backup valido");
}
```

### Watchdog Integration

```cpp
// Configura watchdog
sqlite_watchdog_config_t wd_cfg = {
    .timeout_ms = 60000,        // 60 secondi
    .auto_reset = true,
    .panic_on_timeout = false
};

sqlite_watchdog_init(&wd_cfg);

// Feed watchdog periodicamente
void monitoring_task(void *param) {
    while (true) {
        sqlite_watchdog_feed();  // Reset watchdog
        vTaskDelay(pdMS_TO_TICKS(30000));  // ogni 30s
    }
}
```

## FreeRTOS Multi-Task Example

```cpp
// Task per sensori (Core 0)
void sensor_task(void *param) {
    while (true) {
        sqlite3 *db = sqlite_pool_acquire(pool, 5000);

        // Inserimento async
        char sql[128];
        snprintf(sql, sizeof(sql),
                "INSERT INTO readings VALUES (%f, %lu)",
                read_sensor(), millis());

        sqlite_async_exec(db, sql, NULL, NULL, SQLITE_PRIORITY_NORMAL);
        sqlite_pool_release(pool, db);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Task per logging (Core 1)
void logger_task(void *param) {
    while (true) {
        sqlite3 *db = sqlite_pool_acquire(pool, 5000);

        // Query async con callback
        sqlite_async_query(db,
            "SELECT * FROM readings ORDER BY time DESC LIMIT 10",
            row_callback,
            completion_callback,
            NULL,
            SQLITE_PRIORITY_LOW
        );

        sqlite_pool_release(pool, db);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

// Task per monitoring (Core 0)
void monitor_task(void *param) {
    while (true) {
        sqlite_health_report_t health;
        sqlite_monitor_health_check(&health);

        log_health_status(&health);
        sqlite_watchdog_feed();

        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

void setup() {
    // Inizializza tutto...

    // Crea task su core diversi
    xTaskCreatePinnedToCore(sensor_task, "Sensor", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(logger_task, "Logger", 4096, NULL, 4, NULL, 1);
    xTaskCreatePinnedToCore(monitor_task, "Monitor", 4096, NULL, 6, NULL, 0);
}
```

## Struttura Progetto v2.0

```
sqmini/
├── src/
│   ├── sqlite3.c                    # SQLite 3.47.2 amalgamation
│   ├── sqlite_micro.c               # Core API
│   ├── sqlite_async.c               # 🆕 Async API con FreeRTOS
│   ├── sqlite_pool.c                # 🆕 Connection pooling
│   ├── sqlite_cache.c               # 🆕 Statement cache
│   ├── sqlite_monitor.c             # 🆕 Monitoring & watchdog
│   ├── sqlite_backup.c              # 🆕 Backup automatico
│   ├── vfs/
│   │   ├── vfs_spiffs.c
│   │   ├── vfs_littlefs.c
│   │   └── vfs_sdcard.c
│   └── platform/
│       └── pico_hal_littlefs.c
├── include/
│   └── sqlite_micro/
│       ├── sqlite_micro.h
│       ├── sqlite_config.h
│       ├── sqlite_async.h           # 🆕 Async API
│       ├── sqlite_pool.h            # 🆕 Pooling
│       ├── sqlite_cache.h           # 🆕 Caching
│       ├── sqlite_monitor.h         # 🆕 Monitoring
│       └── sqlite_backup.h          # 🆕 Backup
├── examples/
│   ├── esp32/
│   │   ├── spiffs/
│   │   ├── littlefs/
│   │   ├── sdcard/
│   │   └── freertos/                # 🆕 FreeRTOS examples
│   │       ├── freertos_async_example.ino
│   │       └── freertos_monitoring_example.ino
│   └── rp2040/
│       ├── littlefs/
│       └── sdcard/
└── docs/                            # 🆕 Documentazione estesa
    ├── API.md
    ├── FREERTOS.md
    ├── PERFORMANCE.md
    └── TROUBLESHOOTING.md
```

## Performance Comparison

| Feature | v1.0 | v2.0 FreeRTOS |
|---------|------|---------------|
| Insert 1000 records | ~1200ms | ~180ms (WAL + transactions) |
| Concurrent queries | ❌ Blocking | ✅ Non-blocking |
| Connection reuse | ❌ No | ✅ Pool (4x faster) |
| Statement preparation | Every time | ✅ Cached (10x faster) |
| Error recovery | Manual | ✅ Automatic |
| Backup | Manual | ✅ Scheduled |

## Best Practices v2.0

### 1. Usa sempre il Connection Pool
```cpp
// ❌ NON fare così
sqlite3 *db;
sqlite3_open(path, &db);

// ✅ Usa il pool
sqlite3 *db = sqlite_pool_acquire(pool, timeout);
// ... usa db ...
sqlite_pool_release(pool, db);
```

### 2. Preferisci API Async per operazioni lunghe
```cpp
// ❌ Operazione bloccante
sqlite3_exec(db, "INSERT INTO large_table ...", NULL, NULL, NULL);

// ✅ Non-bloccante
sqlite_async_exec(db, "INSERT INTO large_table ...",
                 callback, data, SQLITE_PRIORITY_NORMAL);
```

### 3. Usa Prepared Statement Cache
```cpp
// ❌ Prepara ogni volta
for (int i = 0; i < 1000; i++) {
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);  // Costoso!
}

// ✅ Usa cache
sqlite3_stmt *stmt = sqlite_cache_get_stmt(cache, sql);
for (int i = 0; i < 1000; i++) {
    sqlite3_bind_int(stmt, 1, i);
    sqlite3_step(stmt);
    sqlite3_reset(stmt);  // Veloce!
}
sqlite_cache_release_stmt(cache, stmt);
```

### 4. Abilita WAL Mode
```cpp
sqlite_pool_config_t cfg = {
    .enable_wal = true,  // ✅ Sempre true per performance
    // ...
};
```

### 5. Monitora Performance
```cpp
// Imposta threshold per slow queries
sqlite_monitor_set_slow_query_threshold(100);  // 100ms

// Alert automatici
sqlite_monitor_set_slow_query_callback(on_slow_query);
```

### 6. Configura Backup Automatico
```cpp
sqlite_auto_backup_config_t cfg = {
    .schedule = SQLITE_BACKUP_SCHED_DAILY,
    .retention_hours = 168,  // 7 giorni
    .backup_on_close = true
};
```

## Esempi Completi

### 📁 [examples/esp32/freertos/](examples/esp32/freertos/)
- `freertos_async_example.ino` - Async API, pooling, caching
- `freertos_monitoring_example.ino` - Monitoring, watchdog, backup

### 📖 Documentazione Completa
- [API Reference](docs/API.md)
- [FreeRTOS Integration](docs/FREERTOS.md)
- [Performance Tuning](docs/PERFORMANCE.md)
- [Troubleshooting](docs/TROUBLESHOOTING.md)

## Configurazione Avanzata

### Memory Settings
```c
// include/sqlite_micro/sqlite_config.h

// ESP32 con PSRAM
#define SQLITE_DEFAULT_CACHE_SIZE -256  // 256KB cache
#define SQLITE_MAX_MEMORY 524288        // 512KB max

// RP2040 limitato
#define SQLITE_DEFAULT_CACHE_SIZE -32   // 32KB cache
#define SQLITE_MAX_MEMORY 65536         // 64KB max
```

### WAL Configuration
```cpp
// Ottimizzazioni WAL
sqlite3_exec(db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
sqlite3_exec(db, "PRAGMA synchronous=NORMAL", NULL, NULL, NULL);
sqlite3_exec(db, "PRAGMA wal_autocheckpoint=100", NULL, NULL, NULL);
sqlite3_exec(db, "PRAGMA busy_timeout=5000", NULL, NULL, NULL);
```

## Troubleshooting

### "Queue full" error
```cpp
// Aumenta dimensione queue
sqlite_async_config_t cfg = SQLITE_ASYNC_DEFAULT_CONFIG();
cfg.queue_size = 128;  // Default: 32
sqlite_async_init(&cfg);
```

### Memoria insufficiente
```cpp
// Riduci pool e cache
pool_cfg.max_connections = 2;  // Invece di 4
cache_cfg.max_statements = 16; // Invece di 32
```

### Performance non ottimali
```cpp
// 1. Verifica WAL sia abilitato
// 2. Usa transazioni per batch insert
// 3. Aumenta cache size
// 4. Monitora slow queries
```

## Roadmap v2.1

- [ ] Support STM32 platform
- [ ] Support nRF52 platform
- [ ] Encryption support (AES-256)
- [ ] Compression for backups (zlib)
- [ ] Web dashboard via WiFi
- [ ] MQTT integration
- [ ] OTA database sync
- [ ] Time-series optimizations

## Contribuire

Contributi benvenuti! Vedi [CONTRIBUTING.md](CONTRIBUTING.md)

## Licenza

MIT License - vedi [LICENSE](LICENSE)

SQLite è Public Domain - https://www.sqlite.org/

---

**Versione**: 2.0.0
**Autore**: SQLite Micro Contributors
**Repository**: https://github.com/yayoboy/sqmini
**Issues**: https://github.com/yayoboy/sqmini/issues
