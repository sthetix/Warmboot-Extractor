# Warmboot Extractor - Project Summary

## What Was Created

A complete Nintendo Switch payload tool that extracts warmboot firmware from Package1 and caches it to SD card, enabling sleep mode on emuMMC with older Atmosphere versions after sysNAND firmware updates.

## Project Structure

```
warmboot-extractor/
├── source/
│   ├── main.c                          # Main entry point with horizontal GUI
│   ├── config.c/h                      # Configuration management
│   ├── link.ld                         # Linker script
│   ├── start.S                         # ARM7TDMI startup code
│   ├── warmboot/
│   │   ├── warmboot_extractor.c        # Package1 parsing and extraction
│   │   └── warmboot_extractor.h        # Warmboot API definitions
│   ├── frontend/
│   │   ├── gui.c/h                     # GUI framework
│   ├── gfx/
│   │   └── gfx_utils.h                 # Graphics utilities
│   ├── storage/
│   │   ├── nx_emmc.c/h                 # eMMC access (BOOT0)
│   │   ├── nx_sd.c/h                   # SD card access
│   │   └── emummc.c/h                  # emuMMC support
│   └── libs/
│       └── fatfs/                      # FAT filesystem library
│
├── bdk/                                # Board Development Kit (from Hekate)
│   ├── display/                        # Display initialization
│   ├── mem/                            # Memory management
│   ├── power/                          # Power management
│   ├── soc/                            # SoC-specific code
│   └── utils/                          # Utility functions
│
├── loader/                             # Payload loader
│   ├── loader.c                        # Loader implementation
│   ├── Makefile                        # Loader build script
│   └── link.ld                         # Loader linker script
│
├── tools/
│   ├── lz/                             # LZ77 compression tool
│   └── bin2c/                          # Binary to C array converter
│
├── Makefile                            # Main build system
├── Versions.inc                        # Version definitions
├── build.sh                            # Build helper script
├── README.md                           # Project overview
├── USAGE.md                            # Detailed usage guide
├── PROJECT_SUMMARY.md                  # This file
└── .gitignore                          # Git ignore rules
```

## Key Features Implemented

### 1. Warmboot Extraction (`warmboot_extractor.c`)
- Reads Package1 from BOOT0 at offset 0x100000
- Locates PK11 container (handles both 0x4000 and 0x7000 offsets)
- Skips known payloads (NX bootloader, secure monitor)
- Validates warmboot size (0x800-0x1000 bytes)
- Extracts warmboot metadata ("WBT0" magic)

### 2. Hardware Detection
- Mariko vs Erista detection via `fuse_read_dramid()`
- Burnt fuse counting from `FUSE_RESERVED_ODM4`
- SoC-aware cache path generation

### 3. Fuse-based Caching
- Generates cache filename: `wb_XX.bin` (XX=fuse count in lowercase hex)
- Creates directory: `sd:/warmboot_mariko/` (Mariko only - Erista uses embedded warmboot)
- Pre-caches next 5 fuse counts for future firmware updates
- Prevents duplicate caching (checks existing files)

### 4. User Interface
- Horizontal display support (1280x720)
- Color-coded status messages
- Real-time extraction progress
- Detailed warmboot information display
- System information (SoC type, fuse count)
- Error handling with helpful suggestions

### 5. Build System
- devkitARM-based Makefile
- Automated tool compilation (lz77, bin2c)
- Payload compression
- Size validation
- Build helper script with environment detection

## Technical Implementation

### Package1 Parsing Algorithm

```c
1. Read 256KB from BOOT0 @ 0x100000
2. Locate PK11 magic "PK11" at offset 0x4000 or 0x7000
3. Iterate through PK11 payloads:
   - If signature == 0xD5034FDF: Skip (NX bootloader)
   - If signature == 0xE328F0C0: Skip (Secure monitor)
   - If signature == 0xF0C0A7F0: Skip (Secure monitor variant)
   - Else: Found warmboot!
4. Validate warmboot size: 0x800 <= size < 0x1000
5. Extract warmboot binary
6. Verify "WBT0" metadata magic
```

### Fuse Count Calculation

```c
u8 get_burnt_fuses(void) {
    u32 fuses = fuse_read_odm(4);
    u8 count = 0;
    while (fuses) {
        count += fuses & 1;  // Count set bits
        fuses >>= 1;
    }
    return count;
}
```

### Cache Path Generation

```c
void get_warmboot_path(char *path, size_t path_size, u8 fuse_count) {
    // Format matches Atmosphère's convention: wb_XX.bin (lowercase hex)
    // Example: 22 fuses → wb_16.bin (0x16 = 22 decimal)

    if (is_mariko()) {
        s_printf(path, "sd:/warmboot_mariko/wb_%02x.bin", fuse_count);
    } else {
        // Erista: For backup/analysis only (not used by Atmosphère)
        s_printf(path, "sd:/warmboot_erista/wb_%02x.bin", fuse_count);
    }
}
```

## How It Solves the Problem

### The Problem
When you update sysNAND firmware:
1. New firmware burns more fuses (e.g., 21 → 22)
2. emuMMC with old Atmosphere can't find warmboot for fuse count 22
3. Sleep mode breaks
4. You must wait for new Atmosphere release just for warmboot cache

### The Solution
This tool:
1. Extracts warmboot immediately after firmware update
2. Saves to `sd:/warmboot_mariko/wb_16.bin` (22 fuses = 0x16 hex)
3. Old Atmosphere finds cached warmboot and uses it
4. Sleep mode works without waiting for Atmosphere update!

### Why It Works
Atmosphere's warmboot loading logic (`fusee_setup_horizon.cpp`):
```cpp
// If burnt fuses > expected, search cache
if (burnt_fuses > expected_fuses) {
    for (u32 attempt = burnt_fuses; attempt <= 32; ++attempt) {
        UpdateWarmbootPath(attempt);  // Try wb_16.bin, wb_17.bin, etc.
        if (LoadCachedWarmboot(warmboot_path)) {
            break;  // Found compatible warmboot!
        }
    }
}
```

## Build Instructions

### Prerequisites
```bash
# Windows (MSYS2)
export DEVKITPRO=/c/devkitPro
export DEVKITARM=/c/devkitPro/devkitARM

# Linux/macOS
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=/opt/devkitpro/devkitARM
```

### Build
```bash
cd D:\Coding\warmboot-extractor
./build.sh
# OR
make
```

### Output
```
loader/warmboot_extractor.bin  → Copy to sd:/bootloader/payloads/
```

## Usage Workflow

1. **Update sysNAND** to new firmware (e.g., 22.0.0)
2. **Launch tool** from Hekate payloads menu
3. **Tool extracts** warmboot from new firmware
4. **Tool saves** to `sd:/warmboot_mariko/wb_16.bin` (and wb_17-1b.bin for future)
5. **Boot emuMMC** with old Atmosphere
6. **Sleep works!** Old Atmosphere uses cached warmboot

## Testing Checklist

- [ ] Build completes without errors
- [ ] Payload size within limits (< 126KB)
- [ ] Tool boots on real hardware (Erista)
- [ ] Tool boots on real hardware (Mariko)
- [ ] BOOT0 reading works
- [ ] Package1 parsing works
- [ ] Warmboot extraction succeeds
- [ ] SD card writing works
- [ ] Cache directory creation works
- [ ] Fuse count detection accurate
- [ ] Pre-caching generates files
- [ ] Extracted warmboot validates ("WBT0" magic)
- [ ] Sleep mode works with cached warmboot

## Known Limitations

1. **Reads from sysNAND BOOT0 only**
   - Does not read from emuMMC boot0 file
   - This is intentional (you want new firmware's warmboot)

2. **Does not decrypt Package1 on Mariko**
   - Relies on plaintext warmboot in PK11 container
   - May fail on some firmware versions requiring BEK decryption

3. **Does not fix other incompatibilities**
   - Only fixes sleep mode
   - Old Atmosphere may still crash if kernel ABI changed

## Future Enhancements

- [ ] Add emuMMC boot0 extraction option
- [ ] Support Package1 BEK decryption for Mariko
- [ ] Add warmboot validation (SHA256 checksum)
- [ ] Show warmboot firmware version from metadata
- [ ] Add option to extract specific fuse count ranges
- [ ] GUI improvements (progress bar, animations)

## Credits

Based on code from:
- **Atmosphère-NX**: fusee_setup_horizon.cpp (warmboot extraction logic)
- **Hekate**: BDK library (hardware initialization)
- **FuseCheck**: Display framework and GUI structure

## License

GNU General Public License v2.0

---

**Created**: 2025-12-07
**Author**: Based on Atmosphère and Hekate projects
**Purpose**: Enable sleep mode on emuMMC without waiting for Atmosphere updates
