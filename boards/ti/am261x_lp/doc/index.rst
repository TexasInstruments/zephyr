.. zephyr:board:: am261x_lp

Overview
********

The AM261x LaunchPad is a development board based off a Sitara™ AM2612 MCU.
The SoC features 2 Cortex R5F cores that can run at a maximum of 500 MHz.
The board includes on-chip flash connected by OSPI, DIP-Switches for boot
mode selection and supports connection of up to two BoosterPack™ plug-in modules.

See the `TI AM261x Product Page`_ for details.

|

Hardware
*********
* Processor:

  + Dual Arm Cortex R5F CPU up to 500MHz

* Memory:

  + 1.5MB On-Chip Shared RAM
  + 2x Octal Serial Peripheral Interface (OSPI) up to 133MHz SDR and DDR

* Debug:

  + XDS110 based JTAG

Details present in `AM261x Sitara™ Microcontrollers datasheet (Rev. C)`_

|

Supported Features
==================

.. zephyr:board-supported-hw::

|

System Clock
============

The AM261x SoC is configured to use an RTI Timer as System Clock.

There are 4 RTIs on the board. Each RTI has 2 Counter Blocks
and 4 Counter Compare Blocks.
Counter 0, Compare Block 0 of the chosen RTI is used for System Clock.

The RTI Clock Source is fixed to SYS_CLK (250 MHz).
Refer to table 13-304 in TRM for list of clock sources and frequencies.

NOTE that RTI timer driver halves input frequency by default.
Thus, an input of 250 MHz to RTI would become 125 MHz.

|

Serial Port
===========

The AM261x SoC has 6 UART interfaces for serial communication.

UART 0 is configured as UART for the console.

Default connection settings are Baud Rate of 115200, no parity,
Data Size of 8, 1 Stop Bit.

|

McSPI
=====

The AM261x SoC has 4 McSPI interfaces.

In AM261x, each McSPI supports up to 2 CS lines.

However, the McSPI driver currently supports only single-channel transfer per McSPI interface.

|

Data Movement Architecture
**************************

AM261x SoC uses eDMA IP for Data Movement.

About eDMA:
===========

eDMA has 2 components:
    - Channel Controller (CC) that handles submitting requests to Transfer Controller (TC).
    - Transfer Controller (TC) is a state machine that handles data movement within SoC.

**> eDMA driver programs the Channel Controller (CC)**

eDMA IP has 2 critical resources that must be split among CPUs:
    - Channels  ( eDMA has 64 channels ).
        This refers to the data path within IP responsible for transferring data.
    - PaRAM sets ( eDMA has 256 PaRAM sets )
        Each PaRAM set refers to a portion of memory that contains DMA transfer configurations.

|

Configuration:
==============
eDMA resource configuration instructions found in ti,edma.yaml file.
    - Each CPU should configure the eDMA resources it needs in overlay files.
    - Each CPU should set region-id = CPU_number to ensure proper resource partitioning.
    - Queue-number refers to which Transfer Controller (TC) handles DMA transfer.

**> Ensure that 2 cores do NOT have conflicting resources.**

Say both Core0 and Core1 are allocated DMA channels 0 to 20.
It is possible at runtime for Core 0 to override Core 1's DMA channel configurations (and vice-versa).

**> It's the application developer's responsibility to ensure resource partitioning using appropriate overlay-files.**

|

Default Resource Partitioning:
==============================

AM261x SoC has partitioned eDMA resources equally among the 2 CPUs:

+--------------+--------+---------+
|              | R5F0_0 |  R5F0_1 |
+==============+========+=========+
| Channels     |  0-31  |  32-63  |
+--------------+--------+---------+
| PaRAM sets   |  0-127 | 128-255 |
+--------------+--------+---------+

|

Some IPs also have DMA channels configured by default:

+----------+--------------+--------------+
|          |    R5F0_0    |    R5F0_1    |
+==========+==============+==============+
| UART0    | Tx: 1  Rx: 2 | Tx: 33 Rx: 34|
+----------+--------------+--------------+
| UART1    | Tx: 3  Rx: 4 | Tx: 35 Rx: 36|
+----------+--------------+--------------+
| UART2    | Tx: 5  Rx: 6 | Tx: 37 Rx: 38|
+----------+--------------+--------------+
| UART3    | Tx: 7  Rx: 8 | Tx: 39 Rx: 40|
+----------+--------------+--------------+
| UART4    | Tx: 9  Rx:10 | Tx: 41 Rx: 42|
+----------+--------------+--------------+
| UART5    | Tx:11  Rx:12 | Tx: 43 Rx: 44|
+----------+--------------+--------------+

|

Flashing
********

The binary can be prepared using zephyr's west tool, .elf files are supported for flashing.

As a prerequisite, the board must contain a SBL.
To prepare and flash an SBL, you will require the MCU-PLUS SDK.

Install latest version of MCU-PLUS SDK from `AM261x MCU-PLUS SDK`_

For more details on SBL and the Bootflow, refer to `AM261x Bootflow Guide`_

For details on the various Boot Modes, refer to `AM261x Boot Modes`_

To flash the SBL into the board, follow the below process:

*   Locate uart_uniflash.py script.
    It should be found at {MCU_SDK_PATH}/tools/boot/

*   SW4 must be in 0111. This sets the device to UART Boot Mode.
    Verify by opening Serial console, the device periodically prints 'C'
    if its in UART Boot mode.

*   Device is ready for flashing.
    Flashing the SBL can be done with the following command:

    ``python uart_uniflash.py -p {port_name} --cfg=sbl_prebuilt/am261x-lp/default_sbl_null.cfg``

    Replace {port_name} with the port connected to UART0 of AM261x LP.

    NOTE that the supplied .cfg file flashes the prebuilt sbl_null into the device.
    To use any other prebuilt sbl, switch the .cfg file used in above command.

*   Switch device to OSPI boot mode (SW4 in 1100).
    Power cycle the device.

You are ready to flash your zephyr binary using debugging tools.

|

Debugging
*********

It is recommended to use CCS for flashing & debugging binary.

Debugging with CCS can be done as follows:

*   Start project-less debug using AM261x's CCXML file.

    IMPORTANT: Ensure that the initialization GEL scripts for Cortex R5_0 and Cortex R5_1 cores are removed.

*   You may bring device to known state by power-cycling the board.
    Verify by reading SBL logs in the Serial console.

*   Connect to the core of your choice (Cortex R5_0 or Cortex R5_1)
    Load the .elf file (zephyr binary)

*   Enjoy debugging!

|

References
**********

*   `AM261x Technical Reference Manual (TRM)`_

*   `AM261x Register Addendum`_

*   `AM261x Sitara™ Microcontrollers datasheet (Rev. C)`_

*   `AM261x LaunchPad User Guide (Rev. A)`_

.. _AM261x Technical Reference Manual (TRM):
    https://www.ti.com/lit/ug/sprujb6b/sprujb6b.pdf

.. _AM261x Register Addendum:
    https://www.ti.com/lit/ug/spruj94a/spruj94a.pdf

.. _AM261x Sitara™ Microcontrollers datasheet (Rev. C):
    https://www.ti.com/lit/ds/sprspa7c/sprspa7c.pdf

.. _AM261x LaunchPad User Guide (Rev. A):
    https://www.ti.com/lit/ug/sprujf1a/sprujf1a.pdf

.. _AM261x Bootflow Guide:
    https://software-dl.ti.com/mcu-plus-sdk/esd/AM261X/latest/exports/docs/api_guide_am261x/BOOTFLOW_GUIDE.html

.. _AM261x Boot Modes:
    https://software-dl.ti.com/mcu-plus-sdk/esd/AM261X/latest/exports/docs/api_guide_am261x/EVM_SETUP_PAGE.html

.. _AM261x MCU-PLUS SDK:
    https://www.ti.com/tool/MCU-PLUS-SDK-AM261X

.. _TI AM261x Product Page:
    https://www.ti.com/tool/LP-AM261

|

License
*******

This document Copyright (c) 2025 Texas Instruments

SPDX-License-Identifier: Apache-2.0