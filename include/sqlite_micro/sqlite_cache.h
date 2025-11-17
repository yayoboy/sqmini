#ifndef SQLITE_CACHE_H
#define SQLITE_CACHE_H

#include "sqlite3.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Prepared statement cache */
typedef struct sqlite_stmt_cache_s sqlite_stmt_cache_t;

/* Cache configuration */
typedef struct {
    uint32_t max_statements;
    uint32_t max_sql_length;
    bool enable_stats;
} sqlite_cache_config_t;

/* Default cache configuration */
#define SQLITE_CACHE_DEFAULT_CONFIG() { \
    .max_statements = 32, \
    .max_sql_length = 1024, \
    .enable_stats = true \
}

/* Cache statistics */
typedef struct {
    uint32_t total_lookups;
    uint32_t cache_hits;
    uint32_t cache_misses;
    uint32_t evictions;
    uint32_t current_size;
    float hit_rate;
} sqlite_cache_stats_t;

/* Create statement cache */
sqlite_stmt_cache_t* sqlite_cache_create(sqlite3 *db, sqlite_cache_config_t *config);

/* Destroy statement cache */
void sqlite_cache_destroy(sqlite_stmt_cache_t *cache);

/* Get or prepare statement (cached) */
sqlite3_stmt* sqlite_cache_get_stmt(sqlite_stmt_cache_t *cache, const char *sql);

/* Release statement back to cache */
int sqlite_cache_release_stmt(sqlite_stmt_cache_t *cache, sqlite3_stmt *stmt);

/* Clear all cached statements */
void sqlite_cache_clear(sqlite_stmt_cache_t *cache);

/* Get cache statistics */
int sqlite_cache_get_stats(sqlite_stmt_cache_t *cache, sqlite_cache_stats_t *stats);

/* Remove least recently used statement */
int sqlite_cache_evict_lru(sqlite_stmt_cache_t *cache);

#ifdef __cplusplus
}
#endif

#endif /* SQLITE_CACHE_H */
