#ifndef SQLITE_MONITOR_H
#define SQLITE_MONITOR_H

#include "sqlite3.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Watchdog configuration */
typedef struct {
    uint32_t timeout_ms;          /* Watchdog timeout */
    bool auto_reset;              /* Auto-reset on timeout */
    bool panic_on_timeout;        /* Panic on watchdog timeout */
} sqlite_watchdog_config_t;

/* Monitoring levels */
typedef enum {
    SQLITE_MONITOR_LEVEL_NONE = 0,
    SQLITE_MONITOR_LEVEL_ERROR = 1,
    SQLITE_MONITOR_LEVEL_WARN = 2,
    SQLITE_MONITOR_LEVEL_INFO = 3,
    SQLITE_MONITOR_LEVEL_DEBUG = 4,
    SQLITE_MONITOR_LEVEL_VERBOSE = 5
} sqlite_monitor_level_t;

/* Performance metrics */
typedef struct {
    uint32_t query_count;
    uint32_t slow_query_count;
    uint32_t error_count;
    uint32_t total_execution_time_ms;
    uint32_t avg_execution_time_ms;
    uint32_t max_execution_time_ms;
    uint32_t min_execution_time_ms;
    uint32_t cache_hit_rate_percent;
    uint32_t connections_active;
    uint32_t memory_used_bytes;
    uint32_t memory_peak_bytes;
} sqlite_perf_metrics_t;

/* Health status */
typedef enum {
    SQLITE_HEALTH_GOOD,
    SQLITE_HEALTH_WARNING,
    SQLITE_HEALTH_CRITICAL,
    SQLITE_HEALTH_UNKNOWN
} sqlite_health_status_t;

/* Health report */
typedef struct {
    sqlite_health_status_t status;
    uint32_t error_count_last_hour;
    uint32_t slow_queries_last_hour;
    float avg_cpu_usage_percent;
    float memory_usage_percent;
    bool disk_space_warning;
    char status_message[128];
} sqlite_health_report_t;

/* Query profiling data */
typedef struct {
    char sql[256];
    uint32_t execution_time_ms;
    uint32_t rows_affected;
    uint32_t timestamp;
} sqlite_query_profile_t;

/* Callback for slow query alerts */
typedef void (*sqlite_slow_query_callback_t)(const char *sql, uint32_t execution_time_ms);

/* Callback for error alerts */
typedef void (*sqlite_error_callback_t)(int error_code, const char *error_msg);

/* Initialize monitoring system */
int sqlite_monitor_init(sqlite_monitor_level_t level);

/* Shutdown monitoring */
void sqlite_monitor_shutdown(void);

/* Set monitoring level */
void sqlite_monitor_set_level(sqlite_monitor_level_t level);

/* Watchdog functions */
int sqlite_watchdog_init(sqlite_watchdog_config_t *config);
void sqlite_watchdog_feed(void);
void sqlite_watchdog_stop(void);

/* Performance monitoring */
int sqlite_monitor_get_metrics(sqlite_perf_metrics_t *metrics);
void sqlite_monitor_reset_metrics(void);

/* Health check */
int sqlite_monitor_health_check(sqlite_health_report_t *report);

/* Slow query detection */
void sqlite_monitor_set_slow_query_threshold(uint32_t threshold_ms);
void sqlite_monitor_set_slow_query_callback(sqlite_slow_query_callback_t callback);

/* Error monitoring */
void sqlite_monitor_set_error_callback(sqlite_error_callback_t callback);
void sqlite_monitor_log_error(int error_code, const char *error_msg);

/* Query profiling */
void sqlite_monitor_profile_start(const char *sql);
void sqlite_monitor_profile_end(const char *sql, uint32_t execution_time_ms, int rows_affected);
int sqlite_monitor_get_slow_queries(sqlite_query_profile_t *profiles, uint32_t max_count);

/* Resource monitoring */
uint32_t sqlite_monitor_get_memory_usage(void);
uint32_t sqlite_monitor_get_disk_usage(void);
float sqlite_monitor_get_cpu_usage(void);

/* Log functions */
void sqlite_monitor_log(sqlite_monitor_level_t level, const char *tag, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif /* SQLITE_MONITOR_H */
