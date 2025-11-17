#ifndef SQLITE_POOL_H
#define SQLITE_POOL_H

#include "sqlite3.h"
#include "sqlite_micro.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Connection pool configuration */
typedef struct {
    const char *db_path;
    sqlite_vfs_config_t *vfs_config;
    uint32_t min_connections;
    uint32_t max_connections;
    uint32_t connection_timeout_ms;
    uint32_t idle_timeout_ms;
    bool enable_wal;
    bool enable_shared_cache;
} sqlite_pool_config_t;

/* Default pool configuration */
#define SQLITE_POOL_DEFAULT_CONFIG(path) { \
    .db_path = path, \
    .vfs_config = NULL, \
    .min_connections = 1, \
    .max_connections = 4, \
    .connection_timeout_ms = 5000, \
    .idle_timeout_ms = 60000, \
    .enable_wal = true, \
    .enable_shared_cache = false \
}

/* Pool statistics */
typedef struct {
    uint32_t total_connections;
    uint32_t active_connections;
    uint32_t idle_connections;
    uint32_t total_acquires;
    uint32_t total_releases;
    uint32_t acquire_timeouts;
    uint32_t peak_connections;
} sqlite_pool_stats_t;

/* Connection pool handle */
typedef struct sqlite_pool_s sqlite_pool_t;

/* Initialize connection pool */
sqlite_pool_t* sqlite_pool_create(sqlite_pool_config_t *config);

/* Destroy connection pool */
void sqlite_pool_destroy(sqlite_pool_t *pool);

/* Acquire connection from pool (blocking with timeout) */
sqlite3* sqlite_pool_acquire(sqlite_pool_t *pool, uint32_t timeout_ms);

/* Release connection back to pool */
int sqlite_pool_release(sqlite_pool_t *pool, sqlite3 *db);

/* Get pool statistics */
int sqlite_pool_get_stats(sqlite_pool_t *pool, sqlite_pool_stats_t *stats);

/* Shrink pool (remove idle connections) */
int sqlite_pool_shrink(sqlite_pool_t *pool);

/* Validate all connections in pool */
int sqlite_pool_validate(sqlite_pool_t *pool);

#ifdef __cplusplus
}
#endif

#endif /* SQLITE_POOL_H */
