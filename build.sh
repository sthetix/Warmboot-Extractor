#!/bin/bash

# Warmboot Generator Build Script
# Sets up devkitARM environment and builds the payload

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${CYAN}════════════════════════════════════════════════════════════════${NC}"
echo -e "${CYAN}           Warmboot Generator Build Script${NC}"
echo -e "${CYAN}════════════════════════════════════════════════════════════════${NC}"
echo ""

# Check for devkitPro installation
if [ -z "$DEVKITPRO" ]; then
    if [ -d "/c/devkitPro" ]; then
        export DEVKITPRO=/c/devkitPro
        export DEVKITARM=/c/devkitPro/devkitARM
        echo -e "${GREEN}[✓]${NC} Found devkitPro at /c/devkitPro"
    elif [ -d "/opt/devkitpro" ]; then
        export DEVKITPRO=/opt/devkitpro
        export DEVKITARM=/opt/devkitpro/devkitARM
        echo -e "${GREEN}[✓]${NC} Found devkitPro at /opt/devkitpro"
    else
        echo -e "${RED}[✗]${NC} devkitPro not found!"
        echo -e "${YELLOW}    Please install devkitPro from:${NC}"
        echo -e "${YELLOW}    https://github.com/devkitPro/installer/releases${NC}"
        exit 1
    fi
else
    echo -e "${GREEN}[✓]${NC} Using DEVKITPRO: $DEVKITPRO"
fi

# Check for arm-none-eabi-gcc
if [ ! -f "$DEVKITARM/bin/arm-none-eabi-gcc" ]; then
    echo -e "${RED}[✗]${NC} arm-none-eabi-gcc not found in $DEVKITARM/bin"
    exit 1
fi

echo -e "${GREEN}[✓]${NC} Found arm-none-eabi-gcc"
echo ""

# Clean previous build
echo -e "${CYAN}[*]${NC} Cleaning previous build..."
make clean > /dev/null 2>&1 || true
echo -e "${GREEN}[✓]${NC} Clean complete"
echo ""

# Build tools
echo -e "${CYAN}[*]${NC} Building tools..."
make -C tools/lz > /dev/null 2>&1 || {
    echo -e "${RED}[✗]${NC} Failed to build lz77 tool"
    exit 1
}
echo -e "${GREEN}[✓]${NC} Built lz77 compressor"

make -C tools/bin2c > /dev/null 2>&1 || {
    echo -e "${RED}[✗]${NC} Failed to build bin2c tool"
    exit 1
}
echo -e "${GREEN}[✓]${NC} Built bin2c converter"
echo ""

# Build main payload
echo -e "${CYAN}[*]${NC} Building Warmboot Generator payload..."
make 2>&1 | tee build.log

if [ ${PIPESTATUS[0]} -eq 0 ]; then
    echo ""
    echo -e "${GREEN}════════════════════════════════════════════════════════════════${NC}"
    echo -e "${GREEN}              Build Successful!${NC}"
    echo -e "${GREEN}════════════════════════════════════════════════════════════════${NC}"
    echo ""

    if [ -f "loader/WarmbootGenerator.bin" ]; then
        SIZE=$(stat -c%s "loader/WarmbootGenerator.bin" 2>/dev/null || stat -f%z "loader/WarmbootGenerator.bin" 2>/dev/null)
        echo -e "${CYAN}Output:${NC}"
        echo -e "  ${GREEN}✓${NC} loader/WarmbootGenerator.bin (${SIZE} bytes)"
        echo ""
        echo -e "${CYAN}Installation:${NC}"
        echo -e "  Copy to: ${YELLOW}sd:/bootloader/payloads/WarmbootGenerator.bin${NC}"
        echo ""
    fi
else
    echo ""
    echo -e "${RED}════════════════════════════════════════════════════════════════${NC}"
    echo -e "${RED}              Build Failed!${NC}"
    echo -e "${RED}════════════════════════════════════════════════════════════════${NC}"
    echo ""
    echo -e "${YELLOW}Check build.log for details${NC}"
    exit 1
fi
