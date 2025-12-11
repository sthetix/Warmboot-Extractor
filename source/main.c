/*
 * Warmboot Generator - Extract warmboot firmware from Package1
 * Based on FuseCheck and Atmosphere fusee
 *
 * Copyright (c) 2018-2025 Atmosphère-NX and contributors
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <string.h>
#include "config.h"
#include <display/di.h>
#include <gfx_utils.h>
#include <mem/heap.h>
#include <mem/minerva.h>
#include <memory_map.h>
#include <power/max77620.h>
#include <soc/bpmp.h>
#include <soc/fuse.h>
#include <soc/hw_init.h>
#include <soc/t210.h>
#include "storage/emummc.h"
#include "storage/nx_emmc.h"
#include <storage/nx_sd.h>
#include <storage/sdmmc.h>
#include <utils/btn.h>
#include <utils/util.h>
#include <utils/types.h>
#include <utils/sprintf.h>
#include "frontend/gui.h"
#include "warmboot/warmboot_extractor.h"
#include <input/touch.h>

// Color definitions - Muted terminal palette
#define COLOR_WHITE     0xFFFFFFFF
#define COLOR_RED       0xFFDD0000
#define COLOR_GREEN     0xFF00DD00
#define COLOR_BLUE      0xFF0000FF
#define COLOR_YELLOW    0xFFFFDD00
#define COLOR_ORANGE    0xFFFF9900
#define COLOR_CYAN      0xFF00D7FF
#define COLOR_VIOLET    0xFFFF00FF
#define COLOR_DEFAULT   0xFF1B1B1B
#define SETCOLOR(fg, bg) gfx_con_setcol(fg, 1, bg)
#define RESETCOLOR SETCOLOR(COLOR_WHITE, COLOR_DEFAULT)

hekate_config h_cfg;
boot_cfg_t __attribute__((section ("._boot_cfg"))) b_cfg;
const volatile ipl_ver_meta_t __attribute__((section ("._ipl_version"))) ipl_ver = {
	.magic = LP_MAGIC,
	.version = (LP_VER_MJ + '0') | ((LP_VER_MN + '0') << 8) | ((LP_VER_BF + '0') << 16),
	.rsvd0 = 0,
	.rsvd1 = 0
};

volatile nyx_storage_t *nyx_str = (nyx_storage_t *)NYX_STORAGE_ADDR;
extern void pivot_stack(u32 stack_top);

// Payload relocation defines
#define RELOC_META_OFF      0x7C
#define PATCHED_RELOC_SZ    0x94
#define PATCHED_RELOC_STACK 0x40007000
#define PATCHED_RELOC_ENTRY 0x40010000
#define EXT_PAYLOAD_ADDR    0xC0000000
#define RCM_PAYLOAD_ADDR    (EXT_PAYLOAD_ADDR + ALIGN(PATCHED_RELOC_SZ, 0x10))
#define COREBOOT_END_ADDR   0xD0000000
#define COREBOOT_VER_OFF    0x41
#define CBFS_DRAM_EN_ADDR   0x4003e000
#define CBFS_DRAM_MAGIC     0x4452414D // "DRAM"

static void *coreboot_addr;
static char *payload_path = NULL;

void reloc_patcher(u32 payload_dst, u32 payload_src, u32 payload_size) {
    memcpy((u8 *)payload_src, (u8 *)IPL_LOAD_ADDR, PATCHED_RELOC_SZ);

    volatile reloc_meta_t *relocator = (reloc_meta_t *)(payload_src + RELOC_META_OFF);

    relocator->start = payload_dst - ALIGN(PATCHED_RELOC_SZ, 0x10);
    relocator->stack = PATCHED_RELOC_STACK;
    relocator->end   = payload_dst + payload_size;
    relocator->ep    = payload_dst;

    if (payload_size == 0x7000) {
        memcpy((u8 *)(payload_src + ALIGN(PATCHED_RELOC_SZ, 0x10)), coreboot_addr, 0x7000);
        *(vu32 *)CBFS_DRAM_EN_ADDR = CBFS_DRAM_MAGIC;
    }
}

int launch_payload(char *path) {
    if (!path) return 1;

    if (sd_mount()) {
        FIL fp;
        if (f_open(&fp, path, FA_READ)) {
            return 1;
        }

        void *buf;
        u32 size = f_size(&fp);

        if (size < 0x30000)
            buf = (void *)RCM_PAYLOAD_ADDR;
        else {
            coreboot_addr = (void *)(COREBOOT_END_ADDR - size);
            buf = coreboot_addr;
            if (h_cfg.t210b01) {
                f_close(&fp);
                return 1;
            }
        }

        if (f_read(&fp, buf, size, NULL)) {
            f_close(&fp);
            return 1;
        }

        f_close(&fp);
        sd_end();

        if (size < 0x30000) {
            reloc_patcher(PATCHED_RELOC_ENTRY, EXT_PAYLOAD_ADDR, ALIGN(size, 0x10));
            hw_reinit_workaround(false, byte_swap_32(*(u32 *)(buf + size - sizeof(u32))));
        } else {
            reloc_patcher(PATCHED_RELOC_ENTRY, EXT_PAYLOAD_ADDR, 0x7000);
            u32 magic = 0;
            char *magic_ptr = buf + COREBOOT_VER_OFF;
            memcpy(&magic, magic_ptr + strlen(magic_ptr) - 4, 4);
            hw_reinit_workaround(true, magic);
        }

        // Some cards (Sandisk U1), do not like a fast power cycle. Wait min 100ms.
        sdmmc_storage_init_wait_sd();

        void (*ext_payload_ptr)() = (void *)EXT_PAYLOAD_ADDR;
        void (*update_ptr)() = (void *)RCM_PAYLOAD_ADDR;

        // Launch
        if (size < 0x30000)
            (*update_ptr)();
        else
            (*ext_payload_ptr)();
    }

    return 1;
}

// Print centered text
void print_centered(int y, const char *text) {
    int len = strlen(text);
    int x = (1280 - (len * 16)) / 2;
    gfx_con_setpos(x, y);
    gfx_printf("%s", text);
}

// Print header
void print_header(void) {
    gfx_clear_grey(0x1B);
    gfx_con_setpos(0, 0);

    SETCOLOR(COLOR_CYAN, COLOR_DEFAULT);
    print_centered(10, "WARMBOOT EXTRACTOR");
    RESETCOLOR;
    gfx_printf("\n");
}

// Print status message with coordinate positioning
void print_status(int x, int y, const char *status, u32 color) {
    gfx_con_setpos(x, y);
    SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
    gfx_printf("[*] ");
    SETCOLOR(color, COLOR_DEFAULT);
    gfx_printf("%s", status);
    RESETCOLOR;
}

// Print info message with coordinate positioning
void print_info(int x, int y, const char *label, const char *value) {
    gfx_con_setpos(x, y);
    SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
    gfx_printf("%s: ", label);
    SETCOLOR(COLOR_CYAN, COLOR_DEFAULT);
    gfx_printf("%s", value);
    RESETCOLOR;
}

// Main extraction workflow
void warmboot_extraction_workflow(void) {
    warmboot_info_t wb_info;
    char path[256];
    char temp[128];

    print_header();

    // Load external fuse database from SD card (optional, uses hardcoded fallback)
    load_wb_database_from_sd();

    // Display system information
    int y_pos = 48;
    SETCOLOR(COLOR_CYAN, COLOR_DEFAULT);
    gfx_con_setpos(24, y_pos);
    gfx_printf("System Information:");
    RESETCOLOR;

    // Detect SoC type
    y_pos += 32;
    bool mariko = is_mariko();
    print_info(24, y_pos, "SoC Type", mariko ? "Mariko (T210B01)" : "Erista (T210)");

    // Read burnt fuses (display early for user info)
    y_pos += 32;
    u8 burnt_fuses = get_burnt_fuses();
    s_printf(temp, "%d fuses", burnt_fuses);
    print_info(24, y_pos, "Burnt Fuses", temp);

    y_pos += 48;

    // Extract warmboot (Mariko only - Erista uses embedded warmboot)
    if (mariko) {
        print_status(24, y_pos, "Extracting warmboot firmware from Package1...", COLOR_WHITE);
        y_pos += 32; 

        wb_extract_error_t err = extract_warmboot_from_pkg1_ex(&wb_info);
        if (err != WB_SUCCESS) {
            print_status(24, y_pos, "Failed to extract warmboot!", COLOR_RED);
            y_pos += 32;

            // Show specific error
            SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
            gfx_con_setpos(24, y_pos);
            gfx_printf("Error code: ");
            SETCOLOR(COLOR_ORANGE, COLOR_DEFAULT);
            gfx_printf("%d", err);
            RESETCOLOR;
            y_pos += 16;
            SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
            gfx_con_setpos(24, y_pos);
            gfx_printf("Details: ");
            SETCOLOR(COLOR_ORANGE, COLOR_DEFAULT);
            gfx_printf("%s", wb_error_to_string(err));
            RESETCOLOR;
            y_pos += 48;

            goto wait_exit;
        }
    } else {
        // Erista: Skip extraction - Atmosphere uses embedded warmboot
        print_status(24, y_pos, "Erista detected - warmboot is embedded in Atmosphere", COLOR_WHITE);
        y_pos += 32;
        print_status(24, y_pos, "No extraction needed for Erista consoles", COLOR_CYAN);
        goto wait_exit;
    }

    print_status(24, y_pos, "Warmboot extracted successfully!", COLOR_GREEN);
    y_pos += 48;

    // Calculate expected fuses (what Atmosphere uses for naming)
    u32 expected_fuses = get_expected_fuse_version(wb_info.target_firmware);

    // Display warmboot information
    SETCOLOR(COLOR_CYAN, COLOR_DEFAULT);
    gfx_con_setpos(24, y_pos);
    gfx_printf("Warmboot Information:");
    RESETCOLOR;
    y_pos += 32;

    s_printf(temp, "0x%X (%d bytes)", wb_info.size, wb_info.size);
    print_info(24, y_pos, "Size", temp);
    y_pos += 32;

    // Display target firmware
    if (wb_info.target_firmware != 0) {
        s_printf(temp, "0x%04X (%c%c%c%c/%c%c/%c%c)", 
                 wb_info.target_firmware,
                 wb_info.pkg1_date[0], wb_info.pkg1_date[1], wb_info.pkg1_date[2], wb_info.pkg1_date[3],
                 wb_info.pkg1_date[4], wb_info.pkg1_date[5],
                 wb_info.pkg1_date[6], wb_info.pkg1_date[7]);
        print_info(24, y_pos, "Target Firmware", temp);
        y_pos += 32;
    } else {
        print_info(24, y_pos, "Target Firmware", "Unknown (new FW?)");
        y_pos += 32;
        SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
        gfx_con_setpos(24, y_pos);
        gfx_printf("Note: Firmware not recognized, using burnt fuses for naming.");
        y_pos += 16;
        gfx_con_setpos(24, y_pos);
        gfx_printf("Extraction still works - warmboot binary is valid.");
        RESETCOLOR;
        y_pos += 32;
    }

    // Display fuse information with clear comparison
    y_pos += 16;
    SETCOLOR(COLOR_CYAN, COLOR_DEFAULT);
    gfx_con_setpos(24, y_pos);
    gfx_printf("Fuse Information (Critical for naming):");
    RESETCOLOR;
    y_pos += 32;

    s_printf(temp, "%d (0x%02X)", wb_info.burnt_fuses, wb_info.burnt_fuses);
    print_info(24, y_pos, "Burnt Fuses (device)", temp);
    y_pos += 32;

    s_printf(temp, "%d (0x%02X)", expected_fuses, expected_fuses);
    print_info(24, y_pos, "Expected Fuses (FW)", temp);
    y_pos += 32;

    // Show naming comparison
    if (wb_info.burnt_fuses == expected_fuses) {
        SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
        gfx_con_setpos(24, y_pos);
        gfx_printf("MATCH: ");
        SETCOLOR(COLOR_GREEN, COLOR_DEFAULT);
        gfx_printf("This script and AMS will use same filename!");
        RESETCOLOR;
        y_pos += 32;
    } else if (wb_info.burnt_fuses > expected_fuses) {
        SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
        gfx_con_setpos(24, y_pos);
        gfx_printf("DOWNGRADE: burnt(");
        SETCOLOR(COLOR_ORANGE, COLOR_DEFAULT);
        gfx_printf("%d", wb_info.burnt_fuses);
        SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
        gfx_printf(") > expected(");
        SETCOLOR(COLOR_ORANGE, COLOR_DEFAULT);
        gfx_printf("%d", expected_fuses);
        SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
        gfx_printf(")");
        RESETCOLOR;
        y_pos += 16;
        gfx_con_setpos(24, y_pos);
        SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
        gfx_printf("  AMS saves as: wb_%02x.bin (expected fuses)", expected_fuses);
        y_pos += 16;
        gfx_con_setpos(24, y_pos);
        gfx_printf("  This saves:   wb_%02x.bin (burnt fuses)", wb_info.burnt_fuses);
        RESETCOLOR;
        y_pos += 32;
    } else {
        SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
        gfx_con_setpos(24, y_pos);
        gfx_printf("WARNING: burnt(");
        SETCOLOR(COLOR_RED, COLOR_DEFAULT);
        gfx_printf("%d", wb_info.burnt_fuses);
        SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
        gfx_printf(") < expected(");
        SETCOLOR(COLOR_RED, COLOR_DEFAULT);
        gfx_printf("%d", expected_fuses);
        SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
        gfx_printf(") - unusual!");
        RESETCOLOR;
        y_pos += 32;
    }

    // Check warmboot metadata
    warmboot_metadata_t *meta = (warmboot_metadata_t *)(wb_info.data + 4);
    if (meta->magic == 0x30544257) {  // "WBT0"
        print_info(24, y_pos, "Metadata Magic", "WBT0 (Valid)");
        y_pos += 32;
    }

    y_pos += 16;

    // Save warmboot to SD using burnt fuse count
    // NOTE: Atmosphere uses EXPECTED fuses for naming when saving from Package1
    // But when loading for downgraded consoles, it searches starting from BURNT fuses
    // So saving with burnt fuses ensures the file is found when needed
    get_warmboot_path(path, sizeof(path), wb_info.burnt_fuses);

    print_status(24, y_pos, "Saving warmboot to SD card...", COLOR_WHITE);
    y_pos += 32;

    // Show both paths for clarity
    gfx_con_setpos(24, y_pos);
    SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
    gfx_printf("Saving to: ");
    SETCOLOR(COLOR_CYAN, COLOR_DEFAULT);
    gfx_printf("wb_%02x.bin", wb_info.burnt_fuses);
    SETCOLOR(COLOR_WHITE, COLOR_DEFAULT);
    gfx_printf(" (burnt fuses)");
    RESETCOLOR;
    y_pos += 32;

    if (!save_warmboot_to_sd(&wb_info, path)) {
        print_status(24, y_pos, "Failed to save warmboot to SD!", COLOR_RED);
        goto cleanup_exit;
    }

    print_status(24, y_pos, "Warmboot saved successfully!", COLOR_GREEN);
    y_pos += 48;

    // Display success summary
    y_pos += 16;
    SETCOLOR(COLOR_CYAN, COLOR_DEFAULT);
    print_centered(y_pos, "EXTRACTION COMPLETED SUCCESSFULLY");
    RESETCOLOR;
    y_pos += 32;

cleanup_exit:
    // Free warmboot data
    if (wb_info.data)
        free(wb_info.data);

wait_exit:
    // Footer
    SETCOLOR(COLOR_RED, COLOR_DEFAULT);
    print_centered(650, "Power: Back to Hekate | 3-Finger: Screenshot");
    RESETCOLOR;

    // Initialize touchscreen for 3-finger screenshot support
    touch_power_on();

    u32 btn_last = btn_read();
    bool touch_active = false;  // Track if we're currently in a touch event
    bool fingers_released = true;  // Track if fingers were released before new touch

    while (true)
    {
        // Poll for touch events (non-blocking)
        touch_event touch = {0};  // Initialize to zero to prevent garbage data
        touch_poll(&touch);

        // Check for 3-finger touch screenshot - only if fingers were released first
        if (touch.touch && touch.fingers >= 3 && fingers_released)
        {
            // Only process if this is a new touch (not already active)
            if (!touch_active)
            {
                touch_active = true;
                fingers_released = false;  // Mark that we've captured this touch
                // Wait a moment to confirm the touch
                msleep(150);

                if (!save_fb_to_bmp())
                {
                    SETCOLOR(COLOR_GREEN, COLOR_DEFAULT);
                    print_centered(680, "Screenshot saved!");
                    RESETCOLOR;
                    msleep(800);
                    // Clear the message
                    print_centered(680, "                     ");
                }
                else
                {
                    SETCOLOR(COLOR_RED, COLOR_DEFAULT);
                    print_centered(680, "Screenshot failed!");
                    RESETCOLOR;
                    msleep(800);
                    // Clear the message
                    print_centered(680, "                      ");
                }
            }
        }
        else if (!touch.touch)
        {
            // Touch has been released - reset flags
            touch_active = false;
            fingers_released = true;
        }

        // Non-blocking button read
        u32 btn = btn_read();

        // Only process button presses (ignore button releases and repeats)
        if (btn == btn_last)
        {
            msleep(10);
            continue;
        }

        btn_last = btn;

        // Ignore button releases (when btn becomes 0)
        if (!btn)
        {
            msleep(10);
            continue;
        }

        // Power button: go back to Hekate (same as vol-)
        if (btn & BTN_POWER)
        {
            break;
        }

        msleep(10);
    }

    // Launch bootloader/update.bin instead of reboot
    FILINFO fno;
    if (!f_stat("sd:/bootloader/update.bin", &fno)) {
        // Launch update.bin (Hekate)
        payload_path = "sd:/bootloader/update.bin";
    } else if (!f_stat("sd:/payload.bin", &fno)) {
        // Fallback to payload.bin
        payload_path = "sd:/payload.bin";
    } else {
        // No payload found, just reboot
        power_set_state(POWER_OFF_REBOOT);
        while (true) bpmp_halt();
    }

    // Launch payload
    launch_payload(payload_path);

    // If launch fails, reboot
    power_set_state(POWER_OFF_REBOOT);
    while (true)
        bpmp_halt();
}

// Main entry point
void ipl_main(void) {
    // Initialize hardware
    b_cfg.boot_cfg = BOOT_CFG_AUTOBOOT_EN;
    b_cfg.extra_cfg = 0;

    hw_init();
    pivot_stack(IPL_STACK_TOP);
    heap_init(IPL_HEAP_START);
    set_default_configuration();

    // Initialize display (horizontal mode)
    display_init();
    u32 *fb = display_init_framebuffer_pitch();
    gfx_init_ctxt(fb, 720, 1280, 720);  // Horizontal: 1280x720
    gfx_con_init();
    display_backlight_pwm_init();
    display_backlight_brightness(100, 1000);

    // Initialize SD card
    if (!sd_mount()) {
        gfx_printf("ERROR: Failed to mount SD card!\n");
        gfx_printf("Press any button to exit...\n");
        btn_wait();
        goto exit;
    }

    // Run warmboot extraction
    warmboot_extraction_workflow();

    // Unmount SD
    sd_unmount();

exit:
    // Launch bootloader/update.bin instead of reboot
    FILINFO fno;
    if (!f_stat("sd:/bootloader/update.bin", &fno)) {
        // Launch update.bin (Hekate)
        payload_path = "sd:/bootloader/update.bin";
    } else if (!f_stat("sd:/payload.bin", &fno)) {
        // Fallback to payload.bin
        payload_path = "sd:/payload.bin";
    } else {
        // No payload found, just reboot
        power_set_state(POWER_OFF_REBOOT);
        while (true) bpmp_halt();
    }

    // Launch payload
    launch_payload(payload_path);

    // If launch fails, reboot
    power_set_state(POWER_OFF_REBOOT);
    while (true)
        bpmp_halt();
}
