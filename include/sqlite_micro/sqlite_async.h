#ifndef SQLITE_ASYNC_H
#define SQLITE_ASYNC_H

#include "sqlite3.h"
#include "sqlite_micro.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef SQLITE_PLATFORM_ESP32
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#define SQLITE_HAS_FREERTOS 1
#endif

/* Query operation types */
typedef enum {
    SQLITE_ASYNC_OP_EXEC,       /* Execute SQL */
    SQLITE_ASYNC_OP_QUERY,      /* Query with results */
    SQLITE_ASYNC_OP_PREPARE,    /* Prepare statement */
    SQLITE_ASYNC_OP_STEP,       /* Step through results */
    SQLITE_ASYNC_OP_FINALIZE,   /* Finalize statement */
    SQLITE_ASYNC_OP_TRANSACTION,/* Begin/Commit/Rollback */
    SQLITE_ASYNC_OP_BACKUP,     /* Backup database */
    SQLITE_ASYNC_OP_VACUUM,     /* Vacuum database */
    SQLITE_ASYNC_OP_CLOSE       /* Close database */
} sqlite_async_op_type_t;

/* Query priority levels */
typedef enum {
    SQLITE_PRIORITY_LOW = 0,
    SQLITE_PRIORITY_NORMAL = 1,
    SQLITE_PRIORITY_HIGH = 2,
    SQLITE_PRIORITY_CRITICAL = 3
} sqlite_async_priority_t;

/* Async operation status */
typedef enum {
    SQLITE_ASYNC_PENDING,
    SQLITE_ASYNC_RUNNING,
    SQLITE_ASYNC_COMPLETED,
    SQLITE_ASYNC_FAILED,
    SQLITE_ASYNC_TIMEOUT,
    SQLITE_ASYNC_CANCELLED
} sqlite_async_status_t;

/* Forward declarations */
typedef struct sqlite_async_request_s sqlite_async_request_t;
typedef struct sqlite_async_result_s sqlite_async_result_t;
typedef struct sqlite_async_context_s sqlite_async_context_t;

/* Callback function types */
typedef void (*sqlite_async_callback_t)(sqlite_async_result_t *result, void *user_data);
typedef void (*sqlite_row_callback_t)(sqlite3_stmt *stmt, void *user_data);

/* Async result structure */
struct sqlite_async_result_s {
    uint32_t request_id;
    sqlite_async_status_t status;
    int sqlite_rc;
    char *error_msg;
    sqlite3_stmt *stmt;
    int rows_affected;
    uint32_t execution_time_ms;
    void *user_data;
};

/* Async request structure */
struct sqlite_async_request_s {
    uint32_t id;
    sqlite_async_op_type_t op_type;
    sqlite_async_priority_t priority;
    char *sql;
    sqlite3 *db;
    sqlite3_stmt *stmt;
    sqlite_async_callback_t callback;
    sqlite_row_callback_t row_callback;
    void *user_data;
    uint32_t timeout_ms;
    uint32_t enqueue_time;
    sqlite_async_result_t *result;
};

/* Async context configuration */
typedef struct {
    const char *task_name;
    uint32_t task_stack_size;
    UBaseType_t task_priority;
    uint32_t queue_size;
    uint32_t max_concurrent_ops;
    bool enable_stats;
} sqlite_async_config_t;

/* Default configuration */
#define SQLITE_ASYNC_DEFAULT_CONFIG() { \
    .task_name = "sqlite_worker", \
    .task_stack_size = 8192, \
    .task_priority = 5, \
    .queue_size = 32, \
    .max_concurrent_ops = 4, \
    .enable_stats = true \
}

/* Async statistics */
typedef struct {
    uint32_t total_requests;
    uint32_t completed_requests;
    uint32_t failed_requests;
    uint32_t cancelled_requests;
    uint32_t timeout_requests;
    uint32_t pending_requests;
    uint32_t avg_execution_time_ms;
    uint32_t max_execution_time_ms;
    uint32_t queue_high_water_mark;
} sqlite_async_stats_t;

/* Initialize async system */
int sqlite_async_init(sqlite_async_config_t *config);

/* Shutdown async system */
void sqlite_async_shutdown(void);

/* Submit async request */
uint32_t sqlite_async_exec(sqlite3 *db, const char *sql,
                           sqlite_async_callback_t callback,
                           void *user_data,
                           sqlite_async_priority_t priority);

/* Submit async query with row callback */
uint32_t sqlite_async_query(sqlite3 *db, const char *sql,
                            sqlite_row_callback_t row_callback,
                            sqlite_async_callback_t completion_callback,
                            void *user_data,
                            sqlite_async_priority_t priority);

/* Async transaction operations */
uint32_t sqlite_async_begin_transaction(sqlite3 *db,
                                        sqlite_async_callback_t callback,
                                        void *user_data);

uint32_t sqlite_async_commit_transaction(sqlite3 *db,
                                         sqlite_async_callback_t callback,
                                         void *user_data);

uint32_t sqlite_async_rollback_transaction(sqlite3 *db,
                                           sqlite_async_callback_t callback,
                                           void *user_data);

/* Cancel pending request */
int sqlite_async_cancel(uint32_t request_id);

/* Wait for request completion (blocking) */
int sqlite_async_wait(uint32_t request_id, uint32_t timeout_ms);

/* Check request status */
sqlite_async_status_t sqlite_async_get_status(uint32_t request_id);

/* Get async statistics */
int sqlite_async_get_stats(sqlite_async_stats_t *stats);

/* Reset statistics */
void sqlite_async_reset_stats(void);

/* Utility: Execute and wait (semi-blocking) */
int sqlite_async_exec_wait(sqlite3 *db, const char *sql,
                           uint32_t timeout_ms, char **error_msg);

#ifdef __cplusplus
}
#endif

#endif /* SQLITE_ASYNC_H */
