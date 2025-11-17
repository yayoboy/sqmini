#include "sqlite3.h"
#include "sqlite_micro/sqlite_micro.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#ifdef SQLITE_PLATFORM_ESP32
#include <esp_vfs_fat.h>
#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <sdmmc_cmd.h>
#include <esp_log.h>
#define LOG_TAG "VFS_SD"
#define LOG_INFO(fmt, ...) ESP_LOGI(LOG_TAG, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) ESP_LOGE(LOG_TAG, fmt, ##__VA_ARGS__)
#elif defined(SQLITE_PLATFORM_RP2040)
#include <stdio.h>
#include "pico/stdlib.h"
#include "ff.h"  /* FatFS */
#include "hw_config.h"
#define LOG_INFO(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#endif

/* SD Card VFS File structure */
typedef struct {
    sqlite3_file base;  /* Base class - must be first */
    int fd;            /* File descriptor */
    char *path;        /* File path */
} sdcard_file_t;

/* SD Card VFS structure */
typedef struct {
    sqlite3_vfs base;  /* Base class - must be first */
} sdcard_vfs_t;

static int sdcard_close(sqlite3_file *pFile) {
    sdcard_file_t *p = (sdcard_file_t*)pFile;
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

static int sdcard_read(sqlite3_file *pFile, void *zBuf, int iAmt, sqlite_int64 iOfst) {
    sdcard_file_t *p = (sdcard_file_t*)pFile;
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

static int sdcard_write(sqlite3_file *pFile, const void *zBuf, int iAmt, sqlite_int64 iOfst) {
    sdcard_file_t *p = (sdcard_file_t*)pFile;
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

static int sdcard_truncate(sqlite3_file *pFile, sqlite_int64 size) {
    sdcard_file_t *p = (sdcard_file_t*)pFile;

    if (ftruncate(p->fd, size) != 0) {
        return SQLITE_IOERR_TRUNCATE;
    }

    return SQLITE_OK;
}

static int sdcard_sync(sqlite3_file *pFile, int flags) {
    sdcard_file_t *p = (sdcard_file_t*)pFile;

    if (fsync(p->fd) != 0) {
        return SQLITE_IOERR_FSYNC;
    }

    return SQLITE_OK;
}

static int sdcard_file_size(sqlite3_file *pFile, sqlite_int64 *pSize) {
    sdcard_file_t *p = (sdcard_file_t*)pFile;
    struct stat st;

    if (fstat(p->fd, &st) != 0) {
        return SQLITE_IOERR_FSTAT;
    }

    *pSize = st.st_size;
    return SQLITE_OK;
}

static int sdcard_lock(sqlite3_file *pFile, int eLock) {
    /* Basic locking support */
    return SQLITE_OK;
}

static int sdcard_unlock(sqlite3_file *pFile, int eLock) {
    return SQLITE_OK;
}

static int sdcard_check_reserved_lock(sqlite3_file *pFile, int *pResOut) {
    *pResOut = 0;
    return SQLITE_OK;
}

static int sdcard_file_control(sqlite3_file *pFile, int op, void *pArg) {
    return SQLITE_NOTFOUND;
}

static int sdcard_sector_size(sqlite3_file *pFile) {
    return 512;  /* SD card sector size */
}

static int sdcard_device_characteristics(sqlite3_file *pFile) {
    return SQLITE_IOCAP_SAFE_APPEND;
}

static const sqlite3_io_methods sdcard_io_methods = {
    1,                                  /* iVersion */
    sdcard_close,                       /* xClose */
    sdcard_read,                        /* xRead */
    sdcard_write,                       /* xWrite */
    sdcard_truncate,                    /* xTruncate */
    sdcard_sync,                        /* xSync */
    sdcard_file_size,                   /* xFileSize */
    sdcard_lock,                        /* xLock */
    sdcard_unlock,                      /* xUnlock */
    sdcard_check_reserved_lock,         /* xCheckReservedLock */
    sdcard_file_control,                /* xFileControl */
    sdcard_sector_size,                 /* xSectorSize */
    sdcard_device_characteristics,      /* xDeviceCharacteristics */
};

static int sdcard_open(sqlite3_vfs *pVfs, const char *zName, sqlite3_file *pFile,
                      int flags, int *pOutFlags) {
    sdcard_file_t *p = (sdcard_file_t*)pFile;
    int oflags = 0;

    memset(p, 0, sizeof(sdcard_file_t));
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

    p->base.pMethods = &sdcard_io_methods;
    return SQLITE_OK;
}

static int sdcard_delete(sqlite3_vfs *pVfs, const char *zPath, int dirSync) {
    if (unlink(zPath) != 0) {
        return SQLITE_IOERR_DELETE;
    }
    return SQLITE_OK;
}

static int sdcard_access(sqlite3_vfs *pVfs, const char *zPath, int flags, int *pResOut) {
    int rc = access(zPath, F_OK);
    *pResOut = (rc == 0) ? 1 : 0;
    return SQLITE_OK;
}

static int sdcard_full_pathname(sqlite3_vfs *pVfs, const char *zPath, int nOut, char *zOut) {
    snprintf(zOut, nOut, "%s", zPath);
    return SQLITE_OK;
}

static void *sdcard_dlopen(sqlite3_vfs *pVfs, const char *zPath) {
    return NULL;
}

static void sdcard_dlerror(sqlite3_vfs *pVfs, int nByte, char *zErrMsg) {
    snprintf(zErrMsg, nByte, "Dynamic loading not supported");
}

static void (*sdcard_dlsym(sqlite3_vfs *pVfs, void *pH, const char *zSym))(void) {
    return NULL;
}

static void sdcard_dlclose(sqlite3_vfs *pVfs, void *pHandle) {
}

static int sdcard_randomness(sqlite3_vfs *pVfs, int nByte, char *zOut) {
    #ifdef SQLITE_PLATFORM_ESP32
    for (int i = 0; i < nByte; i++) {
        zOut[i] = esp_random() & 0xFF;
    }
    #else
    for (int i = 0; i < nByte; i++) {
        zOut[i] = (get_rand_32() & 0xFF);
    }
    #endif
    return nByte;
}

static int sdcard_sleep(sqlite3_vfs *pVfs, int microseconds) {
    #ifdef SQLITE_PLATFORM_ESP32
    usleep(microseconds);
    #else
    sleep_us(microseconds);
    #endif
    return microseconds;
}

static int sdcard_current_time(sqlite3_vfs *pVfs, double *prNow) {
    #ifdef SQLITE_PLATFORM_ESP32
    struct timeval tv;
    gettimeofday(&tv, NULL);
    *prNow = 2440587.5 + tv.tv_sec / 86400.0;
    #else
    *prNow = 2440587.5 + (to_us_since_boot(get_absolute_time()) / 86400000000.0);
    #endif
    return SQLITE_OK;
}

static sdcard_vfs_t sdcard_vfs = {
    {
        1,                          /* iVersion */
        sizeof(sdcard_file_t),      /* szOsFile */
        256,                        /* mxPathname */
        NULL,                       /* pNext */
        "sdcard",                   /* zName */
        NULL,                       /* pAppData */
        sdcard_open,                /* xOpen */
        sdcard_delete,              /* xDelete */
        sdcard_access,              /* xAccess */
        sdcard_full_pathname,       /* xFullPathname */
        sdcard_dlopen,              /* xDlOpen */
        sdcard_dlerror,             /* xDlError */
        sdcard_dlsym,               /* xDlSym */
        sdcard_dlclose,             /* xDlClose */
        sdcard_randomness,          /* xRandomness */
        sdcard_sleep,               /* xSleep */
        sdcard_current_time,        /* xCurrentTime */
    }
};

int sqlite_vfs_register_sdcard(const char *name) {
    if (name) {
        sdcard_vfs.base.zName = name;
    }
    return sqlite3_vfs_register(&sdcard_vfs.base, 1);
}

#ifdef SQLITE_PLATFORM_ESP32
static sdmmc_card_t *card = NULL;

int sqlite_micro_init_sdcard(int cs_pin, int mosi_pin, int miso_pin, int sck_pin) {
    esp_err_t ret;

    /* Options for mounting the filesystem */
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num = mosi_pin,
        .miso_io_num = miso_pin,
        .sclk_io_num = sck_pin,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4000,
    };

    ret = spi_bus_initialize(host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        LOG_ERROR("Failed to initialize bus");
        return SQLITE_ERROR;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = cs_pin;
    slot_config.host_id = host.slot;

    ret = esp_vfs_fat_sdspi_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        LOG_ERROR("Failed to mount SD card (%s)", esp_err_to_name(ret));
        return SQLITE_ERROR;
    }

    /* Card has been initialized, print its properties */
    sdmmc_card_print_info(stdout, card);
    LOG_INFO("SD card mounted successfully");

    return SQLITE_OK;
}
#endif

#ifdef SQLITE_PLATFORM_RP2040
int sqlite_micro_init_sdcard(int cs_pin, int mosi_pin, int miso_pin, int sck_pin) {
    /* Initialize SD card using FatFS and no-OS-FatFS-SD-SPI-RPi-Pico library */
    sd_card_t *pSD = sd_get_by_num(0);
    if (pSD == NULL) {
        LOG_ERROR("Failed to get SD card");
        return SQLITE_ERROR;
    }

    FRESULT fr = f_mount(&pSD->fatfs, pSD->pcName, 1);
    if (FR_OK != fr) {
        LOG_ERROR("Failed to mount SD card (error %d)", fr);
        return SQLITE_ERROR;
    }

    LOG_INFO("SD card mounted successfully");
    return SQLITE_OK;
}
#endif
