#ifndef SQLITE_BACKUP_H
#define SQLITE_BACKUP_H

#include "sqlite3.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Backup types */
typedef enum {
    SQLITE_BACKUP_FULL,      /* Full database backup */
    SQLITE_BACKUP_INCR,      /* Incremental backup */
    SQLITE_BACKUP_DIFF       /* Differential backup */
} sqlite_backup_type_t;

/* Backup configuration */
typedef struct {
    const char *backup_path;
    sqlite_backup_type_t type;
    uint32_t page_size;
    bool compress;
    bool encrypt;
    uint32_t max_backups;    /* Keep last N backups */
} sqlite_backup_config_t;

/* Backup status */
typedef enum {
    SQLITE_BACKUP_STATUS_IDLE,
    SQLITE_BACKUP_STATUS_RUNNING,
    SQLITE_BACKUP_STATUS_COMPLETED,
    SQLITE_BACKUP_STATUS_FAILED
} sqlite_backup_status_t;

/* Backup progress info */
typedef struct {
    sqlite_backup_status_t status;
    uint32_t total_pages;
    uint32_t pages_copied;
    uint32_t percent_complete;
    uint32_t bytes_copied;
    uint32_t elapsed_time_ms;
} sqlite_backup_progress_t;

/* Backup schedule */
typedef enum {
    SQLITE_BACKUP_SCHED_MANUAL,
    SQLITE_BACKUP_SCHED_HOURLY,
    SQLITE_BACKUP_SCHED_DAILY,
    SQLITE_BACKUP_SCHED_WEEKLY
} sqlite_backup_schedule_t;

/* Auto-backup configuration */
typedef struct {
    sqlite_backup_schedule_t schedule;
    uint32_t retention_hours;
    bool backup_on_close;
    const char *backup_directory;
} sqlite_auto_backup_config_t;

/* Progress callback */
typedef void (*sqlite_backup_progress_callback_t)(sqlite_backup_progress_t *progress, void *user_data);

/* Initialize backup system */
int sqlite_backup_init(void);

/* Shutdown backup system */
void sqlite_backup_shutdown(void);

/* Perform backup (blocking) */
int sqlite_backup_database(sqlite3 *source_db,
                           const char *dest_path,
                           sqlite_backup_config_t *config);

/* Perform async backup */
uint32_t sqlite_backup_async(sqlite3 *source_db,
                             const char *dest_path,
                             sqlite_backup_config_t *config,
                             sqlite_backup_progress_callback_t callback,
                             void *user_data);

/* Restore database from backup */
int sqlite_backup_restore(const char *backup_path,
                         sqlite3 *dest_db);

/* Configure auto-backup */
int sqlite_backup_configure_auto(sqlite3 *db, sqlite_auto_backup_config_t *config);

/* Start auto-backup */
int sqlite_backup_auto_start(sqlite3 *db);

/* Stop auto-backup */
void sqlite_backup_auto_stop(sqlite3 *db);

/* List available backups */
int sqlite_backup_list(const char *directory, char **backup_list, uint32_t *count);

/* Delete old backups based on retention policy */
int sqlite_backup_cleanup(const char *directory, uint32_t retention_hours);

/* Verify backup integrity */
int sqlite_backup_verify(const char *backup_path);

/* Get backup info */
typedef struct {
    char path[256];
    uint64_t size_bytes;
    uint32_t timestamp;
    sqlite_backup_type_t type;
    bool compressed;
    bool encrypted;
} sqlite_backup_info_t;

int sqlite_backup_get_info(const char *backup_path, sqlite_backup_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* SQLITE_BACKUP_H */
