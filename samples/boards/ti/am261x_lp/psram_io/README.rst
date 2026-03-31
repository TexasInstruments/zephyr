.. _am261x_lp_psram_io:

AM261x LaunchPad PSRAM I/O Sample
##################################

Overview
********

This sample demonstrates PSRAM (Pseudo-Static RAM) read/write operations on the
TI AM261x LaunchPad board. The application performs the following operations:

1. Writes an array of 10 words (40 bytes) from SRAM to PSRAM
2. Reads back the data from PSRAM to a different SRAM location
3. Verifies data integrity by comparing source and readback data
4. Performs direct pointer access test to PSRAM base address

The PSRAM is accessed via OSPI1 interface.

Building and Running
********************

This sample can be built for the AM261x LaunchPad board.

Prerequisites
=============

Before running this sample, ensure that:

1. The SBL (Secondary Bootloader) has been configured to initialize OSPI1 for PSRAM
2. OSPI1 is configured with DAC mode enabled

Device Tree Configuration
=========================

PSRAM address and size are retrieved from the device tree at compile time using the
``psram`` alias defined in board overlays. To adapt this sample for different PSRAM
configurations, update the ``boards/*.overlay`` files to point the ``psram`` alias
to the appropriate memory region node.

Building
========

.. zephyr-app-commands::
   :zephyr-app: samples/boards/ti/am261x_lp/psram_io
   :board: am261x_lp/am2612/r5f0_0
   :goals: build
   :compact:

Sample Output
=============

.. code-block:: console

    ===========================================
      AM261x PSRAM I/O Sample Application
    ===========================================

    Memory Layout:
      SRAM Source:      0xa0000800
      PSRAM Destination: 0xa0000850
      SRAM Readback:    0xa0000828
      PSRAM Base Addr:  0xA0000000

    Source Data (SRAM):
    sram_src_data:
      [0] = 0xDEADBEEF
      [1] = 0xCAFEBABE
      [2] = 0x12345678
      [3] = 0x87654321
      [4] = 0xABCDEF00
      [5] = 0x00FEDCBA
      [6] = 0x55AA55AA
      [7] = 0xAA55AA55
      [8] = 0x11223344
      [9] = 0x99887766

    ------------------------------------------
    Test 1: Write SRAM -> PSRAM (linker section)
    ------------------------------------------
    Data written to PSRAM at 0xa0000850

    ------------------------------------------
    Test 2: Read PSRAM -> SRAM (readback)
    ------------------------------------------
    Data read back from PSRAM

    Readback Data (SRAM):
    sram_readback_data:
      [0] = 0xDEADBEEF
      [1] = 0xCAFEBABE
      [2] = 0x12345678
      [3] = 0x87654321
      [4] = 0xABCDEF00
      [5] = 0x00FEDCBA
      [6] = 0x55AA55AA
      [7] = 0xAA55AA55
      [8] = 0x11223344
      [9] = 0x99887766

    ------------------------------------------
    Verification: Compare Source vs Readback
    ------------------------------------------
    PASS: All data matches!

    ------------------------------------------
    Test 3: Direct Pointer Access to PSRAM
    ------------------------------------------
    Writing via direct pointer to 0xA0000000
    Reading back via direct pointer...
    PASS: Direct pointer access test passed!

    ===========================================
      ALL TESTS PASSED!
    ===========================================
