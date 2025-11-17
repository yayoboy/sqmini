#include "sqlite_micro/sqlite_async.h"
#include <string.h>
#include <stdlib.h>

#ifdef SQLITE_HAS_FREERTOS

#include <esp_log.h>
#include <esp_timer.h>

#define LOG_TAG "SQLiteAsync"

/* Internal context structure */
struct sqlite_async_context_s {
    TaskHandle_t worker_task;
    QueueHandle_t request_queue;
    SemaphoreHandle_t mutex;
    EventGroupHandle_t event_group;
    sqlite_async_config_t config;
    sqlite_async_stats_t stats;
    uint32_t next_request_id;
    bool is_running;
};

static sqlite_async_context_t *async_ctx = NULL;

/* Event group bits */
#define ASYNC_EVENT_STOP (1 << 0)

/* Request result pool */
#define MAX_PENDING_RESULTS 64
static sqlite_async_result_t result_pool[MAX_PENDING_RESULTS];
static SemaphoreHandle_t result_pool_mutex = NULL;

/* Forward declarations */
static void sqlite_async_worker_task(void *param);
static void process_async_request(sqlite_async_request_t *req);
static sqlite_async_result_t* allocate_result(void);
static void free_result(sqlite_async_result_t *result);
static uint32_t get_current_time_ms(void);

int sqlite_async_init(sqlite_async_config_t *config) {
    if (async_ctx != NULL) {
        ESP_LOGW(LOG_TAG, "Async system already initialized");
        return SQLITE_OK;
    }

    async_ctx = (sqlite_async_context_t*)calloc(1, sizeof(sqlite_async_context_t));
    if (!async_ctx) {
        ESP_LOGE(LOG_TAG, "Failed to allocate async context");
        return SQLITE_NOMEM;
    }

    /* Use default config if not provided */
    if (config) {
        memcpy(&async_ctx->config, config, sizeof(sqlite_async_config_t));
    } else {
        sqlite_async_config_t default_cfg = SQLITE_ASYNC_DEFAULT_CONFIG();
        memcpy(&async_ctx->config, &default_cfg, sizeof(sqlite_async_config_t));
    }

    /* Create synchronization objects */
    async_ctx->mutex = xSemaphoreCreateMutex();
    if (!async_ctx->mutex) {
        ESP_LOGE(LOG_TAG, "Failed to create mutex");
        goto error;
    }

    async_ctx->event_group = xEventGroupCreate();
    if (!async_ctx->event_group) {
        ESP_LOGE(LOG_TAG, "Failed to create event group");
        goto error;
    }

    /* Create request queue */
    async_ctx->request_queue = xQueueCreate(async_ctx->config.queue_size,
                                            sizeof(sqlite_async_request_t*));
    if (!async_ctx->request_queue) {
        ESP_LOGE(LOG_TAG, "Failed to create request queue");
        goto error;
    }

    /* Create result pool mutex */
    if (!result_pool_mutex) {
        result_pool_mutex = xSemaphoreCreateMutex();
        if (!result_pool_mutex) {
            ESP_LOGE(LOG_TAG, "Failed to create result pool mutex");
            goto error;
        }
    }

    /* Initialize result pool */
    memset(result_pool, 0, sizeof(result_pool));

    /* Start worker task */
    async_ctx->is_running = true;
    async_ctx->next_request_id = 1;

    BaseType_t ret = xTaskCreate(
        sqlite_async_worker_task,
        async_ctx->config.task_name,
        async_ctx->config.task_stack_size,
        NULL,
        async_ctx->config.task_priority,
        &async_ctx->worker_task
    );

    if (ret != pdPASS) {
        ESP_LOGE(LOG_TAG, "Failed to create worker task");
        goto error;
    }

    ESP_LOGI(LOG_TAG, "Async system initialized successfully");
    return SQLITE_OK;

error:
    if (async_ctx->mutex) vSemaphoreDelete(async_ctx->mutex);
    if (async_ctx->event_group) vEventGroupDelete(async_ctx->event_group);
    if (async_ctx->request_queue) vQueueDelete(async_ctx->request_queue);
    free(async_ctx);
    async_ctx = NULL;
    return SQLITE_ERROR;
}

void sqlite_async_shutdown(void) {
    if (!async_ctx) {
        return;
    }

    /* Signal worker task to stop */
    async_ctx->is_running = false;
    xEventGroupSetBits(async_ctx->event_group, ASYNC_EVENT_STOP);

    /* Wait for task to finish (max 5 seconds) */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* Delete task if still running */
    if (async_ctx->worker_task) {
        vTaskDelete(async_ctx->worker_task);
    }

    /* Clean up */
    if (async_ctx->mutex) vSemaphoreDelete(async_ctx->mutex);
    if (async_ctx->event_group) vEventGroupDelete(async_ctx->event_group);
    if (async_ctx->request_queue) vQueueDelete(async_ctx->request_queue);

    free(async_ctx);
    async_ctx = NULL;

    ESP_LOGI(LOG_TAG, "Async system shutdown complete");
}

uint32_t sqlite_async_exec(sqlite3 *db, const char *sql,
                           sqlite_async_callback_t callback,
                           void *user_data,
                           sqlite_async_priority_t priority) {
    if (!async_ctx || !db || !sql) {
        return 0;
    }

    sqlite_async_request_t *req = (sqlite_async_request_t*)calloc(1, sizeof(sqlite_async_request_t));
    if (!req) {
        ESP_LOGE(LOG_TAG, "Failed to allocate request");
        return 0;
    }

    /* Acquire mutex for ID generation */
    xSemaphoreTake(async_ctx->mutex, portMAX_DELAY);
    req->id = async_ctx->next_request_id++;
    async_ctx->stats.total_requests++;
    async_ctx->stats.pending_requests++;
    xSemaphoreGive(async_ctx->mutex);

    req->op_type = SQLITE_ASYNC_OP_EXEC;
    req->priority = priority;
    req->sql = strdup(sql);
    req->db = db;
    req->callback = callback;
    req->user_data = user_data;
    req->enqueue_time = get_current_time_ms();
    req->timeout_ms = 30000; /* 30 second default timeout */

    /* Allocate result */
    req->result = allocate_result();
    if (req->result) {
        req->result->request_id = req->id;
        req->result->status = SQLITE_ASYNC_PENDING;
        req->result->user_data = user_data;
    }

    /* Queue the request */
    if (xQueueSend(async_ctx->request_queue, &req, pdMS_TO_TICKS(1000)) != pdPASS) {
        ESP_LOGE(LOG_TAG, "Failed to queue request");
        free(req->sql);
        free_result(req->result);
        free(req);
        return 0;
    }

    return req->id;
}

uint32_t sqlite_async_query(sqlite3 *db, const char *sql,
                            sqlite_row_callback_t row_callback,
                            sqlite_async_callback_t completion_callback,
                            void *user_data,
                            sqlite_async_priority_t priority) {
    if (!async_ctx || !db || !sql) {
        return 0;
    }

    sqlite_async_request_t *req = (sqlite_async_request_t*)calloc(1, sizeof(sqlite_async_request_t));
    if (!req) {
        return 0;
    }

    xSemaphoreTake(async_ctx->mutex, portMAX_DELAY);
    req->id = async_ctx->next_request_id++;
    async_ctx->stats.total_requests++;
    async_ctx->stats.pending_requests++;
    xSemaphoreGive(async_ctx->mutex);

    req->op_type = SQLITE_ASYNC_OP_QUERY;
    req->priority = priority;
    req->sql = strdup(sql);
    req->db = db;
    req->callback = completion_callback;
    req->row_callback = row_callback;
    req->user_data = user_data;
    req->enqueue_time = get_current_time_ms();
    req->timeout_ms = 30000;

    req->result = allocate_result();
    if (req->result) {
        req->result->request_id = req->id;
        req->result->status = SQLITE_ASYNC_PENDING;
        req->result->user_data = user_data;
    }

    if (xQueueSend(async_ctx->request_queue, &req, pdMS_TO_TICKS(1000)) != pdPASS) {
        free(req->sql);
        free_result(req->result);
        free(req);
        return 0;
    }

    return req->id;
}

uint32_t sqlite_async_begin_transaction(sqlite3 *db,
                                        sqlite_async_callback_t callback,
                                        void *user_data) {
    return sqlite_async_exec(db, "BEGIN TRANSACTION", callback, user_data, SQLITE_PRIORITY_HIGH);
}

uint32_t sqlite_async_commit_transaction(sqlite3 *db,
                                         sqlite_async_callback_t callback,
                                         void *user_data) {
    return sqlite_async_exec(db, "COMMIT", callback, user_data, SQLITE_PRIORITY_HIGH);
}

uint32_t sqlite_async_rollback_transaction(sqlite3 *db,
                                           sqlite_async_callback_t callback,
                                           void *user_data) {
    return sqlite_async_exec(db, "ROLLBACK", callback, user_data, SQLITE_PRIORITY_CRITICAL);
}

static void sqlite_async_worker_task(void *param) {
    sqlite_async_request_t *req;
    ESP_LOGI(LOG_TAG, "Worker task started");

    while (async_ctx->is_running) {
        /* Wait for request or timeout */
        if (xQueueReceive(async_ctx->request_queue, &req, pdMS_TO_TICKS(100)) == pdPASS) {
            /* Process the request */
            process_async_request(req);

            /* Invoke callback if provided */
            if (req->callback && req->result) {
                req->callback(req->result, req->user_data);
            }

            /* Cleanup request */
            if (req->sql) free(req->sql);

            /* Don't free result immediately - keep for status queries */
            xSemaphoreTake(async_ctx->mutex, portMAX_DELAY);
            async_ctx->stats.pending_requests--;
            xSemaphoreGive(async_ctx->mutex);

            free(req);
        }

        /* Check for stop event */
        EventBits_t bits = xEventGroupGetBits(async_ctx->event_group);
        if (bits & ASYNC_EVENT_STOP) {
            break;
        }
    }

    ESP_LOGI(LOG_TAG, "Worker task stopped");
    vTaskDelete(NULL);
}

static void process_async_request(sqlite_async_request_t *req) {
    if (!req || !req->result) {
        return;
    }

    uint32_t start_time = get_current_time_ms();
    req->result->status = SQLITE_ASYNC_RUNNING;

    switch (req->op_type) {
        case SQLITE_ASYNC_OP_EXEC: {
            char *err_msg = NULL;
            req->result->sqlite_rc = sqlite3_exec(req->db, req->sql, NULL, NULL, &err_msg);

            if (req->result->sqlite_rc == SQLITE_OK) {
                req->result->status = SQLITE_ASYNC_COMPLETED;
                req->result->rows_affected = sqlite3_changes(req->db);

                xSemaphoreTake(async_ctx->mutex, portMAX_DELAY);
                async_ctx->stats.completed_requests++;
                xSemaphoreGive(async_ctx->mutex);
            } else {
                req->result->status = SQLITE_ASYNC_FAILED;
                req->result->error_msg = err_msg ? strdup(err_msg) : NULL;

                xSemaphoreTake(async_ctx->mutex, portMAX_DELAY);
                async_ctx->stats.failed_requests++;
                xSemaphoreGive(async_ctx->mutex);
            }

            if (err_msg) sqlite3_free(err_msg);
            break;
        }

        case SQLITE_ASYNC_OP_QUERY: {
            sqlite3_stmt *stmt = NULL;
            req->result->sqlite_rc = sqlite3_prepare_v2(req->db, req->sql, -1, &stmt, NULL);

            if (req->result->sqlite_rc == SQLITE_OK) {
                int row_count = 0;

                /* Step through results */
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    row_count++;

                    /* Invoke row callback if provided */
                    if (req->row_callback) {
                        req->row_callback(stmt, req->user_data);
                    }
                }

                sqlite3_finalize(stmt);
                req->result->status = SQLITE_ASYNC_COMPLETED;
                req->result->rows_affected = row_count;

                xSemaphoreTake(async_ctx->mutex, portMAX_DELAY);
                async_ctx->stats.completed_requests++;
                xSemaphoreGive(async_ctx->mutex);
            } else {
                req->result->status = SQLITE_ASYNC_FAILED;
                req->result->error_msg = strdup(sqlite3_errmsg(req->db));

                xSemaphoreTake(async_ctx->mutex, portMAX_DELAY);
                async_ctx->stats.failed_requests++;
                xSemaphoreGive(async_ctx->mutex);
            }
            break;
        }

        default:
            req->result->status = SQLITE_ASYNC_FAILED;
            req->result->error_msg = strdup("Unsupported operation type");
            break;
    }

    req->result->execution_time_ms = get_current_time_ms() - start_time;

    /* Update statistics */
    xSemaphoreTake(async_ctx->mutex, portMAX_DELAY);
    if (req->result->execution_time_ms > async_ctx->stats.max_execution_time_ms) {
        async_ctx->stats.max_execution_time_ms = req->result->execution_time_ms;
    }
    xSemaphoreGive(async_ctx->mutex);
}

int sqlite_async_get_stats(sqlite_async_stats_t *stats) {
    if (!async_ctx || !stats) {
        return SQLITE_ERROR;
    }

    xSemaphoreTake(async_ctx->mutex, portMAX_DELAY);
    memcpy(stats, &async_ctx->stats, sizeof(sqlite_async_stats_t));
    xSemaphoreGive(async_ctx->mutex);

    return SQLITE_OK;
}

void sqlite_async_reset_stats(void) {
    if (!async_ctx) {
        return;
    }

    xSemaphoreTake(async_ctx->mutex, portMAX_DELAY);
    memset(&async_ctx->stats, 0, sizeof(sqlite_async_stats_t));
    xSemaphoreGive(async_ctx->mutex);
}

static sqlite_async_result_t* allocate_result(void) {
    sqlite_async_result_t *result = NULL;

    if (!result_pool_mutex) {
        return NULL;
    }

    xSemaphoreTake(result_pool_mutex, portMAX_DELAY);

    for (int i = 0; i < MAX_PENDING_RESULTS; i++) {
        if (result_pool[i].status == 0) {  /* Free slot */
            result = &result_pool[i];
            memset(result, 0, sizeof(sqlite_async_result_t));
            result->status = SQLITE_ASYNC_PENDING;
            break;
        }
    }

    xSemaphoreGive(result_pool_mutex);
    return result;
}

static void free_result(sqlite_async_result_t *result) {
    if (!result || !result_pool_mutex) {
        return;
    }

    xSemaphoreTake(result_pool_mutex, portMAX_DELAY);

    if (result->error_msg) {
        free(result->error_msg);
    }

    memset(result, 0, sizeof(sqlite_async_result_t));

    xSemaphoreGive(result_pool_mutex);
}

static uint32_t get_current_time_ms(void) {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

int sqlite_async_exec_wait(sqlite3 *db, const char *sql,
                           uint32_t timeout_ms, char **error_msg) {
    /* Simplified synchronous wrapper */
    char *err = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err);

    if (error_msg) {
        *error_msg = err;
    } else if (err) {
        sqlite3_free(err);
    }

    return rc;
}

#endif /* SQLITE_HAS_FREERTOS */
