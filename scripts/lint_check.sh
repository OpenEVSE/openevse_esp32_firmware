#!/bin/bash
# Lint check script for OpenEVSE ESP32 Firmware
# Runs clang-tidy on source files to check naming conventions
#
# Note: This script may report warnings in legacy code that doesn't follow
# current naming conventions. Focus on ensuring NEW code follows conventions.
# See NAMING_CONVENTIONS.md for details.

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo "OpenEVSE ESP32 Firmware - Code Linting"
echo "======================================="
echo -e "${BLUE}Note: Warnings in legacy code are informational.${NC}"
echo -e "${BLUE}Focus on ensuring NEW code follows conventions.${NC}"
echo ""

# Check if clang-tidy is installed
if ! command -v clang-tidy &> /dev/null; then
    echo -e "${RED}Error: clang-tidy is not installed${NC}"
    echo "Install it with: sudo apt-get install clang-tidy"
    exit 1
fi

echo "Using clang-tidy version: $(clang-tidy --version | head -n1)"

# Build include paths
INCLUDES="-Isrc -Ilib"

# Add library dependencies if available
if [ -d ".pio/libdeps/openevse_wifi_v1" ]; then
    echo "Found PlatformIO library dependencies"
    for dir in .pio/libdeps/openevse_wifi_v1/*/; do
        INCLUDES="$INCLUDES -I$dir"
        if [ -d "$dir/src" ]; then
            INCLUDES="$INCLUDES -I$dir/src"
        fi
    done
fi

# Add ESP32 Arduino core includes if available
if [ -d "$HOME/.platformio/packages/framework-arduinoespressif32" ]; then
    INCLUDES="$INCLUDES -I$HOME/.platformio/packages/framework-arduinoespressif32/cores/esp32"
fi

# Determine which files to check
if [ $# -eq 0 ]; then
    # No arguments - check all source files
    echo "Checking all C++ source files in src/"
    FILES=$(find src -name "*.cpp" -o -name "*.h")
else
    # Check specified files
    echo "Checking specified files: $@"
    FILES="$@"
fi

# Count files
FILE_COUNT=$(echo "$FILES" | wc -l)
echo "Files to check: $FILE_COUNT"
echo ""

# Run clang-tidy on each file
PASSED=0
FAILED=0
FAILED_FILES=""

for file in $FILES; do
    if [ ! -f "$file" ]; then
        echo -e "${YELLOW}Warning: File not found: $file${NC}"
        continue
    fi
    
    echo -n "Checking $(basename $file)... "
    
    # Run clang-tidy and capture output
    if clang-tidy "$file" -- $INCLUDES -DARDUINO=10819 -DESP32 -std=gnu++11 2>&1 | grep -q "warning:"; then
        echo -e "${RED}FAILED${NC}"
        FAILED=$((FAILED + 1))
        FAILED_FILES="$FAILED_FILES\n  - $file"
        
        # Show the warnings
        echo "  Issues found:"
        clang-tidy "$file" -- $INCLUDES -DARDUINO=10819 -DESP32 -std=gnu++11 2>&1 | grep "warning:" | sed 's/^/    /'
        echo ""
    else
        echo -e "${GREEN}PASSED${NC}"
        PASSED=$((PASSED + 1))
    fi
done

# Print summary
echo ""
echo "======================================="
echo "Summary:"
echo -e "  ${GREEN}Passed: $PASSED${NC}"
echo -e "  ${RED}Failed: $FAILED${NC}"

if [ $FAILED -gt 0 ]; then
    echo ""
    echo -e "${RED}Files with issues:${NC}"
    echo -e "$FAILED_FILES"
    echo ""
    echo "Please review NAMING_CONVENTIONS.md for naming guidelines"
    exit 1
else
    echo ""
    echo -e "${GREEN}All files passed linting checks!${NC}"
    exit 0
fi
