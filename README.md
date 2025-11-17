# SQLite Micro - SQLite per Microcontrollori

Una implementazione ottimizzata di SQLite per microcontrollori **ESP32** e **RP2040** con supporto per diversi tipi di memoria.

## Caratteristiche

- ✅ **Piattaforme Supportate**: ESP32, RP2040 (Raspberry Pi Pico)
- ✅ **Storage Multiplo**:
  - SPIFFS (ESP32)
  - LittleFS (ESP32 e RP2040)
  - SD Card (ESP32 e RP2040)
- ✅ **Ottimizzato per Embedded**: Configurazione ottimizzata per memoria limitata
- ✅ **VFS Personalizzato**: Virtual File System per ogni tipo di storage
- ✅ **Facile da Usare**: API semplice e intuitiva
- ✅ **Esempi Completi**: Esempi per ogni piattaforma e tipo di storage

## Requisiti Hardware

### ESP32
- ESP32 con almeno 4MB di Flash
- (Opzionale) Modulo SD Card per storage esterno

### RP2040
- Raspberry Pi Pico o compatibile
- Almeno 2MB di Flash
- (Opzionale) Modulo SD Card per storage esterno

## Installazione

### PlatformIO

1. Clona il repository:
```bash
git clone https://github.com/yayoboy/sqmini.git
cd sqmini
```

2. Apri il progetto in PlatformIO VSCode

3. Seleziona l'environment appropriato:
   - `esp32-spiffs` - ESP32 con SPIFFS
   - `esp32-littlefs` - ESP32 con LittleFS
   - `esp32-sdcard` - ESP32 con SD Card
   - `pico-littlefs` - RP2040 con LittleFS
   - `pico-sdcard` - RP2040 con SD Card

### Arduino IDE

1. Scarica il repository come ZIP
2. In Arduino IDE: Sketch → Include Library → Add .ZIP Library
3. Includi nel tuo sketch:
```cpp
#include "sqlite_micro/sqlite_micro.h"
```

## Utilizzo

### Esempio Base (ESP32 + SPIFFS)

```cpp
#include <Arduino.h>
#include "sqlite_micro/sqlite_micro.h"

sqlite3 *db;

void setup() {
    Serial.begin(115200);

    // 1. Inizializza SQLite
    sqlite_micro_init();

    // 2. Inizializza SPIFFS
    sqlite_micro_init_spiffs("/spiffs", 5);

    // 3. Registra VFS
    sqlite_micro_register_vfs(SQLITE_STORAGE_SPIFFS, "spiffs");

    // 4. Apri database
    sqlite_vfs_config_t config = {
        .mount_point = "/spiffs",
        .db_path = "/spiffs/test.db",
        .storage_type = SQLITE_STORAGE_SPIFFS,
        .cache_size = 64  // 64KB cache
    };

    sqlite_micro_open("/spiffs/test.db", &db, &config);

    // 5. Crea tabella
    sqlite3_exec(db,
        "CREATE TABLE IF NOT EXISTS sensors ("
        "id INTEGER PRIMARY KEY,"
        "name TEXT,"
        "value REAL);",
        NULL, NULL, NULL);

    // 6. Inserisci dati
    sqlite3_exec(db,
        "INSERT INTO sensors (name, value) VALUES ('Temp', 23.5);",
        NULL, NULL, NULL);

    // 7. Query
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT * FROM sensors", -1, &stmt, NULL);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Serial.printf("ID: %d, Name: %s, Value: %.2f\n",
            sqlite3_column_int(stmt, 0),
            sqlite3_column_text(stmt, 1),
            sqlite3_column_double(stmt, 2));
    }

    sqlite3_finalize(stmt);
}

void loop() {
    delay(1000);
}
```

### Esempio ESP32 con LittleFS

```cpp
// Inizializza LittleFS invece di SPIFFS
sqlite_micro_init_littlefs_esp32("/littlefs", 5);
sqlite_micro_register_vfs(SQLITE_STORAGE_LITTLEFS, "littlefs");

sqlite_vfs_config_t config = {
    .mount_point = "/littlefs",
    .db_path = "/littlefs/data.db",
    .storage_type = SQLITE_STORAGE_LITTLEFS,
    .cache_size = 64
};

sqlite_micro_open("/littlefs/data.db", &db, &config);
```

### Esempio ESP32 con SD Card

```cpp
// Definisci i pin SPI per SD Card
#define PIN_SD_CS   5
#define PIN_SD_MOSI 23
#define PIN_SD_MISO 19
#define PIN_SD_SCK  18

// Inizializza SD Card
sqlite_micro_init_sdcard(PIN_SD_CS, PIN_SD_MOSI, PIN_SD_MISO, PIN_SD_SCK);
sqlite_micro_register_vfs(SQLITE_STORAGE_SDCARD, "sdcard");

sqlite_vfs_config_t config = {
    .mount_point = "/sdcard",
    .db_path = "/sdcard/datalog.db",
    .storage_type = SQLITE_STORAGE_SDCARD,
    .cache_size = 128  // SD Card può gestire cache più grandi
};

sqlite_micro_open("/sdcard/datalog.db", &db, &config);
```

### Esempio RP2040 con LittleFS

```cpp
#include <Arduino.h>
#include "sqlite_micro/sqlite_micro.h"

sqlite3 *db;

void setup() {
    Serial.begin(115200);

    // Inizializza per RP2040
    sqlite_micro_init();
    sqlite_micro_init_littlefs_rp2040();
    sqlite_micro_register_vfs(SQLITE_STORAGE_LITTLEFS, "littlefs");

    sqlite_vfs_config_t config = {
        .mount_point = "/lfs",
        .db_path = "/lfs/sensor.db",
        .storage_type = SQLITE_STORAGE_LITTLEFS,
        .cache_size = 32  // RP2040 ha meno RAM
    };

    sqlite_micro_open("/lfs/sensor.db", &db, &config);

    // Usa il database...
}
```

## Configurazione Memoria

SQLite Micro è configurato per minimizzare l'uso di memoria su microcontrollori:

### ESP32 (Default)
- Page size: 512 bytes
- Cache: 64KB (configurabile)
- Max memory: 64KB

### RP2040 (Default)
- Page size: 512 bytes
- Cache: 32KB (configurabile)
- Max memory: 32KB

### Ottimizzazioni Applicate

Il file `sqlite_config.h` disabilita automaticamente features non necessarie:

- ❌ Load extension
- ❌ Virtual tables
- ❌ Views
- ❌ Triggers
- ❌ Subqueries complesse
- ❌ Window functions
- ✅ Operazioni base (SELECT, INSERT, UPDATE, DELETE)
- ✅ JOIN semplici
- ✅ Aggregazioni (SUM, AVG, COUNT, etc.)
- ✅ Indici

## Struttura del Progetto

```
sqmini/
├── src/
│   ├── sqlite3.c              # SQLite amalgamation
│   ├── sqlite3.h
│   ├── sqlite_micro.c         # Implementazione principale
│   ├── vfs/
│   │   ├── vfs_spiffs.c      # VFS per SPIFFS (ESP32)
│   │   ├── vfs_littlefs.c    # VFS per LittleFS
│   │   └── vfs_sdcard.c      # VFS per SD Card
│   └── platform/
│       ├── pico_hal_littlefs.c
│       └── pico_hal_littlefs.h
├── include/
│   └── sqlite_micro/
│       ├── sqlite_micro.h     # Header principale
│       └── sqlite_config.h    # Configurazione
├── examples/
│   ├── esp32/
│   │   ├── spiffs/
│   │   ├── littlefs/
│   │   └── sdcard/
│   └── rp2040/
│       ├── littlefs/
│       └── sdcard/
├── platformio/
│   ├── esp32/platformio.ini
│   └── rp2040/platformio.ini
└── README.md
```

## API Reference

### Funzioni Principali

#### `sqlite_micro_init()`
Inizializza SQLite per microcontrollori.

```cpp
int rc = sqlite_micro_init();
```

#### `sqlite_micro_register_vfs(type, name)`
Registra un Virtual File System per un tipo di storage specifico.

```cpp
sqlite_micro_register_vfs(SQLITE_STORAGE_LITTLEFS, "littlefs");
```

#### `sqlite_micro_open(path, db, config)`
Apre un database con configurazione specifica.

```cpp
sqlite_vfs_config_t config = {
    .mount_point = "/littlefs",
    .db_path = path,
    .storage_type = SQLITE_STORAGE_LITTLEFS,
    .cache_size = 64
};
sqlite_micro_open(path, &db, &config);
```

### Tipi di Storage

```cpp
typedef enum {
    SQLITE_STORAGE_SPIFFS,    // ESP32 SPIFFS
    SQLITE_STORAGE_LITTLEFS,  // LittleFS (ESP32/RP2040)
    SQLITE_STORAGE_SDCARD,    // SD Card
    SQLITE_STORAGE_FLASH      // Flash grezza (futuro)
} sqlite_storage_type_t;
```

### Statistiche Memoria

```cpp
sqlite_micro_stats_t stats;
sqlite_micro_get_stats(&stats);

printf("Memoria corrente: %d bytes\n", stats.current_memory);
printf("Picco memoria: %d bytes\n", stats.peak_memory);
```

## Esempi Forniti

### ESP32
1. **SPIFFS Example** - Database su filesystem SPIFFS
2. **LittleFS Example** - Database su LittleFS con transazioni
3. **SD Card Example** - Data logging su SD Card

### RP2040
1. **LittleFS Example** - Database su flash interna
2. **SD Card Example** - Monitoraggio ambientale con SD Card

Tutti gli esempi si trovano nella cartella `examples/`.

## Performance

### Inserimenti (100 record con transazione)

| Piattaforma | Storage  | Tempo    |
|-------------|----------|----------|
| ESP32       | SPIFFS   | ~150ms   |
| ESP32       | LittleFS | ~120ms   |
| ESP32       | SD Card  | ~200ms   |
| RP2040      | LittleFS | ~180ms   |
| RP2040      | SD Card  | ~250ms   |

*Note: i tempi sono approssimativi e dipendono dalla qualità della SD Card*

## Best Practices

1. **Usa Transazioni**: Per inserimenti multipli, usa sempre `BEGIN/COMMIT`
```cpp
sqlite3_exec(db, "BEGIN TRANSACTION;", NULL, NULL, NULL);
// ... inserimenti ...
sqlite3_exec(db, "COMMIT;", NULL, NULL, NULL);
```

2. **Configura Cache Appropriata**:
   - ESP32: 64-128KB
   - RP2040: 32-64KB

3. **Usa Prepared Statements**: Per query ripetute
```cpp
sqlite3_stmt *stmt;
sqlite3_prepare_v2(db, "SELECT * FROM sensors WHERE id=?", -1, &stmt, NULL);
sqlite3_bind_int(stmt, 1, sensor_id);
sqlite3_step(stmt);
sqlite3_finalize(stmt);
```

4. **Monitora Memoria**: Usa `sqlite_micro_get_stats()` per tenere traccia dell'uso della memoria

5. **SD Card**: Preferisci SD Card per grandi quantità di dati (>1MB)

## Troubleshooting

### "Failed to mount SPIFFS/LittleFS"
- Verifica che la partizione sia configurata correttamente
- Prova a formattare il filesystem

### "Out of memory"
- Riduci la cache size
- Verifica di non avere memory leak
- Usa transazioni per batch insert

### "SD Card not found"
- Verifica il wiring
- Controlla che la SD Card sia formattata FAT32
- Prova a ridurre la velocità SPI

### "Database is locked"
- SQLite Micro usa `SQLITE_THREADSAFE=1` (serialized)
- Evita accessi concorrenti da task diversi

## Limitazioni

- No supporto per estensioni loadabili
- No virtual tables
- Subquery limitate
- No window functions
- Single-threaded access (serialized mode)

## Contribuire

Contributi sono benvenuti! Per favore:

1. Fai fork del repository
2. Crea un branch per la tua feature
3. Commit le modifiche
4. Push al branch
5. Apri una Pull Request

## Licenza

Questo progetto usa SQLite (Public Domain) e codice custom sotto licenza MIT.

## Crediti

- SQLite: https://www.sqlite.org/
- ESP32 Arduino Core: https://github.com/espressif/arduino-esp32
- RP2040 Arduino Core: https://github.com/earlephilhower/arduino-pico

## Supporto

Per problemi o domande:
- Apri una Issue su GitHub
- Consulta gli esempi nella cartella `examples/`
- Leggi la documentazione ufficiale di SQLite: https://www.sqlite.org/docs.html

---

**Autore**: SQLite Micro Contributors
**Versione**: 1.0.0
**Ultimo aggiornamento**: 2024
