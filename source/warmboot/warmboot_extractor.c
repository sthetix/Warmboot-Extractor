/*
 * Warmboot Extractor
 * Based on Atmosphere fusee_setup_horizon.cpp
 *
 * Copyright (c) 2018-2025 Atmosphère-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include "warmboot_extractor.h"
#include <string.h>
#include <stdio.h>
#include <mem/heap.h>
#include <soc/fuse.h>
#include <soc/hw_init.h>
#include "../storage/nx_emmc.h"
#include "../storage/emummc.h"
#include <libs/fatfs/ff.h>
#include <utils/sprintf.h>
#include <utils/util.h>
#include <sec/se.h>
#include <storage/sdmmc.h>
#include <storage/nx_sd.h>

// Mariko keyslot for BEK (Boot Encryption Key)
#define KS_MARIKO_BEK 13

// Database entry structure for firmware -> fuse mapping
typedef struct {
    u32 firmware_version;
    u32 fuse_count;
} fuse_db_entry_t;

// Max database entries (covers future firmware releases)
#define MAX_DB_ENTRIES 64

// Global database cache
static fuse_db_entry_t fuse_database[MAX_DB_ENTRIES];
static u32 db_entry_count = 0;
static bool db_loaded = false;

// Helper function to check if this is Mariko hardware
bool is_mariko(void) {
    // Check SoC ID register to determine if Mariko
    // Mariko has different SoC revision (DRAM ID >= 4)
    return (fuse_read_dramid(false) >= 4);
}

// Read burnt fuse count
// Per fusecheck: Burnt fuses are stored in ODM6 and ODM7 registers
u8 get_burnt_fuses(void) {
    u8 fuse_count = 0;
    u32 fuse_odm6 = fuse_read_odm(6);
    u32 fuse_odm7 = fuse_read_odm(7);

    // Count bits in ODM6
    for (u32 i = 0; i < 32; i++) {
        if ((fuse_odm6 >> i) & 1)
            fuse_count++;
    }

    // Count bits in ODM7
    for (u32 i = 0; i < 32; i++) {
        if ((fuse_odm7 >> i) & 1)
            fuse_count++;
    }

    return fuse_count;
}

// Generate warmboot cache path based on fuse count
void get_warmboot_path(char *path, size_t path_size, u8 fuse_count) {
    // Format matches Atmosphère's convention:
    // Mariko: sd:/warmboot_mariko/wb_xx.bin (lowercase hex, e.g., wb_16.bin for 22 fuses)
    // Note: Erista doesn't use cached warmboot - it uses embedded binary from Atmosphère

    if (is_mariko()) {
        s_printf(path, "sd:/warmboot_mariko/wb_%02x.bin", fuse_count);
    } else {
        // Erista: Keep old format for potential future use, though not used by Atmosphère
        s_printf(path, "sd:/warmboot_erista/wb_%02x.bin", fuse_count);
    }
}

// Get target firmware version from Package1 header
// Based on Atmosphere fusee_setup_horizon.cpp:209-277
static u32 get_target_firmware_from_pkg1(const u8 *package1) {
    // Check version byte at offset 0x1F
    switch (package1[0x1F]) {
        case 0x01: return 0x100;  // 1.0.0
        case 0x02: return 0x200;  // 2.0.0
        case 0x04: return 0x300;  // 3.0.0
        case 0x07: return 0x400;  // 4.0.0
        case 0x0B: return 0x500;  // 5.0.0
        case 0x0E:
            if (memcmp(package1 + 0x10, "20180802", 8) == 0) return 0x600;  // 6.0.0
            if (memcmp(package1 + 0x10, "20181107", 8) == 0) return 0x620;  // 6.2.0
            break;
        case 0x0F: return 0x700;  // 7.0.0
        case 0x10:
            if (memcmp(package1 + 0x10, "20190314", 8) == 0) return 0x800;  // 8.0.0
            if (memcmp(package1 + 0x10, "20190531", 8) == 0) return 0x810;  // 8.1.0
            if (memcmp(package1 + 0x10, "20190809", 8) == 0) return 0x900;  // 9.0.0
            if (memcmp(package1 + 0x10, "20191021", 8) == 0) return 0x910;  // 9.1.0
            if (memcmp(package1 + 0x10, "20200303", 8) == 0) return 0xA00;  // 10.0.0
            if (memcmp(package1 + 0x10, "20201030", 8) == 0) return 0xB00;  // 11.0.0
            if (memcmp(package1 + 0x10, "20210129", 8) == 0) return 0xC00;  // 12.0.0
            if (memcmp(package1 + 0x10, "20210422", 8) == 0) return 0xC02;  // 12.0.2
            if (memcmp(package1 + 0x10, "20210607", 8) == 0) return 0xC10;  // 12.1.0
            if (memcmp(package1 + 0x10, "20210805", 8) == 0) return 0xD00;  // 13.0.0
            if (memcmp(package1 + 0x10, "20220105", 8) == 0) return 0xD21;  // 13.2.1
            if (memcmp(package1 + 0x10, "20220209", 8) == 0) return 0xE00;  // 14.0.0
            if (memcmp(package1 + 0x10, "20220801", 8) == 0) return 0xF00;  // 15.0.0
            if (memcmp(package1 + 0x10, "20230111", 8) == 0) return 0x1000; // 16.0.0
            if (memcmp(package1 + 0x10, "20230906", 8) == 0) return 0x1100; // 17.0.0
            if (memcmp(package1 + 0x10, "20240207", 8) == 0) return 0x1200; // 18.0.0
            if (memcmp(package1 + 0x10, "20240808", 8) == 0) return 0x1300; // 19.0.0
            if (memcmp(package1 + 0x10, "20250206", 8) == 0) return 0x1400; // 20.0.0
            if (memcmp(package1 + 0x10, "20251009", 8) == 0) return 0x1500; // 21.0.0
            break;
        default:
            break;
    }
    return 0;  // Unknown
}

// Load fuse database from sd:/config/wb_db.txt
// Format: FIRMWARE_VERSION=FUSE_COUNT (hex values)
// Example: 0x1500=22
bool load_wb_database_from_sd(void) {
    if (db_loaded) return true;  // Already loaded
    
    FIL fp;
    FRESULT res;
    char buffer[128];
    
    // Try to mount SD card
    if (!sd_mount()) {
        return false;  // SD card not available, will use hardcoded
    }
    
    // Try to open database file
    res = f_open(&fp, "sd:/config/wb_db.txt", FA_READ);
    if (res != FR_OK) {
        sd_end();
        return false;  // File not found, will use hardcoded
    }
    
    db_entry_count = 0;
    
    // Parse database file
    while (f_gets(buffer, sizeof(buffer), &fp) != NULL && db_entry_count < MAX_DB_ENTRIES) {
        // Skip comments and empty lines
        if (buffer[0] == '#' || buffer[0] == '\n' || buffer[0] == '\r') {
            continue;
        }
        
        // Parse "FIRMWARE_VERSION=FUSE_COUNT"
        char *eq_pos = strchr(buffer, '=');
        if (!eq_pos) continue;
        
        u32 fw_ver = 0;
        u32 fuse_cnt = 0;
        
        // Parse firmware version (hex)
        if (sscanf(buffer, "%x=%u", &fw_ver, &fuse_cnt) == 2) {
            fuse_database[db_entry_count].firmware_version = fw_ver;
            fuse_database[db_entry_count].fuse_count = fuse_cnt;
            db_entry_count++;
        }
    }
    
    f_close(&fp);
    sd_end();
    
    db_loaded = (db_entry_count > 0);
    return db_loaded;
}

// Get expected fuse version for a given target firmware
// Based on Atmosphere fuse::GetExpectedFuseVersion in fuse_api.cpp
// The array has 22 entries, returns (22 - index) where index is first match
u32 get_expected_fuse_version(u32 target_firmware) {
    // Try external database first (if loaded)
    if (db_loaded && db_entry_count > 0) {
        // Linear search through database (sorted descending)
        for (u32 i = 0; i < db_entry_count; i++) {
            if (target_firmware >= fuse_database[i].firmware_version) {
                return fuse_database[i].fuse_count;
            }
        }
    }
    
    // Fallback to hardcoded database
    // FuseVersionIncrementFirmwares array (22 entries, sorted descending):
    // Index 0:  21.0.0 -> returns 22
    // Index 1:  20.0.0 -> returns 21
    // Index 2:  19.0.0 -> returns 20
    // Index 3:  17.0.0 -> returns 19 (note: no 18.0.0 entry!)
    // Index 4:  16.0.0 -> returns 18
    // Index 5:  15.0.0 -> returns 17
    // Index 6:  13.2.1 -> returns 16
    // Index 7:  12.0.2 -> returns 15
    // Index 8:  11.0.0 -> returns 14
    // Index 9:  10.0.0 -> returns 13
    // Index 10: 9.1.0  -> returns 12
    // Index 11: 9.0.0  -> returns 11
    // Index 12: 8.1.0  -> returns 10
    // Index 13: 7.0.0  -> returns 9
    // Index 14: 6.2.0  -> returns 8
    // Index 15: 6.0.0  -> returns 7
    // Index 16: 5.0.0  -> returns 6
    // Index 17: 4.0.0  -> returns 5
    // Index 18: 3.0.2  -> returns 4
    // Index 19: 3.0.0  -> returns 3
    // Index 20: 2.0.0  -> returns 2
    // Index 21: 1.0.0  -> returns 1

    if (target_firmware >= 0x1500) return 22; // 21.0.0+
    if (target_firmware >= 0x1400) return 21; // 20.0.0+
    if (target_firmware >= 0x1300) return 20; // 19.0.0+
    if (target_firmware >= 0x1100) return 19; // 17.0.0+ (no 18.0.0 fuse bump)
    if (target_firmware >= 0x1000) return 18; // 16.0.0+
    if (target_firmware >= 0xF00)  return 17; // 15.0.0+
    if (target_firmware >= 0xD21)  return 16; // 13.2.1+
    if (target_firmware >= 0xC02)  return 15; // 12.0.2+
    if (target_firmware >= 0xB00)  return 14; // 11.0.0+
    if (target_firmware >= 0xA00)  return 13; // 10.0.0+
    if (target_firmware >= 0x910)  return 12; // 9.1.0+
    if (target_firmware >= 0x900)  return 11; // 9.0.0+
    if (target_firmware >= 0x810)  return 10; // 8.1.0+
    if (target_firmware >= 0x700)  return 9;  // 7.0.0+
    if (target_firmware >= 0x620)  return 8;  // 6.2.0+
    if (target_firmware >= 0x600)  return 7;  // 6.0.0+
    if (target_firmware >= 0x500)  return 6;  // 5.0.0+
    if (target_firmware >= 0x400)  return 5;  // 4.0.0+
    if (target_firmware >= 0x302)  return 4;  // 3.0.2+
    if (target_firmware >= 0x300)  return 3;  // 3.0.0+
    if (target_firmware >= 0x200)  return 2;  // 2.0.0+
    if (target_firmware >= 0x100)  return 1;  // 1.0.0+
    return 0;
}

// Error code to string for debugging
const char *wb_error_to_string(wb_extract_error_t err) {
    switch (err) {
        case WB_SUCCESS:                  return "Success";
        case WB_ERR_NULL_INFO:            return "NULL wb_info pointer";
        case WB_ERR_ERISTA_NOT_SUPPORTED: return "Erista not supported (uses embedded warmboot)";
        case WB_ERR_MALLOC_PKG1:          return "Failed to allocate Package1 buffer (256KB)";
        case WB_ERR_MMC_INIT:             return "Failed to initialize eMMC";
        case WB_ERR_MMC_PARTITION:        return "Failed to set BOOT0 partition";
        case WB_ERR_MMC_READ:             return "Failed to read Package1 from BOOT0";
        case WB_ERR_DECRYPT_VERIFY:       return "Package1 decryption failed (BEK missing or wrong)";
        case WB_ERR_PK11_MAGIC:           return "PK11 magic not found (invalid Package1)";
        case WB_ERR_WB_SIZE_INVALID:      return "Warmboot size invalid (not 0x800-0x1000)";
        case WB_ERR_MALLOC_WB:            return "Failed to allocate warmboot buffer";
        default:                          return "Unknown error";
    }
}

// Extended extraction with detailed error codes
wb_extract_error_t extract_warmboot_from_pkg1_ex(warmboot_info_t *wb_info) {
    if (!wb_info)
        return WB_ERR_NULL_INFO;

    // Initialize result
    memset(wb_info, 0, sizeof(warmboot_info_t));
    wb_info->is_erista = !is_mariko();
    wb_info->fuse_count = get_burnt_fuses();

    // Erista doesn't need warmboot extraction from Package1
    if (wb_info->is_erista) {
        return WB_ERR_ERISTA_NOT_SUPPORTED;
    }

    // From here on, we're dealing with Mariko only

    // Allocate buffer for Package1
    u8 *pkg1_buffer = (u8 *)malloc(PKG1_SIZE);
    if (!pkg1_buffer)
        return WB_ERR_MALLOC_PKG1;

    // Keep original pointer for freeing later
    u8 *pkg1_buffer_orig = pkg1_buffer;

    // Read Package1 from BOOT0
    // When chainloaded from Hekate, eMMC might already be initialized
    // or in an unexpected state. We need to properly end any existing
    // session and reinitialize.

    // First, cleanly end any existing eMMC session
    // This handles the case where Hekate left eMMC in an initialized state
    if (emmc_storage.sdmmc != NULL) {
        sdmmc_storage_end(&emmc_storage);
    }

    // Small delay to allow eMMC controller to settle
    usleep(1000);

    // emummc_storage_init_mmc() returns:
    //   0 = success (sysMMC mode)
    //   1 = emuMMC file-based error
    //   2 = eMMC hardware init failed
    int mmc_res = emummc_storage_init_mmc();
    if (mmc_res == 2) {
        free(pkg1_buffer_orig);
        return WB_ERR_MMC_INIT;
    }

    if (!emummc_storage_set_mmc_partition(EMMC_BOOT0)) {
        emummc_storage_end();
        free(pkg1_buffer_orig);
        return WB_ERR_MMC_PARTITION;
    }

    // Read package1 from offset 0x100000
    if (!emummc_storage_read(PKG1_OFFSET / NX_EMMC_BLOCKSIZE, PKG1_SIZE / NX_EMMC_BLOCKSIZE, pkg1_buffer)) {
        emummc_storage_end();
        free(pkg1_buffer_orig);
        return WB_ERR_MMC_READ;
    }

    emummc_storage_end();

    // On Mariko, Package1 is encrypted and needs decryption
    // Skip 0x170 byte header to get to the encrypted payload
    u8 *pkg1_mariko = pkg1_buffer + 0x170;

    // Set IV from package1 + 0x10 (16 bytes)
    se_aes_iv_set(KS_MARIKO_BEK, pkg1_mariko + 0x10);

    // Decrypt using BEK at keyslot 13
    se_aes_crypt_cbc(KS_MARIKO_BEK, 0, /* DECRYPT */
                    pkg1_mariko + 0x20,
                    0x40000 - (0x20 + 0x170),
                    pkg1_mariko + 0x20,
                    0x40000 - (0x20 + 0x170));

    // Verify decryption (first 0x20 bytes should match decrypted header)
    if (memcmp(pkg1_mariko, pkg1_mariko + 0x20, 0x20) != 0) {
        free(pkg1_buffer_orig);
        return WB_ERR_DECRYPT_VERIFY;
    }

    // Use pkg1_mariko as the working pointer
    pkg1_buffer = pkg1_mariko;

    // Detect target firmware (for display purposes only)
    u32 target_fw = get_target_firmware_from_pkg1(pkg1_buffer);

    // Store debug info: Package1 version byte and date string
    wb_info->pkg1_version = pkg1_buffer[0x1F];
    memcpy(wb_info->pkg1_date, pkg1_buffer + 0x10, 8);
    wb_info->pkg1_date[8] = '\0';

    // Get burnt fuse count from device
    u8 burnt_fuses = get_burnt_fuses();

    // For warmboot naming, ALWAYS use burnt_fuses directly.
    // This is the correct approach because:
    // 1. The warmboot extracted from Package1 is compatible with the current fuse count
    // 2. Works for unknown/future firmware versions without modification
    // 3. No dependency on firmware detection accuracy
    // 4. Matches Atmosphère's search logic (starts from burnt_fuses and goes up)
    //
    // The device's burnt fuses are the source of truth for warmboot compatibility,
    // not the firmware version.

    // Store fuse information - use burnt_fuses for naming
    wb_info->fuse_count = burnt_fuses;
    wb_info->burnt_fuses = burnt_fuses;
    wb_info->target_firmware = target_fw;

    // Determine PK11 offset using firmware hint first (Atmosphere logic),
    // then fall back to magic checks to stay compatible with unknown FW.
    u32 pk11_offset = 0x4000;
    if (target_fw != 0 && target_fw >= 0x620)
        pk11_offset = 0x7000;

    u8 *pk11_ptr_u8 = pkg1_buffer + pk11_offset;
    bool pk11_ok = memcmp(pk11_ptr_u8, "PK11", 4) == 0;

    // If preferred offset fails, try the other one
    if (!pk11_ok) {
        pk11_offset = (pk11_offset == 0x4000) ? 0x7000 : 0x4000;
        pk11_ptr_u8 = pkg1_buffer + pk11_offset;
        pk11_ok = memcmp(pk11_ptr_u8, "PK11", 4) == 0;
    }

    if (!pk11_ok) {
        free(pkg1_buffer_orig);
        return WB_ERR_PK11_MAGIC;
    }

    // Store debug info: PK11 offset used
    wb_info->pk11_offset = pk11_offset;

    u32 *pk11_ptr = (u32 *)pk11_ptr_u8;

    // Store debug info: PK11 header (first 8 u32s = 32 bytes)
    for (int i = 0; i < 8; i++) {
        wb_info->pk11_header[i] = pk11_ptr[i];
    }

    // Navigate through PK11 container to find warmboot
    // This EXACTLY matches Atmosphere's logic in fusee_setup_horizon.cpp
    u32 *pk11_data = pk11_ptr + (0x20 / sizeof(u32));

    // Initialize debug sig array
    wb_info->sig_found[0] = 0;
    wb_info->sig_found[1] = 0;
    wb_info->sig_found[2] = 0;

    // Atmosphere's navigation loop - iterate up to 3 times to skip payloads
    for (int i = 0; i < 3; i++) {
        u32 signature = *pk11_data;
        
        // Store signature for debug
        wb_info->sig_found[i] = signature;
        
        switch (signature) {
            case SIG_NX_BOOTLOADER:    // 0xD5034FDF
                // Skip NX Bootloader using size from pk11[6]
                pk11_data += pk11_ptr[6] / sizeof(u32);
                break;
            case SIG_SECURE_MONITOR_1:  // 0xE328F0C0
            case SIG_SECURE_MONITOR_2:  // 0xF0C0A7F0
                // Skip Secure Monitor using size from pk11[4]
                pk11_data += pk11_ptr[4] / sizeof(u32);
                break;
            default:
                // No known signature - this is warmboot
                // Exit loop immediately (don't skip)
                i = 3;
                break;
        }
    }

    // Store debug info: Final offset from pk11_ptr to pk11_data
    wb_info->debug_ptr_offset = (u32)((u8 *)pk11_data - pk11_ptr_u8);

    // Read warmboot size (EXACTLY like Atmosphere does AFTER the loop)
    // warmboot_src_size = *package1_pk11_data;
    u32 wb_size = *pk11_data;
    
    // Determine layout type for debugging (not used for extraction logic)
    u32 warmboot_size_header = pk11_ptr[1];
    if (warmboot_size_header == wb_size && 
        warmboot_size_header >= WARMBOOT_MIN_SIZE && 
        warmboot_size_header < WARMBOOT_MAX_SIZE) {
        wb_info->debug_layout_type = 1;  // New layout (warmboot at offset 0)
    } else {
        wb_info->debug_layout_type = 2;  // Traditional layout
    }

    // Debug: Store additional info for troubleshooting
    wb_info->target_firmware = target_fw;

    if (wb_size < WARMBOOT_MIN_SIZE || wb_size >= WARMBOOT_MAX_SIZE) {
        // Store debug info: what we actually found
        // wb_info->size will contain the invalid value for debugging
        wb_info->size = wb_size;
        free(pkg1_buffer_orig);
        return WB_ERR_WB_SIZE_INVALID;
    }

    wb_info->size = wb_size;

    // DEBUG: Store first 16 bytes of warmboot data for comparison
    memcpy(wb_info->debug_warmboot_preview, (u8 *)pk11_data, 16);

    // Allocate warmboot buffer
    // Atmosphere format is: [size_u32][warmboot_binary...]
    // The size field (*pk11_data) is INCLUDED in the data we save
    u8 *wb_data = (u8 *)malloc(wb_size);
    if (!wb_data) {
        free(pkg1_buffer_orig);
        return WB_ERR_MALLOC_WB;
    }

    // Copy warmboot data INCLUDING the size prefix at the beginning
    // This matches Atmosphere's exact format: warmboot_src points to the size field
    // and warmboot_src_size is the total bytes to write (size + actual warmboot binary)
    memcpy(wb_data, (u8 *)pk11_data, wb_size);

    // Set result
    wb_info->data = wb_data;
    wb_info->size = wb_size;

    free(pkg1_buffer_orig);
    return WB_SUCCESS;
}

// Original wrapper for backward compatibility
bool extract_warmboot_from_pkg1(warmboot_info_t *wb_info) {
    return extract_warmboot_from_pkg1_ex(wb_info) == WB_SUCCESS;
}

// Save warmboot to SD card
bool save_warmboot_to_sd(const warmboot_info_t *wb_info, const char *path) {
    if (!wb_info || !wb_info->data || !path)
        return false;

    FIL fp;
    UINT bytes_written;

    // Create directory if needed
    const char *dir_path = wb_info->is_erista ? "sd:/warmboot_erista" : "sd:/warmboot_mariko";
    f_mkdir(dir_path);

    // Open file for writing
    if (f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
        return false;

    // CRITICAL: Atmosphere's format is ALWAYS: [size_u32][warmboot_binary...]
    // Our wb_info->data already contains this format because we copied from pk11_data
    // which points to the size field. The size value (wb_info->size) is the total
    // number of bytes to write, matching exactly what Atmosphere does:
    //   fs::WriteFile(file, 0, warmboot_src, warmboot_src_size, ...)
    // where warmboot_src points to the size field and warmboot_src_size is the total.
    //
    // So we just write the data as-is, no conditional logic needed.
    if (f_write(&fp, wb_info->data, wb_info->size, &bytes_written) != FR_OK) {
        f_close(&fp);
        return false;
    }

    f_close(&fp);
    
    // Verify write succeeded
    return (bytes_written == wb_info->size);
}
