#!/bin/bash
set -e

usage() {
    echo "Usage: $0 --build-dir <build_dir> --snr <serial_number>"
    echo "  --build-dir   Path to the build directory containing domains.yaml"
    echo "  --snr         Device serial number"
    exit 1
}

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --snr)       SNR="$2";       shift 2 ;;
        *) echo "Unknown argument: $1"; usage ;;
    esac
done

# Validate
[ -z "$BUILD_DIR" ] && { echo "ERROR: --build-dir is required"; usage; }
[ -z "$SNR" ]       && { echo "ERROR: --snr is required";       usage; }

DOMAINS_YAML="$BUILD_DIR/domains.yaml"
[ ! -f "$DOMAINS_YAML" ] && { echo "ERROR: domains.yaml not found at $DOMAINS_YAML"; exit 1; }

echo "Using build dir: $BUILD_DIR"
echo "Serial number:   $SNR"

# Extract flash_order from domains.yaml
FLASH_ORDER=$(python3 -c "
import yaml
with open('$DOMAINS_YAML') as f:
    d = yaml.safe_load(f)
for domain in d['flash_order']:
    print(domain)
")

echo "Unlocking device..."
nrfutil device recover --serial-number $SNR

echo "Erasing device..."
nrfutil device erase --serial-number $SNR

# Provision KMU public key BEFORE flashing (only if the build produced one).
if [ -f "$BUILD_DIR/keyfile.json" ]; then
    echo "Provisioning KMU keys -> $BUILD_DIR/keyfile.json"
    nrfutil device x-provision-keys --key-file "$BUILD_DIR/keyfile.json" --serial-number $SNR
fi

for DOMAIN in $FLASH_ORDER; do
    # dfu_extra_<N> is a pseudo-image (the Wi-Fi patch = MCUboot image 1). Its
    # signed hex lives in the build root as ext_img<N>.signed.hex, placed at the
    # slot address. This MUST be flashed or MCUboot (2 images) refuses to boot.
    if [[ "$DOMAIN" == dfu_extra_* ]]; then
        N="${DOMAIN##*_}"
        HEX="$BUILD_DIR/ext_img${N}.signed.hex"
    # Precedence: NSIB-signed -> top-level image hex -> MCUboot-signed app -> plain.
    elif [ -f "$BUILD_DIR/signed_by_b0_$DOMAIN.hex" ]; then
        HEX="$BUILD_DIR/signed_by_b0_$DOMAIN.hex"
    elif [ -f "$BUILD_DIR/$DOMAIN.hex" ]; then
        HEX="$BUILD_DIR/$DOMAIN.hex"
    elif [ -f "$BUILD_DIR/$DOMAIN/zephyr/zephyr.signed.hex" ]; then
        HEX="$BUILD_DIR/$DOMAIN/zephyr/zephyr.signed.hex"
    else
        HEX="$BUILD_DIR/$DOMAIN/zephyr/zephyr.hex"
    fi

    if [ ! -f "$HEX" ]; then
        echo "WARNING: no hex for $DOMAIN ($HEX), skipping..."; continue
    fi
    echo "Flashing $DOMAIN -> $HEX"
    nrfutil device program --firmware "$HEX" --serial-number $SNR --options chip_erase_mode=ERASE_NONE
done

echo "Resetting device..."
nrfutil device reset --serial-number $SNR
echo "Done!"
