#!/bin/bash
set -e

GREEN='\033[0;32m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}=== Phase 1: Deploy Common Kernel Files ===${NC}"

read -p "Enter the absolute path from your home to your Linux kernel source code: " KDIR

example : /home/dnyanesh/linux-6.1.4

if [ ! -d "$KDIR" ] || [ ! -f "$KDIR/Makefile" ]; then
    echo -e "${RED}Error: Invalid Linux kernel directory at $KDIR.${NC}"
    exit 1
fi

COMMON_DIR="./kernel_files/common_kernel_files"
if [ ! -d "$COMMON_DIR" ]; then
    echo -e "${RED}Error: Common files directory not found at $COMMON_DIR.${NC}"
    exit 1
fi

declare -A FILE_MAPPINGS=(
    ["rx_timing.h"]="include/net/rx_timing.h"
    ["rx_timing.c"]="net/core/rx_timing.c"
    ["Makefile"]="net/core/Makefile"
    ["gro.c"]="net/core/gro.c"
    ["dev.c"]="net/core/dev.c"
    ["ip_input.c"]="net/ipv4/ip_input.c"
    ["tcp_ipv4.c"]="net/ipv4/tcp_ipv4.c"
    ["tcp_input.c"]="net/ipv4/tcp_input.c"
    ["tcp.c"]="net/ipv4/tcp.c"
    ["e1000_main.c"]="drivers/net/ethernet/intel/e1000/e1000_main.c"
)

echo -e "Preparing the Kernel $KDIR..."
for FILE in "${!FILE_MAPPINGS[@]}"; do
    SRC="$COMMON_DIR/$FILE"
    DEST="$KDIR/${FILE_MAPPINGS[$FILE]}"
    
    if [ -f "$SRC" ]; then
        cp "$SRC" "$DEST"
        echo -e "  ${GREEN}[Copied]${NC} $FILE -> ${FILE_MAPPINGS[$FILE]}"
    else
        echo -e "  ${RED}[Missing]${NC} $FILE not found in $COMMON_DIR!"
        exit 1
    fi
done

echo -e "\n${GREEN}Preparation done successfully. You can now run the policy script.${NC}"
