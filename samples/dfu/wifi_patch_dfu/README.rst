.. _wifi_patch_dfu:

nRF7120: Wi-Fi patch DFU
########################

.. contents::
   :local:
   :depth: 2

Overview
********

This proof of concept demonstrates that the nRF7120 Wi-Fi LMAC and UMAC ROM
patches can be field-updated through MCUboot as a separate image, independently
of the application, over a serial (UART) transport - no debugger required.

The Wi-Fi patches are kept out of the application binary (a licensing
requirement: the UMAC patch is GPL) and packaged as their own MCUboot image:

* **Image 0** - the application, ``slot0_partition`` / ``slot1_partition``.
* **Image 1** - one signed blob carrying the LMAC and UMAC sub-applications,
  ``slot2_partition`` / ``slot3_partition``.

MCUboot validates and swaps image 1 but never boots it (it only boots image 0).
On real silicon the Wi-Fi VPR executes the patch in place; this PoC has no VPR,
so the application itself calls each patch entry function at its fixed origin
and prints the version it returns. After a DFU update of image 1 and a reboot,
the printed version changes, proving the patch was updated via MCUboot.

Memory layout (image-1 primary slot)
====================================

::

   slot2 base 0x3D2000
     + 0x0800   MCUboot header (CONFIG_ROM_START_OFFSET on nRF7120)
     0x3D2800   LMAC sub-application   (wifi_lmac_patch_partition, 16K)
     0x3D6800   UMAC sub-application   (wifi_umac_patch_partition, 16K)

The patch image is a single signed blob built from two independently linked
sub-blobs (``wifi_patch/patch.c`` linked by ``lmac.ld`` / ``umac.ld``), merged
with ``mergehex`` so each lands at its fixed origin once the MCUboot header is
prepended.

Requirements
************

The nRF7120 DK (``nrf7120dk/nrf7120/cpuapp``). The PoC is intended for FPGA
bring-up. ``mergehex`` (nRF Command Line Tools) and the Zephyr SDK
``arm-zephyr-eabi`` toolchain must be on ``PATH`` for the patch-blob build.

Building
********

.. code-block:: console

   west build --sysbuild -b nrf7120dk/nrf7120/cpuapp nrf/samples/dfu/wifi_patch_dfu

The patch blobs are built with the same Zephyr SDK the images use (located via the
CMake package registry). If your SDK is not discoverable (no registry entry, env
unset), pass it explicitly - it is forwarded to every image:
``-- -DZEPHYR_SDK_INSTALL_DIR=$(ls -d ~/ncs/toolchains/*/opt/zephyr-sdk | head -1)``.

Build a v2 patch (the only change needed for an update) by bumping one flag:

.. code-block:: console

   west build --sysbuild -b nrf7120dk/nrf7120/cpuapp nrf/samples/dfu/wifi_patch_dfu \
       -- -DWIFI_PATCH_VERSION=2

``WIFI_PATCH_VERSION`` sets both the integer the patch reports at runtime and the
MCUboot image version used for signing. LMAC and UMAC can also be versioned
independently (both still ship in the one image-1 blob)::

   west build --sysbuild -b nrf7120dk/nrf7120/cpuapp nrf/samples/dfu/wifi_patch_dfu \
       -- -DLMAC_VERSION=3 -DUMAC_VERSION=2

Build artifacts
===============

nRF7120 is UICR-based and flashes per-image (no single ``merged.hex``):

* ``build/ext_img1.signed.bin`` / ``ext_img1.signed.hex`` - the signed image-1
  (Wi-Fi patch). The hex is placed at the slot2 address.
* ``build/dfu_multi_image.bin`` / ``build/dfu_application.zip`` - DFU packages
  containing image_index 0 (app) and 1 (patch); the patch file in the package is
  ``ext_img1.signed.bin``.
* Per-image hex files under ``build/<image>/zephyr/`` (mcuboot, app, uicr, wicr).

Testing
*******

#. Flash everything once via debugger and watch the console:

   .. code-block:: console

      west flash -d build

   ``west flash`` programs all domains in order (mcuboot, the image_flasher that
   writes the patch to slot2, uicr, wicr, app). Expected console:

   .. code-block:: console

      *** nRF7120 Wi-Fi patch DFU PoC ... ***
      Patch image (image 1) MCUboot version: 1.0.0+0, size ...
      LMAC entry @ 0x003d2800 -> Version Info: 1 (Hello World from LMAC)
      UMAC entry @ 0x003d6800 -> Version Info: 1 (Hello World from UMAC)

#. Build the v2 patch (above) and update image 1 over UART only:

   .. code-block:: console

      mcumgr --conntype serial --connstring "/dev/ttyACM0,baud=115200" image list
      mcumgr ... image upload -n 1 build/ext_img1.signed.bin
      mcumgr ... image list           # new hash now in image1 / slot 3
      mcumgr ... image test <hash>
      mcumgr ... reset

#. After the reboot, MCUboot swaps image 1 and the console shows
   ``Version Info: 2`` for both LMAC and UMAC. ``mcumgr image list`` shows the v2
   hash in the image-1 primary slot - bootloader-level proof of the swap.

To prove rollback, ``image test`` (do not let the app confirm) and reset twice;
MCUboot reverts image 1 and the version returns to ``1``.
