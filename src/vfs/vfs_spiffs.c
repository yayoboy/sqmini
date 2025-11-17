#ifdef SQLITE_PLATFORM_ESP32

#include "sqlite3.h"
#include "sqlite_micro/sqlite_micro.h"
#include <esp_spiffs.h>
#include <esp_log.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#define LOG_TAG "VFS_SPIFFS"

/* SPIFFS VFS File structure */
typedef struct {
    sqlite3_file base;  /* Base class - must be first */
    int fd;            /* File descriptor */
    char *path;        /* File path */
} spiffs_file_t;

/* SPIFFS VFS structure */
typedef struct {
    sqlite3_vfs base;  /* Base class - must be first */
} spiffs_vfs_t;

static int spiffs_close(sqlite3_file *pFile) {
    spiffs_file_t *p = (spiffs_file_t*)pFile;
    int rc = SQLITE_OK;

    if (p->fd >= 0) {
        if (close(p->fd) != 0) {
            rc = SQLITE_IOERR_CLOSE;
        }
    }

    if (p->path) {
        sqlite3_free(p->path);
    }

    return rc;
}

static int spiffs_read(sqlite3_file *pFile, void *zBuf, int iAmt, sqlite_int64 iOfst) {
    spiffs_file_t *p = (spiffs_file_t*)pFile;
    int nRead;

    if (lseek(p->fd, iOfst, SEEK_SET) < 0) {
        return SQLITE_IOERR_READ;
    }

    nRead = read(p->fd, zBuf, iAmt);
    if (nRead < 0) {
        return SQLITE_IOERR_READ;
    }

    if (nRead < iAmt) {
        /* Unread parts of the buffer must be zero-filled */
        memset(&((char*)zBuf)[nRead], 0, iAmt - nRead);
        return SQLITE_IOERR_SHORT_READ;
    }

    return SQLITE_OK;
}

static int spiffs_write(sqlite3_file *pFile, const void *zBuf, int iAmt, sqlite_int64 iOfst) {
    spiffs_file_t *p = (spiffs_file_t*)pFile;
    int nWrite;

    if (lseek(p->fd, iOfst, SEEK_SET) < 0) {
        return SQLITE_IOERR_WRITE;
    }

    nWrite = write(p->fd, zBuf, iAmt);
    if (nWrite != iAmt) {
        return SQLITE_IOERR_WRITE;
    }

    return SQLITE_OK;
}

static int spiffs_truncate(sqlite3_file *pFile, sqlite_int64 size) {
    spiffs_file_t *p = (spiffs_file_t*)pFile;

    if (ftruncate(p->fd, size) != 0) {
        return SQLITE_IOERR_TRUNCATE;
    }

    return SQLITE_OK;
}

static int spiffs_sync(sqlite3_file *pFile, int flags) {
    spiffs_file_t *p = (spiffs_file_t*)pFile;

    if (fsync(p->fd) != 0) {
        return SQLITE_IOERR_FSYNC;
    }

    return SQLITE_OK;
}

static int spiffs_file_size(sqlite3_file *pFile, sqlite_int64 *pSize) {
    spiffs_file_t *p = (spiffs_file_t*)pFile;
    struct stat st;

    if (fstat(p->fd, &st) != 0) {
        return SQLITE_IOERR_FSTAT;
    }

    *pSize = st.st_size;
    return SQLITE_OK;
}

static int spiffs_lock(sqlite3_file *pFile, int eLock) {
    /* SPIFFS doesn't support locking, so we just return OK */
    return SQLITE_OK;
}

static int spiffs_unlock(sqlite3_file *pFile, int eLock) {
    /* SPIFFS doesn't support locking, so we just return OK */
    return SQLITE_OK;
}

static int spiffs_check_reserved_lock(sqlite3_file *pFile, int *pResOut) {
    *pResOut = 0;
    return SQLITE_OK;
}

static int spiffs_file_control(sqlite3_file *pFile, int op, void *pArg) {
    return SQLITE_NOTFOUND;
}

static int spiffs_sector_size(sqlite3_file *pFile) {
    return 512;  /* SPIFFS sector size */
}

static int spiffs_device_characteristics(sqlite3_file *pFile) {
    return SQLITE_IOCAP_SAFE_APPEND | SQLITE_IOCAP_SEQUENTIAL;
}

static const sqlite3_io_methods spiffs_io_methods = {
    1,                              /* iVersion */
    spiffs_close,                   /* xClose */
    spiffs_read,                    /* xRead */
    spiffs_write,                   /* xWrite */
    spiffs_truncate,                /* xTruncate */
    spiffs_sync,                    /* xSync */
    spiffs_file_size,               /* xFileSize */
    spiffs_lock,                    /* xLock */
    spiffs_unlock,                  /* xUnlock */
    spiffs_check_reserved_lock,     /* xCheckReservedLock */
    spiffs_file_control,            /* xFileControl */
    spiffs_sector_size,             /* xSectorSize */
    spiffs_device_characteristics,  /* xDeviceCharacteristics */
};

static int spiffs_open(sqlite3_vfs *pVfs, const char *zName, sqlite3_file *pFile,
                       int flags, int *pOutFlags) {
    spiffs_file_t *p = (spiffs_file_t*)pFile;
    int oflags = 0;
    int rc = SQLITE_OK;

    memset(p, 0, sizeof(spiffs_file_t));
    p->fd = -1;

    if (zName == NULL) {
        return SQLITE_IOERR;
    }

    /* Convert SQLite flags to POSIX flags */
    if (flags & SQLITE_OPEN_READONLY) {
        oflags = O_RDONLY;
    } else if (flags & SQLITE_OPEN_READWRITE) {
        oflags = O_RDWR;
    } else if (flags & SQLITE_OPEN_CREATE) {
        oflags = O_RDWR | O_CREAT;
    }

    if (flags & SQLITE_OPEN_EXCLUSIVE) {
        oflags |= O_EXCL;
    }

    /* Open the file */
    p->fd = open(zName, oflags, 0666);
    if (p->fd < 0) {
        ESP_LOGE(LOG_TAG, "Failed to open file: %s, errno: %d", zName, errno);
        return SQLITE_CANTOPEN;
    }

    p->path = sqlite3_mprintf("%s", zName);
    if (p->path == NULL) {
        close(p->fd);
        return SQLITE_NOMEM;
    }

    if (pOutFlags) {
        *pOutFlags = flags;
    }

    p->base.pMethods = &spiffs_io_methods;
    return SQLITE_OK;
}

static int spiffs_delete(sqlite3_vfs *pVfs, const char *zPath, int dirSync) {
    if (unlink(zPath) != 0) {
        return SQLITE_IOERR_DELETE;
    }
    return SQLITE_OK;
}

static int spiffs_access(sqlite3_vfs *pVfs, const char *zPath, int flags, int *pResOut) {
    int rc = access(zPath, F_OK);
    *pResOut = (rc == 0) ? 1 : 0;
    return SQLITE_OK;
}

static int spiffs_full_pathname(sqlite3_vfs *pVfs, const char *zPath, int nOut, char *zOut) {
    snprintf(zOut, nOut, "%s", zPath);
    return SQLITE_OK;
}

static void *spiffs_dlopen(sqlite3_vfs *pVfs, const char *zPath) {
    return NULL;
}

static void spiffs_dlerror(sqlite3_vfs *pVfs, int nByte, char *zErrMsg) {
    snprintf(zErrMsg, nByte, "Dynamic loading not supported");
}

static void (*spiffs_dlsym(sqlite3_vfs *pVfs, void *pH, const char *zSym))(void) {
    return NULL;
}

static void spiffs_dlclose(sqlite3_vfs *pVfs, void *pHandle) {
}

static int spiffs_randomness(sqlite3_vfs *pVfs, int nByte, char *zOut) {
    for (int i = 0; i < nByte; i++) {
        zOut[i] = esp_random() & 0xFF;
    }
    return nByte;
}

static int spiffs_sleep(sqlite3_vfs *pVfs, int microseconds) {
    usleep(microseconds);
    return microseconds;
}

static int spiffs_current_time(sqlite3_vfs *pVfs, double *prNow) {
    /* Simple implementation - return a constant if RTC not available */
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *prNow = 2440587.5 + tv.tv_sec / 86400.0;
    return SQLITE_OK;
}

static spiffs_vfs_t spiffs_vfs = {
    {
        1,                      /* iVersion */
        sizeof(spiffs_file_t),  /* szOsFile */
        256,                    /* mxPathname */
        NULL,                   /* pNext */
        "spiffs",               /* zName */
        NULL,                   /* pAppData */
        spiffs_open,            /* xOpen */
        spiffs_delete,          /* xDelete */
        spiffs_access,          /* xAccess */
        spiffs_full_pathname,   /* xFullPathname */
        spiffs_dlopen,          /* xDlOpen */
        spiffs_dlerror,         /* xDlError */
        spiffs_dlsym,           /* xDlSym */
        spiffs_dlclose,         /* xDlClose */
        spiffs_randomness,      /* xRandomness */
        spiffs_sleep,           /* xSleep */
        spiffs_current_time,    /* xCurrentTime */
    }
};

int sqlite_vfs_register_spiffs(const char *name) {
    if (name) {
        spiffs_vfs.base.zName = name;
    }
    return sqlite3_vfs_register(&spiffs_vfs.base, 1);
}

int sqlite_micro_init_spiffs(const char *base_path, size_t max_files) {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = base_path,
        .partition_label = NULL,
        .max_files = max_files,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(LOG_TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        return SQLITE_ERROR;
    }

    size_t total = 0, used = 0;
    ret = esp_spiffs_info(NULL, &total, &used);
    if (ret == ESP_OK) {
        ESP_LOGI(LOG_TAG, "SPIFFS: %d KB total, %d KB used", total / 1024, used / 1024);
    }

    return SQLITE_OK;
}

#endif /* SQLITE_PLATFORM_ESP32 */
