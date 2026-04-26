#!/bin/bash
set -e

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}=== Phase 2: Inject e1000 NUMA Policy ===${NC}"

read -p "Enter the absolute path to your Linux kernel source code: " KDIR

if [ ! -d "$KDIR" ] || [ ! -f "$KDIR/Makefile" ]; then
    echo -e "${RED}Error: Invalid Linux kernel directory at $KDIR.${NC}"
    exit 1
fi

PATCH_BASE_DIR="./kernel_files"
POLICIES=($(find "$PATCH_BASE_DIR" -mindepth 1 -maxdepth 1 -type d ! -name "common_kernel_files"))

if [ ${#POLICIES[@]} -eq 0 ]; then
    echo -e "${RED}Error: No policy directories found in $PATCH_BASE_DIR.${NC}"
    exit 1
fi

echo -e "\n${YELLOW}Available NUMA Policies:${NC}"
for i in "${!POLICIES[@]}"; do
    echo "  [$i] $(basename "${POLICIES[$i]}")"
done

read -p "Select the policy to apply (0-$((${#POLICIES[@]}-1))): " POLICY_IDX

if [[ ! "$POLICY_IDX" =~ ^[0-9]+$ ]] || [ "$POLICY_IDX" -ge "${#POLICIES[@]}" ]; then
    echo -e "${RED}Error: Invalid selection.${NC}"
    exit 1
fi

SELECTED_POLICY="${POLICIES[$POLICY_IDX]}"
E1000_FILE="$SELECTED_POLICY/e1000_main.c"
E1000_DEST="drivers/net/ethernet/intel/e1000/e1000_main.c"

if [ ! -f "$E1000_FILE" ]; then
    echo -e "${RED}Error: e1000_main.c not found in $SELECTED_POLICY.${NC}"
    exit 1
fi

cp "$E1000_FILE" "$KDIR/$E1000_DEST"
echo -e "  ${GREEN}[Copied]${NC} $(basename "$SELECTED_POLICY")/e1000_main.c -> $E1000_DEST"

echo -e "\n${CYAN}Policy applied successfully.${NC}"
read -p "Would you like me to start the 'make -j$(nproc)' process now? (y/n): " RUN_MAKE

if [[ "$RUN_MAKE" =~ ^[Yy]$ ]]; then
    cd "$KDIR"
    echo -e "${YELLOW}Starting build...${NC}"
    make -j$(nproc)
    echo -e "\n${GREEN}Build complete. Next steps:${NC}"
    echo -e "  First go to your linux source code folder"
    echo -e "  sudo make modules_install"
    echo -e "  sudo make install"
    echo -e "  sudo reboot"
else
    echo -e "Exiting. Build manually when ready."
fi