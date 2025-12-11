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

#ifndef _WARMBOOT_EXTRACTOR_H_
#define _WARMBOOT_EXTRACTOR_H_

#include <stddef.h>
#include <utils/types.h>

// Warmboot binary size constraints
#define WARMBOOT_MIN_SIZE 0x800   // 2048 bytes
#define WARMBOOT_MAX_SIZE 0x1000  // 4096 bytes

// Package1 locations
#define PKG1_OFFSET       0x100000  // 1MB into BOOT0
#define PKG1_SIZE         0x40000   // 256KB

// PK11 magic
#define PK11_MAGIC        0x31314B50  // "PK11" in little endian

// Known payload signatures to skip
#define SIG_NX_BOOTLOADER 0xD5034FDF
#define SIG_SECURE_MONITOR_1 0xE328F0C0
#define SIG_SECURE_MONITOR_2 0xF0C0A7F0

// Warmboot metadata structure
typedef struct {
    u32 magic;              // "WBT0" (0x30544257)
    u32 target_firmware;    // Target firmware version
    u32 reserved[2];        // Reserved fields
} warmboot_metadata_t;

// Warmboot extraction result
typedef struct {
    u8 *data;               // Warmboot binary data
    u32 size;               // Size of warmboot binary
    u8 fuse_count;          // Burnt fuse count (used for naming: wb_XX.bin)
    u8 burnt_fuses;         // Actual burnt fuses on device (same as fuse_count)
    u32 target_firmware;    // Detected target firmware (0 if unknown, for display only)
    bool is_erista;         // True if Erista, false if Mariko
    // Debug fields for troubleshooting
    u32 pk11_offset;        // Which PK11 offset was used (0x4000 or 0x7000)
    u32 pk11_header[8];     // First 32 bytes of PK11 header (8 u32s)
    u32 sig_found[3];       // Signatures found during payload iteration
    u32 debug_ptr_offset;   // Final offset from pk11_ptr to warmboot location
    u32 debug_layout_type;  // 1 = new layout (warmboot at offset 0), 2 = traditional layout
    u8 debug_warmboot_preview[16];  // First 16 bytes of warmboot data (encrypted)
    u8 pkg1_date[12];       // Package1 date string (8 chars + null)
    u8 pkg1_version;        // Package1 version byte at offset 0x1F
} warmboot_info_t;

// Extraction error codes for debugging
typedef enum {
    WB_SUCCESS = 0,
    WB_ERR_NULL_INFO,
    WB_ERR_ERISTA_NOT_SUPPORTED,
    WB_ERR_MALLOC_PKG1,
    WB_ERR_MMC_INIT,
    WB_ERR_MMC_PARTITION,
    WB_ERR_MMC_READ,
    WB_ERR_DECRYPT_VERIFY,
    WB_ERR_PK11_MAGIC,
    WB_ERR_WB_SIZE_INVALID,
    WB_ERR_MALLOC_WB,
} wb_extract_error_t;

// Function prototypes
wb_extract_error_t extract_warmboot_from_pkg1_ex(warmboot_info_t *wb_info);
bool extract_warmboot_from_pkg1(warmboot_info_t *wb_info);
bool save_warmboot_to_sd(const warmboot_info_t *wb_info, const char *path);
u8 get_burnt_fuses(void);
bool is_mariko(void);
void get_warmboot_path(char *path, size_t path_size, u8 fuse_count);
u32 get_expected_fuse_version(u32 target_firmware);
bool load_wb_database_from_sd(void);
const char *wb_error_to_string(wb_extract_error_t err);

#endif /* _WARMBOOT_EXTRACTOR_H_ */
