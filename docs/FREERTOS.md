# FreeRTOS Integration Guide

Guida completa all'integrazione di SQLite Micro con FreeRTOS per ESP32 e RP2040.

## Indice

1. [Introduzione](#introduzione)
2. [Architettura](#architettura)
3. [API Asincrona](#api-asincrona)
4. [Connection Pooling](#connection-pooling)
5. [Task Patterns](#task-patterns)
6. [Sincronizzazione](#sincronizzazione)
7. [Best Practices](#best-practices)
8. [Troubleshooting](#troubleshooting)

## Introduzione

SQLite Micro v2.0 integra completamente FreeRTOS per fornire:

- **Operazioni non-bloccanti**: Le query non bloccano il task chiamante
- **Multi-threading sicuro**: Protezione con mutex e semafori FreeRTOS
- **Task dedicati**: Worker task in background per operazioni database
- **Multi-core support**: Sfrutta entrambi i core ESP32

## Architettura

### Componenti Principali

```
┌─────────────────┐
│  User Tasks     │ (Sensor, Logger, UI, etc.)
└────────┬────────┘
         │ Async API
┌────────▼────────┐
│  Request Queue  │ (Priority Queue FreeRTOS)
└────────┬────────┘
         │
┌────────▼────────┐
│  Worker Task    │ (Dedicated SQLite Task)
└────────┬────────┘
         │
┌────────▼────────┐
│ Connection Pool │ (2-8 connections)
└────────┬────────┘
         │
┌────────▼────────┐
│   SQLite VFS    │ (SPIFFS/LittleFS/SD)
└─────────────────┘
```

### Thread Safety

SQLite Micro usa `SQLITE_THREADSAFE=1` (serialized mode) con:

- **Mutex FreeRTOS** per proteggere strutture condivise
- **Semafori** per gestire disponibilità connessioni
- **Event Groups** per notifiche tra task
- **Queue** per comunicazione asincrona

## API Asincrona

### Inizializzazione

```cpp
#include "sqlite_micro/sqlite_async.h"

void setup() {
    // Configura async system
    sqlite_async_config_t config = {
        .task_name = "sqlite_worker",
        .task_stack_size = 8192,      // 8KB stack
        .task_priority = 5,            // Priorità media
        .queue_size = 64,              // Max 64 richieste in coda
        .max_concurrent_ops = 4,       // Max 4 operazioni simultanee
        .enable_stats = true
    };

    int rc = sqlite_async_init(&config);
    if (rc != SQLITE_OK) {
        Serial.println("Failed to init async!");
    }
}
```

### Esecuzione Asincrona

#### Esempio 1: INSERT Asincrono

```cpp
void on_insert_complete(sqlite_async_result_t *result, void *user_data) {
    if (result->status == SQLITE_ASYNC_COMPLETED) {
        Serial.printf("✓ Inserito in %d ms\n", result->execution_time_ms);
    } else {
        Serial.printf("✗ Errore: %s\n", result->error_msg);
    }
}

void insert_sensor_data(float temperature) {
    sqlite3 *db = sqlite_pool_acquire(pool, 5000);

    char sql[128];
    snprintf(sql, sizeof(sql),
            "INSERT INTO sensors (temp, time) VALUES (%.2f, %lu)",
            temperature, millis());

    // Operazione non-bloccante!
    uint32_t req_id = sqlite_async_exec(
        db,
        sql,
        on_insert_complete,
        NULL,
        SQLITE_PRIORITY_NORMAL
    );

    sqlite_pool_release(pool, db);

    // Il task continua immediatamente senza aspettare
    Serial.printf("Request %d queued\n", req_id);
}
```

#### Esempio 2: Query con Row Callback

```cpp
void on_row(sqlite3_stmt *stmt, void *user_data) {
    int id = sqlite3_column_int(stmt, 0);
    double temp = sqlite3_column_double(stmt, 1);
    uint32_t time = sqlite3_column_int(stmt, 2);

    Serial.printf("Sensor %d: %.2f°C at %lu\n", id, temp, time);
}

void on_query_complete(sqlite_async_result_t *result, void *user_data) {
    Serial.printf("Query completata: %d righe\n", result->rows_affected);
}

void query_recent_data() {
    sqlite3 *db = sqlite_pool_acquire(pool, 5000);

    sqlite_async_query(
        db,
        "SELECT * FROM sensors ORDER BY time DESC LIMIT 10",
        on_row,              // Chiamato per ogni riga
        on_query_complete,   // Chiamato alla fine
        NULL,
        SQLITE_PRIORITY_HIGH
    );

    sqlite_pool_release(pool, db);
}
```

### Priorità Richieste

```cpp
typedef enum {
    SQLITE_PRIORITY_LOW = 0,       // Operazioni in background
    SQLITE_PRIORITY_NORMAL = 1,    // Operazioni normali
    SQLITE_PRIORITY_HIGH = 2,      // Operazioni importanti
    SQLITE_PRIORITY_CRITICAL = 3   // Operazioni critiche (es. errori)
} sqlite_async_priority_t;
```

Esempio:

```cpp
// Backup in background - bassa priorità
sqlite_async_exec(db, "VACUUM", callback, NULL, SQLITE_PRIORITY_LOW);

// Inserimento dati - normale
sqlite_async_exec(db, "INSERT ...", callback, NULL, SQLITE_PRIORITY_NORMAL);

// Rollback - alta priorità
sqlite_async_rollback_transaction(db, callback, NULL);  // PRIORITY_CRITICAL
```

## Connection Pooling

### Configurazione Pool

```cpp
#include "sqlite_micro/sqlite_pool.h"

sqlite_pool_config_t pool_config = {
    .db_path = "/littlefs/app.db",
    .vfs_config = &vfs_cfg,
    .min_connections = 2,          // Sempre 2 connessioni aperte
    .max_connections = 6,          // Massimo 6 connessioni
    .connection_timeout_ms = 5000, // Timeout acquire: 5s
    .idle_timeout_ms = 120000,     // Chiudi dopo 2min idle
    .enable_wal = true,            // Abilita WAL mode
    .enable_shared_cache = false
};

pool = sqlite_pool_create(&pool_config);
```

### Pattern Acquire/Release

```cpp
void database_operation() {
    // 1. Acquisisci connessione (bloccante con timeout)
    sqlite3 *db = sqlite_pool_acquire(pool, 5000);

    if (!db) {
        Serial.println("Timeout acquiring connection!");
        return;
    }

    // 2. Usa la connessione
    sqlite3_exec(db, "INSERT INTO ...", NULL, NULL, NULL);

    // 3. SEMPRE rilascia la connessione!
    sqlite_pool_release(pool, db);
}
```

⚠️ **IMPORTANTE**: Rilascia SEMPRE le connessioni, anche in caso di errore:

```cpp
sqlite3 *db = sqlite_pool_acquire(pool, 5000);
if (!db) return;

int rc = sqlite3_exec(db, sql, NULL, NULL, &err);

if (rc != SQLITE_OK) {
    Serial.printf("Error: %s\n", err);
    sqlite3_free(err);
}

// Rilascia anche se c'è errore!
sqlite_pool_release(pool, db);
```

### Statistiche Pool

```cpp
sqlite_pool_stats_t stats;
sqlite_pool_get_stats(pool, &stats);

Serial.printf("Pool Statistics:\n");
Serial.printf("  Total: %d\n", stats.total_connections);
Serial.printf("  Active: %d\n", stats.active_connections);
Serial.printf("  Idle: %d\n", stats.idle_connections);
Serial.printf("  Peak: %d\n", stats.peak_connections);
Serial.printf("  Acquires: %d\n", stats.total_acquires);
Serial.printf("  Timeouts: %d\n", stats.acquire_timeouts);
```

### Shrink Pool

```cpp
// Riduci pool rimuovendo connessioni idle
// (utile per liberare memoria)
void cleanup_task(void *param) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(300000));  // Ogni 5 minuti

        int removed = sqlite_pool_shrink(pool);
        Serial.printf("Removed %d idle connections\n", removed);
    }
}
```

## Task Patterns

### Pattern 1: Single Producer

Un task scrive nel database:

```cpp
void sensor_task(void *param) {
    TickType_t last_wake = xTaskGetTickCount();

    while (true) {
        // Leggi sensore
        float temp = read_temperature();

        // Scrivi async
        sqlite3 *db = sqlite_pool_acquire(pool, 5000);
        if (db) {
            char sql[128];
            snprintf(sql, sizeof(sql),
                    "INSERT INTO sensors VALUES (%.2f, %lu)",
                    temp, millis());

            sqlite_async_exec(db, sql, NULL, NULL, SQLITE_PRIORITY_NORMAL);
            sqlite_pool_release(pool, db);
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(1000));
    }
}

xTaskCreate(sensor_task, "Sensor", 4096, NULL, 5, NULL);
```

### Pattern 2: Multiple Producers

Più task scrivono concorrentemente:

```cpp
void producer_task(void *param) {
    int task_id = (int)param;

    while (true) {
        sqlite3 *db = sqlite_pool_acquire(pool, 5000);

        if (db) {
            char sql[128];
            snprintf(sql, sizeof(sql),
                    "INSERT INTO events (task_id, value) VALUES (%d, %d)",
                    task_id, random(100));

            sqlite_async_exec(db, sql, NULL, NULL, SQLITE_PRIORITY_NORMAL);
            sqlite_pool_release(pool, db);
        }

        vTaskDelay(pdMS_TO_TICKS(500 + random(1000)));
    }
}

// Crea 4 producer task
for (int i = 0; i < 4; i++) {
    xTaskCreate(producer_task, "Producer", 4096, (void*)i, 5, NULL);
}
```

### Pattern 3: Producer-Consumer

Producer scrive, Consumer legge:

```cpp
// Producer
void writer_task(void *param) {
    while (true) {
        sqlite3 *db = sqlite_pool_acquire(pool, 5000);

        if (db) {
            // Batch insert con transazione
            sqlite_async_begin_transaction(db, NULL, NULL);

            for (int i = 0; i < 10; i++) {
                char sql[128];
                snprintf(sql, sizeof(sql),
                        "INSERT INTO queue VALUES (%lu, 'data_%d')",
                        millis(), i);
                sqlite_async_exec(db, sql, NULL, NULL, SQLITE_PRIORITY_NORMAL);
            }

            sqlite_async_commit_transaction(db, NULL, NULL);
            sqlite_pool_release(pool, db);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// Consumer
void reader_task(void *param) {
    while (true) {
        sqlite3 *db = sqlite_pool_acquire(pool, 5000);

        if (db) {
            sqlite_async_query(db,
                "SELECT * FROM queue LIMIT 100",
                process_row,
                on_complete,
                NULL,
                SQLITE_PRIORITY_NORMAL);

            sqlite_pool_release(pool, db);
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

xTaskCreate(writer_task, "Writer", 4096, NULL, 5, NULL);
xTaskCreate(reader_task, "Reader", 4096, NULL, 4, NULL);
```

### Pattern 4: Multi-Core (ESP32)

Distribuisci task sui due core:

```cpp
void setup() {
    // Core 0: Operazioni critiche
    xTaskCreatePinnedToCore(
        sensor_task,
        "Sensor",
        4096,
        NULL,
        6,              // Alta priorità
        NULL,
        0               // Core 0
    );

    xTaskCreatePinnedToCore(
        control_task,
        "Control",
        4096,
        NULL,
        7,              // Massima priorità
        NULL,
        0               // Core 0
    );

    // Core 1: Database e logging
    xTaskCreatePinnedToCore(
        database_task,
        "Database",
        8192,
        NULL,
        4,              // Media priorità
        NULL,
        1               // Core 1
    );

    xTaskCreatePinnedToCore(
        logger_task,
        "Logger",
        4096,
        NULL,
        3,              // Bassa priorità
        NULL,
        1               // Core 1
    );
}
```

## Sincronizzazione

### Event Groups

Sincronizza operazioni database con altri eventi:

```cpp
EventGroupHandle_t events;

#define DB_READY_BIT    (1 << 0)
#define DATA_READY_BIT  (1 << 1)

void init_task(void *param) {
    // Inizializza database
    init_database();

    // Segnala che DB è pronto
    xEventGroupSetBits(events, DB_READY_BIT);

    vTaskDelete(NULL);
}

void worker_task(void *param) {
    // Aspetta che DB sia pronto
    xEventGroupWaitBits(events, DB_READY_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

    // Ora può usare il database
    while (true) {
        // ...
    }
}

void setup() {
    events = xEventGroupCreate();

    xTaskCreate(init_task, "Init", 4096, NULL, 7, NULL);
    xTaskCreate(worker_task, "Worker", 4096, NULL, 5, NULL);
}
```

### Semaphores per Rate Limiting

```cpp
SemaphoreHandle_t rate_limiter;

void setup() {
    // Max 10 operazioni al secondo
    rate_limiter = xSemaphoreCreateCounting(10, 10);

    xTaskCreate(rate_limited_task, "RateLimited", 4096, NULL, 5, NULL);

    // Task che ricarica il semaforo ogni secondo
    xTaskCreate(rate_reloader_task, "Reloader", 2048, NULL, 3, NULL);
}

void rate_limited_task(void *param) {
    while (true) {
        // Aspetta permesso
        if (xSemaphoreTake(rate_limiter, pdMS_TO_TICKS(1000)) == pdPASS) {
            sqlite3 *db = sqlite_pool_acquire(pool, 5000);

            if (db) {
                sqlite_async_exec(db, "INSERT ...", NULL, NULL, SQLITE_PRIORITY_NORMAL);
                sqlite_pool_release(pool, db);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void rate_reloader_task(void *param) {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Ricarica fino a 10 permessi
        for (int i = 0; i < 10; i++) {
            xSemaphoreGive(rate_limiter);
        }
    }
}
```

## Best Practices

### 1. Dimensiona Correttamente lo Stack

```cpp
// ❌ Stack troppo piccolo
xTaskCreate(db_task, "DB", 2048, NULL, 5, NULL);  // Potrebbe crashare!

// ✅ Stack adeguato per operazioni DB
xTaskCreate(db_task, "DB", 8192, NULL, 5, NULL);  // Sicuro
```

### 2. Gestisci Sempre gli Errori

```cpp
// ❌ Non controlla errori
sqlite3 *db = sqlite_pool_acquire(pool, 5000);
sqlite3_exec(db, sql, NULL, NULL, NULL);
sqlite_pool_release(pool, db);

// ✅ Gestione errori completa
sqlite3 *db = sqlite_pool_acquire(pool, 5000);
if (!db) {
    log_error("Failed to acquire connection");
    return;
}

char *err = NULL;
int rc = sqlite3_exec(db, sql, NULL, NULL, &err);

if (rc != SQLITE_OK) {
    log_error("SQL error: %s", err);
    sqlite3_free(err);
}

sqlite_pool_release(pool, db);
```

### 3. Usa Transazioni per Batch

```cpp
// ❌ Inserimenti singoli (lento)
for (int i = 0; i < 1000; i++) {
    sqlite3_exec(db, "INSERT ...", NULL, NULL, NULL);
}

// ✅ Transazione batch (veloce)
sqlite_async_begin_transaction(db, NULL, NULL);

for (int i = 0; i < 1000; i++) {
    sqlite_async_exec(db, "INSERT ...", NULL, NULL, SQLITE_PRIORITY_NORMAL);
}

sqlite_async_commit_transaction(db, NULL, NULL);
```

### 4. Monitora le Risorse

```cpp
void monitor_task(void *param) {
    while (true) {
        // Pool stats
        sqlite_pool_stats_t pool_stats;
        sqlite_pool_get_stats(pool, &pool_stats);

        // Async stats
        sqlite_async_stats_t async_stats;
        sqlite_async_get_stats(&async_stats);

        // System stats
        UBaseType_t free_stack = uxTaskGetStackHighWaterMark(NULL);
        uint32_t free_heap = xPortGetFreeHeapSize();

        Serial.printf("Pool: %d active, %d idle\n",
                     pool_stats.active_connections,
                     pool_stats.idle_connections);

        Serial.printf("Async: %d pending, %d completed\n",
                     async_stats.pending_requests,
                     async_stats.completed_requests);

        Serial.printf("System: %d stack, %d heap\n",
                     free_stack, free_heap);

        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
```

### 5. Cleanup su Shutdown

```cpp
void app_shutdown() {
    // 1. Ferma tutti i task che usano DB
    vTaskDelete(sensor_task_handle);
    vTaskDelete(logger_task_handle);

    // 2. Aspetta completamento operazioni async
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 3. Shutdown async system
    sqlite_async_shutdown();

    // 4. Distruggi pool
    sqlite_pool_destroy(pool);

    // 5. Shutdown SQLite
    sqlite_micro_shutdown();
}
```

## Troubleshooting

### "Queue full" Error

**Sintomo**: `sqlite_async_exec` ritorna 0

**Causa**: Queue di richieste piena

**Soluzione**:
```cpp
sqlite_async_config_t config = SQLITE_ASYNC_DEFAULT_CONFIG();
config.queue_size = 128;  // Aumenta da 32 a 128
sqlite_async_init(&config);
```

### Timeout Acquiring Connection

**Sintomo**: `sqlite_pool_acquire` ritorna NULL

**Cause possibili**:
1. Troppe connessioni attive
2. Connessioni non rilasciate
3. Timeout troppo breve

**Soluzioni**:
```cpp
// 1. Aumenta max connections
pool_cfg.max_connections = 8;  // Invece di 4

// 2. Verifica sempre release
// Usa RAII pattern (se C++):
class PoolConnection {
    sqlite_pool_t *pool;
    sqlite3 *db;
public:
    PoolConnection(sqlite_pool_t *p, uint32_t timeout)
        : pool(p), db(sqlite_pool_acquire(p, timeout)) {}

    ~PoolConnection() {
        if (db) sqlite_pool_release(pool, db);
    }

    sqlite3* get() { return db; }
};

// Uso:
{
    PoolConnection conn(pool, 5000);
    if (conn.get()) {
        // Usa conn.get()
    }
}  // Rilascio automatico!

// 3. Aumenta timeout
sqlite3 *db = sqlite_pool_acquire(pool, 10000);  // 10s
```

### Stack Overflow

**Sintomo**: Task crash, reboot random

**Causa**: Stack task insufficiente per operazioni DB

**Soluzione**:
```cpp
// Aumenta stack size
xTaskCreate(db_task, "DB", 12288, NULL, 5, NULL);  // 12KB invece di 4KB

// Monitora stack usage
UBaseType_t high_water = uxTaskGetStackHighWaterMark(NULL);
Serial.printf("Stack remaining: %d bytes\n", high_water * 4);
```

### Memory Leak

**Sintomo**: Memoria disponibile diminuisce nel tempo

**Cause**:
1. Connessioni non rilasciate
2. Result non liberati
3. Error message non liberati

**Soluzioni**:
```cpp
// 1. Verifica release connessioni
// Aggiungi assertion:
void database_op() {
    uint32_t before = pool->stats.active_connections;

    sqlite3 *db = sqlite_pool_acquire(pool, 5000);
    // ... operazioni ...
    sqlite_pool_release(pool, db);

    uint32_t after = pool->stats.active_connections;
    assert(before == after);  // Deve essere uguale!
}

// 2. Libera error messages
char *err = NULL;
sqlite3_exec(db, sql, NULL, NULL, &err);
if (err) {
    log_error("%s", err);
    sqlite3_free(err);  // IMPORTANTE!
}

// 3. Monitora heap
void monitor_heap() {
    uint32_t free_heap = xPortGetFreeHeapSize();
    uint32_t min_free = xPortGetMinimumEverFreeHeapSize();

    Serial.printf("Heap: %d free, %d minimum\n", free_heap, min_free);

    if (free_heap < 10000) {
        log_error("Low memory warning!");
    }
}
```

---

**Prossimi passi**: Leggi [API.md](API.md) per reference completa delle API.
