#
# Copyright (c) 2026 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

# Build the Wi-Fi LMAC/UMAC ROM-patch sub-blobs, merge them (with padding) into
# one image-1 payload, and register it so NCS signs it as MCUboot image 1
# (slot2/slot3) and adds it to merged.hex and the DFU packages.
#
# A single WIFI_PATCH_VERSION drives both the integer the patch reports at
# runtime and the MCUboot image version used for signing, so v1 vs v2 is just:
#   west build ... -- -DWIFI_PATCH_VERSION=2

set(WIFI_PATCH_VERSION "1" CACHE STRING
    "Wi-Fi patch version: printed by the patch and used as its MCUboot image version")

# The two-image partition layout must be identical for every image that resolves
# partitions: MCUboot maps slot0..slot3, and the helper images read the patch /
# boot partitions. The application picks up boards/<board>.overlay automatically;
# apply the same overlay to the other images so they all agree (DTS-only, no PM).
include(${ZEPHYR_NRF_MODULE_DIR}/sysbuild/extensions.cmake)
set(wifi_patch_overlay ${CMAKE_CURRENT_LIST_DIR}/boards/nrf7120dk_nrf7120_cpuapp.overlay)
foreach(img mcuboot dfu_extra_1 uicr wicr)
  add_overlay_dts(${img} ${wifi_patch_overlay})
endforeach()

# LMAC and UMAC can be versioned independently; each defaults to
# WIFI_PATCH_VERSION. e.g. -DLMAC_VERSION=3 -DUMAC_VERSION=2 (both still ship in
# the one image-1 blob; the MCUboot image version stays WIFI_PATCH_VERSION).
if(NOT DEFINED LMAC_VERSION)
  set(LMAC_VERSION ${WIFI_PATCH_VERSION})
endif()
if(NOT DEFINED UMAC_VERSION)
  set(UMAC_VERSION ${WIFI_PATCH_VERSION})
endif()

set(patch_src_dir ${CMAKE_CURRENT_LIST_DIR}/wifi_patch)
set(patch_build_dir ${CMAKE_BINARY_DIR}/wifi_patch)
file(MAKE_DIRECTORY ${patch_build_dir})

# Locate a bare-metal ARM toolchain for the patch blobs the same way the image
# builds do, so no env/flags are needed when a normal NCS build works. Order:
# already-resolved var -> env -> the CMake package registry (find_package, what
# Zephyr itself uses) -> common NCS / standalone SDK locations.
set(_sdk_dir "${ZEPHYR_SDK_INSTALL_DIR}")
if(NOT _sdk_dir)
  set(_sdk_dir "$ENV{ZEPHYR_SDK_INSTALL_DIR}")
endif()
if(NOT _sdk_dir)
  find_package(Zephyr-sdk QUIET)
  set(_sdk_dir "${ZEPHYR_SDK_INSTALL_DIR}")
endif()
if(NOT _sdk_dir)
  file(GLOB _sdk_glob
       $ENV{HOME}/ncs/toolchains/*/opt/zephyr-sdk
       /opt/nordic/ncs/toolchains/*/opt/zephyr-sdk
       $ENV{HOME}/zephyr-sdk-*
       /opt/zephyr-sdk-*)
  if(_sdk_glob)
    list(GET _sdk_glob 0 _sdk_dir)
  endif()
endif()

find_program(PATCH_CC NAMES arm-zephyr-eabi-gcc arm-none-eabi-gcc
             HINTS ${_sdk_dir} ENV ZEPHYR_SDK_INSTALL_DIR ENV GNUARMEMB_TOOLCHAIN_PATH
             PATH_SUFFIXES arm-zephyr-eabi/bin bin)
get_filename_component(_tc_bin "${PATCH_CC}" DIRECTORY)
find_program(PATCH_OBJCOPY NAMES arm-zephyr-eabi-objcopy arm-none-eabi-objcopy
             HINTS ${_tc_bin} ${_sdk_dir} PATH_SUFFIXES arm-zephyr-eabi/bin bin)
find_program(PATCH_MERGEHEX mergehex)

if(NOT PATCH_CC OR NOT PATCH_OBJCOPY OR NOT PATCH_MERGEHEX)
  message(FATAL_ERROR
    "wifi_patch_dfu: could not locate the patch-blob build tools.\n"
    "  arm gcc:  ${PATCH_CC}\n  objcopy:  ${PATCH_OBJCOPY}\n  mergehex: ${PATCH_MERGEHEX}\n"
    "Build inside your NCS toolchain environment (so the Zephyr SDK is on PATH and "
    "ZEPHYR_SDK_INSTALL_DIR is set), or pass -DZEPHYR_SDK_INSTALL_DIR=<path>. "
    "mergehex ships with the nRF Command Line Tools.")
endif()

set(patch_cflags
    -mcpu=cortex-m33 -mthumb -Os
    -ffreestanding -nostdlib -nostartfiles
    -fno-pic -fno-pie -ffunction-sections)

# Compile one sub-blob (stem.elf, linked by ldscript) and emit absolute-addressed
# Intel HEX so mergehex can place it correctly. The version is written to a
# per-blob header that is regenerated every configure, so changing a version
# forces that blob to rebuild.
function(wifi_patch_build_blob stem ldscript label version)
  set(ver_h ${patch_build_dir}/${stem}_version.h)
  file(WRITE ${ver_h} "#define WIFI_PATCH_VERSION ${version}\n")
  add_custom_command(
    OUTPUT ${patch_build_dir}/${stem}.hex
    COMMAND ${PATCH_CC} ${patch_cflags}
            -include ${ver_h}
            -DPATCH_NAME=${label}
            -T ${patch_src_dir}/${ldscript}
            ${patch_src_dir}/patch.c
            -o ${patch_build_dir}/${stem}.elf
    COMMAND ${PATCH_OBJCOPY} -O ihex
            ${patch_build_dir}/${stem}.elf ${patch_build_dir}/${stem}.hex
    DEPENDS ${patch_src_dir}/patch.c ${patch_src_dir}/${ldscript} ${ver_h}
    COMMENT "wifi_patch_dfu: building ${label} patch blob (v${version})"
    VERBATIM)
endfunction()

wifi_patch_build_blob(lmac lmac.ld LMAC ${LMAC_VERSION})
wifi_patch_build_blob(umac umac.ld UMAC ${UMAC_VERSION})

# Merge LMAC + UMAC (each absolute-addressed) into the single image-1 payload.
# objcopy ihex->binary starts the .bin at the lowest address (LMAC origin) and
# fills the LMAC->UMAC gap with 0xFF, so each sub-blob lands at its origin once
# imgtool prepends the 0x800 MCUboot header.
set(patch_bin ${CMAKE_BINARY_DIR}/wifi_patch.bin)
add_custom_command(
  OUTPUT ${patch_bin}
  COMMAND ${PATCH_MERGEHEX} -m
          ${patch_build_dir}/lmac.hex ${patch_build_dir}/umac.hex
          -o ${patch_build_dir}/wifi_patch_combined.hex
  COMMAND ${PATCH_OBJCOPY} -I ihex -O binary --gap-fill 0xFF
          ${patch_build_dir}/wifi_patch_combined.hex ${patch_bin}
  DEPENDS ${patch_build_dir}/lmac.hex ${patch_build_dir}/umac.hex
  COMMENT "wifi_patch_dfu: merging LMAC+UMAC into the image-1 payload"
  VERBATIM)
add_custom_target(wifi_patch_blob ALL DEPENDS ${patch_bin})

# Register the merged blob as MCUboot image 1. NCS signs it against
# slot2_partition (--header-size CONFIG_ROM_START_OFFSET=0x800) and adds it
# (image_index 1) to dfu_multi_image.bin / the DFU zip.
#
# NAME must be "ext_img1": with SB_CONFIG_MCUBOOT_EXTRA_IMAGES on (and no
# Partition Manager), NCS auto-creates an image_flasher domain that programs
# ext_img1.signed.hex to slot2 during `west flash`. That filename is fixed, so
# the signed output must match it for initial provisioning to work.
include(${ZEPHYR_NRF_MODULE_DIR}/cmake/dfu_extra.cmake)
dfu_extra_add_binary(
  BINARY_PATH ${patch_bin}
  NAME        "ext_img1"
  VERSION     "${WIFI_PATCH_VERSION}.0.0+0"
  PACKAGE_TYPE all
  DEPENDS     wifi_patch_blob)
