.. zephyr:board:: am243x_launchpad

Overview
********

The AM243x Launchpad is a development board that is based of a AM2434 SoC. The
Cortex R5F cores in the SoC run at 800 MHz. The board also includes a flash
chip, DIP-Switches for the boot mode selection and 2 RJ45 Ethernet ports.


Hardware
********

The AM2434 SoC has 2 domains. A MAIN domain and a MCU domain. The MAIN domain
consists of 4 R5F cores and the MCU domain of one M4F core. It is possible to
isolate those but not necessary.

The board physically contains:

* 512 Mb of Infineon NOR Flash
* DIP switches to change the boot mode
* Buttons to reset parts of the SoC in different ways
* A push button connected to a GPIO pin
* Multiple LEDs (some for power indication and some connected to GPIO pins)
* A XDS110 debug probe (JTAG emulation)


Supported Features
==================

.. zephyr:board-supported-hw::


Connections and IOs
===================

This table shows the connected pins. It only includes the ones that are
currently configured for this board. UART0 is connected to the onboard XDS110
chip and can be accessed through it without requiring additional hardware.

+-----------+---------------------+----------+
| Type      | Name                | Pin      |
+===========+=====================+==========+
| LED       | Test LED 1 Green    | GPIO0 22 |
+-----------+---------------------+----------+
| UART      | UART0 TX            | GPIO1 30 |
+-----------+---------------------+----------+
| UART      | UART0 RX            | GPIO1 29 |
+-----------+---------------------+----------+
| NOR Flash | CLK                 | GPIO0 0  |
+-----------+---------------------+----------+
| NOR Flash | CSn0                | GPIO0 11 |
+-----------+---------------------+----------+
| NOR Flash | D0                  | GPIO0 3  |
+-----------+---------------------+----------+
| NOR Flash | D1                  | GPIO0 4  |
+-----------+---------------------+----------+
| NOR Flash | D2                  | GPIO0 5  |
+-----------+---------------------+----------+
| NOR Flash | D3                  | GPIO0 6  |
+-----------+---------------------+----------+
| NOR Flash | DQS                 | GPIO0 2  |
+-----------+---------------------+----------+
| NOR Flash | LBCLKO              | GPIO0 1  |
+-----------+---------------------+----------+


Programming and Debugging
*************************


Flashing
========
The boot process of the AM2434 SoC requires the booting image to be in a
specific format and to wait for the internal DMSC-L of the AM2434 to start up
and configure memory firewalls. Since there exists no Zephyr support it's
required to use one of the SBL bootloader examples from the TI MCU+ SDK.


Prerequisites
-------------

The following steps are from the time this documentation was written and might
change in the future. They also target Linux with assumption some basic things
(like python3 and openssl) are installed.

To build these you need to install the TI MCU+ SDK. To do this you need to
follow the steps described in the ``mcupsdk-core`` repository, which includes
cloning the repositories with west.  It's recommended to use another Python venv
for this since the MCU+ SDK has own Python dependencies that could conflict with
Zephyr dependencies. You can replace ``all/dev.yml`` in the ``west init``
command with ``am243x/dev.yml``, if you want to clone a few less repositories.

You also need to follow the "Downloading And Installing Dependencies" section
but you need to replace all ``am263x`` occurences in commands with ``am243x``.
Please also take note of the ``tools`` and ``mcu_plus_sdk`` install path. The
``tools`` install path will later be referred to as ``$TI_TOOLS`` and the MCU+
SDK path as ``$MCUPSDK``. You can pass ``--skip_doxygen=true`` and
``--skip_ccs=true`` to the install script since they aren't needed. You might
encounter a error that a script can't be executed. To fix it you need to mark it
as executable with ``chmod +x <path>`` and run the ``download_components.sh``
again.

Summarized you will most likely want to run the following commands or similar
versions for setting up the MCU+ SDK:

.. code-block:: console

   python3 -m venv .venv
   source .venv/bin/activate
   pip3 install west
   west init -m https://github.com/TexasInstruments/mcupsdk-manifests.git --mr mcupsdk_west --mf am243x/dev.yml
   west update
   ./mcupsdk_setup/am243x/download_components.sh --skip_doxygen=true --skip_ccs=true
   # the following two commands are not needed, if the download_components.sh script ran successfully
   chmod +x mcupsdk_setup/releases/10_01_00/am243x/download_components.sh
   ./mcupsdk_setup/am243x/download_components.sh --skip_doxygen=true --skip_ccs=true


Now you can build the internal libraries with the following commands:

.. code-block:: console

   make gen-buildfiles DEVICE=am243x PROFILE=release
   make libs DEVICE=am243x PROFILE=release

If you encounter compile errors you have to fix them. For that you might have to
change parameter types, remove missing source files from makefiles or download
missing headers from the TI online reference.

Depending on whether you later want to boot from flash or by loading the image
via UART either the ``sbl_ospi`` or the ``sbl_uart`` example is relevant for the
next section.


Building the bootloader itself
------------------------------

The example is found in the
``examples/drivers/boot/<example>/am243x-lp/r5fss0-0_nortos`` directory. You
want to edit the ``main.c`` file to include ``kernel/dpl/HwiP.h`` and run
``HwiP_disableInt(160)`` right before the ``runCpu`` function is called since
Zephyr will otherwise fault due to the bootloader timer still running and
generating an spurious interrupt.

You can then build the example by invoking ``make -C
examples/drivers/boot/<example>/am243x-lp/r5fss0-0_nortos/ti-arm-clang/
DEVICE=am243x PROFILE=release`` from the ``mcu_plus_sdk`` root directory. If you
want to boot from flash you also need to build the UART uniflash example by
running the same command again but with ``<example>`` being ``sbl_uart_uniflash``.


Converting the Zephyr application
---------------------------------

Additionally for booting you need to convert your built Zephyr binary into a
format that the TI example bootloader can boot. You can do this with the
following commands, where ``$TI_TOOLS`` refers to the root of where your
ti-tools (clang, sysconfig etc.) are installed (``$HOME/ti`` by default) and
``$MCUPSDK`` to the root of the MCU+ SDK (directory called ``mcu_plus_sdk``).
You might have to change version numbers in the commands. It's expected that the
``zephyr.elf`` from the build output is in the current directory.

.. code-block:: bash

   $TI_TOOLS/sysconfig_1.21.2/nodejs/node $MCUPSDK/tools/boot/out2rprc/elf2rprc.js ./zephyr.elf
   $MCUPSDK/tools/boot/xipGen/xipGen.out -i ./zephyr.rprc -o ./zephyr.rprc_out -x ./zephyr.rprc_out_xip --flash-start-addr 0x60000000
   $MCUPSDK/tools/boot/xipGen/xipGen.out -i ./zephyr.rprc -o ./zephyr.rprc_out -x ./zephyr.rprc_out_xip --flash-start-addr 0x60000000
   $TI_TOOLS/sysconfig_1.21.2/nodejs/node $MCUPSDK/tools/boot/multicoreImageGen/multicoreImageGen.js --devID 55 --out ./zephyr.appimage ./zephyr.rprc_out@4
   $TI_TOOLS/sysconfig_1.21.2/nodejs/node $MCUPSDK/tools/boot/multicoreImageGen/multicoreImageGen.js --devID 55 --out ./zephyr.appimage_xip ./zephyr.rprc_out_xip@4
   python3 $MCUPSDK/source/security/security_common/tools/boot/signing/appimage_x509_cert_gen.py --bin ./zephyr.appimage --authtype 1 --key $MCUPSDK/source/security/security_common/tools/boot/signing/app_degenerateKey.pem --output ./zephyr.appimage.hs_fs


Running the Zephyr image
------------------------

After that you want to switch the bootmode to UART by switching the DIP-Switches
into a ``11100000`` position.

If you want to just run the image via UART you need to run ``python3
uart_bootloader.py -p /dev/ttyACM0 --bootloader=sbl_uart.release.hs_fs.tiimage
--file=zephyr.appimage.hs_fs``.  The ``uart_bootloader.py`` script is found in
``$MCUPSDK/tools/boot`` and the ``sbl_uart.release.hs_fs.tiimage`` in
``$MCUPSDK/tools/boot/sbl_prebuilt/am243x-lp``.  After sending the image your
Zephyr application will run after a 2 second long delay.

If you want to flash the image instead you have to take one example config file
from the ``$MCUPSDK/tools/boot/sbl_prebuilt/am243x-lp`` directory and change the
filepath according to your names. It should look approximately like this:

.. code-block::

   --flash-writer=sbl_uart_uniflash.release.hs_fs.tiimage
   --file=zephyr.appimage.hs_fs --operation=flash --flash-offset=0x80000
   --file=zephyr.appimage_xip --operation=flash-xip

You then need to run ``python3 uart_uniflash.py -p /dev/ttyACM0
--cfg=<name-of-your-config-file>``. The scripts and images are in the same path
as described in the UART section above.

After flashing your image you can power off your board, switch the DIP-Switches
into ``01000100`` position and power your board back on. After that your Zephyr
image will boot immeadiatly.


Debugging
=========

For debugging you can use OpenOCD. As of now you need to compile it yourself to
get a version that supports the board. The board config file is called
``ti_am243_launchpad.cfg``.

Additionally you can use the UART interface that is natively supported.


References
**********

AM2434 documents:
   https://www.ti.com/product/de-de/AM2434#tech-docs

MCU+ SDK Github repository:
   https://github.com/TexasInstruments/mcupsdk-core


License
*******

This document Copyright (c) Siemens Mobility GmbH

SPDX-License-Identifier: Apache-2.0
