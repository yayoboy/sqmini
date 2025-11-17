#include "sqlite3.h"
#include "sqlite_micro/sqlite_micro.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#ifdef SQLITE_PLATFORM_ESP32
#include <esp_littlefs.h>
#include <esp_log.h>
#define LOG_TAG "VFS_LFS"
#define LOG_INFO(fmt, ...) ESP_LOGI(LOG_TAG, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) ESP_LOGE(LOG_TAG, fmt, ##__VA_ARGS__)
#elif defined(SQLITE_PLATFORM_RP2040)
#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#define LOG_INFO(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#endif

/* LittleFS VFS File structure */
typedef struct {
    sqlite3_file base;  /* Base class - must be first */
    int fd;            /* File descriptor */
    char *path;        /* File path */
} littlefs_file_t;

/* LittleFS VFS structure */
typedef struct {
    sqlite3_vfs base;  /* Base class - must be first */
} littlefs_vfs_t;

static int littlefs_close(sqlite3_file *pFile) {
    littlefs_file_t *p = (littlefs_file_t*)pFile;
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

static int littlefs_read(sqlite3_file *pFile, void *zBuf, int iAmt, sqlite_int64 iOfst) {
    littlefs_file_t *p = (littlefs_file_t*)pFile;
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

static int littlefs_write(sqlite3_file *pFile, const void *zBuf, int iAmt, sqlite_int64 iOfst) {
    littlefs_file_t *p = (littlefs_file_t*)pFile;
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

static int littlefs_truncate(sqlite3_file *pFile, sqlite_int64 size) {
    littlefs_file_t *p = (littlefs_file_t*)pFile;

    if (ftruncate(p->fd, size) != 0) {
        return SQLITE_IOERR_TRUNCATE;
    }

    return SQLITE_OK;
}

static int littlefs_sync(sqlite3_file *pFile, int flags) {
    littlefs_file_t *p = (littlefs_file_t*)pFile;

    if (fsync(p->fd) != 0) {
        return SQLITE_IOERR_FSYNC;
    }

    return SQLITE_OK;
}

static int littlefs_file_size(sqlite3_file *pFile, sqlite_int64 *pSize) {
    littlefs_file_t *p = (littlefs_file_t*)pFile;
    struct stat st;

    if (fstat(p->fd, &st) != 0) {
        return SQLITE_IOERR_FSTAT;
    }

    *pSize = st.st_size;
    return SQLITE_OK;
}

static int littlefs_lock(sqlite3_file *pFile, int eLock) {
    /* LittleFS doesn't support locking, so we just return OK */
    return SQLITE_OK;
}

static int littlefs_unlock(sqlite3_file *pFile, int eLock) {
    /* LittleFS doesn't support locking, so we just return OK */
    return SQLITE_OK;
}

static int littlefs_check_reserved_lock(sqlite3_file *pFile, int *pResOut) {
    *pResOut = 0;
    return SQLITE_OK;
}

static int littlefs_file_control(sqlite3_file *pFile, int op, void *pArg) {
    return SQLITE_NOTFOUND;
}

static int littlefs_sector_size(sqlite3_file *pFile) {
    return 4096;  /* LittleFS typical block size */
}

static int littlefs_device_characteristics(sqlite3_file *pFile) {
    return SQLITE_IOCAP_SAFE_APPEND | SQLITE_IOCAP_SEQUENTIAL;
}

static const sqlite3_io_methods littlefs_io_methods = {
    1,                                  /* iVersion */
    littlefs_close,                     /* xClose */
    littlefs_read,                      /* xRead */
    littlefs_write,                     /* xWrite */
    littlefs_truncate,                  /* xTruncate */
    littlefs_sync,                      /* xSync */
    littlefs_file_size,                 /* xFileSize */
    littlefs_lock,                      /* xLock */
    littlefs_unlock,                    /* xUnlock */
    littlefs_check_reserved_lock,       /* xCheckReservedLock */
    littlefs_file_control,              /* xFileControl */
    littlefs_sector_size,               /* xSectorSize */
    littlefs_device_characteristics,    /* xDeviceCharacteristics */
};

static int littlefs_open(sqlite3_vfs *pVfs, const char *zName, sqlite3_file *pFile,
                        int flags, int *pOutFlags) {
    littlefs_file_t *p = (littlefs_file_t*)pFile;
    int oflags = 0;

    memset(p, 0, sizeof(littlefs_file_t));
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
        LOG_ERROR("Failed to open file: %s, errno: %d", zName, errno);
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

    p->base.pMethods = &littlefs_io_methods;
    return SQLITE_OK;
}

static int littlefs_delete(sqlite3_vfs *pVfs, const char *zPath, int dirSync) {
    if (unlink(zPath) != 0) {
        return SQLITE_IOERR_DELETE;
    }
    return SQLITE_OK;
}

static int littlefs_access(sqlite3_vfs *pVfs, const char *zPath, int flags, int *pResOut) {
    int rc = access(zPath, F_OK);
    *pResOut = (rc == 0) ? 1 : 0;
    return SQLITE_OK;
}

static int littlefs_full_pathname(sqlite3_vfs *pVfs, const char *zPath, int nOut, char *zOut) {
    snprintf(zOut, nOut, "%s", zPath);
    return SQLITE_OK;
}

static void *littlefs_dlopen(sqlite3_vfs *pVfs, const char *zPath) {
    return NULL;
}

static void littlefs_dlerror(sqlite3_vfs *pVfs, int nByte, char *zErrMsg) {
    snprintf(zErrMsg, nByte, "Dynamic loading not supported");
}

static void (*littlefs_dlsym(sqlite3_vfs *pVfs, void *pH, const char *zSym))(void) {
    return NULL;
}

static void littlefs_dlclose(sqlite3_vfs *pVfs, void *pHandle) {
}

static int littlefs_randomness(sqlite3_vfs *pVfs, int nByte, char *zOut) {
    #ifdef SQLITE_PLATFORM_ESP32
    for (int i = 0; i < nByte; i++) {
        zOut[i] = esp_random() & 0xFF;
    }
    #else
    /* RP2040: use a simple PRNG or hardware RNG if available */
    for (int i = 0; i < nByte; i++) {
        zOut[i] = (get_rand_32() & 0xFF);
    }
    #endif
    return nByte;
}

static int littlefs_sleep(sqlite3_vfs *pVfs, int microseconds) {
    #ifdef SQLITE_PLATFORM_ESP32
    usleep(microseconds);
    #else
    sleep_us(microseconds);
    #endif
    return microseconds;
}

static int littlefs_current_time(sqlite3_vfs *pVfs, double *prNow) {
    #ifdef SQLITE_PLATFORM_ESP32
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *prNow = 2440587.5 + tv.tv_sec / 86400.0;
    #else
    /* RP2040: Simple monotonic time if RTC not available */
    *prNow = 2440587.5 + (to_us_since_boot(get_absolute_time()) / 86400000000.0);
    #endif
    return SQLITE_OK;
}

static littlefs_vfs_t littlefs_vfs = {
    {
        1,                          /* iVersion */
        sizeof(littlefs_file_t),    /* szOsFile */
        256,                        /* mxPathname */
        NULL,                       /* pNext */
        "littlefs",                 /* zName */
        NULL,                       /* pAppData */
        littlefs_open,              /* xOpen */
        littlefs_delete,            /* xDelete */
        littlefs_access,            /* xAccess */
        littlefs_full_pathname,     /* xFullPathname */
        littlefs_dlopen,            /* xDlOpen */
        littlefs_dlerror,           /* xDlError */
        littlefs_dlsym,             /* xDlSym */
        littlefs_dlclose,           /* xDlClose */
        littlefs_randomness,        /* xRandomness */
        littlefs_sleep,             /* xSleep */
        littlefs_current_time,      /* xCurrentTime */
    }
};

int sqlite_vfs_register_littlefs(const char *name) {
    if (name) {
        littlefs_vfs.base.zName = name;
    }
    return sqlite3_vfs_register(&littlefs_vfs.base, 1);
}

#ifdef SQLITE_PLATFORM_ESP32
int sqlite_micro_init_littlefs_esp32(const char *base_path, size_t max_files) {
    esp_vfs_littlefs_conf_t conf = {
        .base_path = base_path,
        .partition_label = "littlefs",
        .format_if_mount_failed = true,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        LOG_ERROR("Failed to initialize LittleFS (%s)", esp_err_to_name(ret));
        return SQLITE_ERROR;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info(conf.partition_label, &total, &used);
    if (ret == ESP_OK) {
        LOG_INFO("LittleFS: %d KB total, %d KB used", total / 1024, used / 1024);
    }

    return SQLITE_OK;
}
#endif

#ifdef SQLITE_PLATFORM_RP2040
#include "pico_hal_littlefs.h"

int sqlite_micro_init_littlefs_rp2040(void) {
    /* Initialize LittleFS on RP2040 flash */
    int rc = pico_littlefs_init();
    if (rc != 0) {
        LOG_ERROR("Failed to initialize LittleFS on RP2040");
        return SQLITE_ERROR;
    }

    LOG_INFO("LittleFS initialized on RP2040");
    return SQLITE_OK;
}
#endif
