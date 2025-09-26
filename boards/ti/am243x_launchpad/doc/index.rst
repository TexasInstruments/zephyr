.. zephyr:board:: am243x_launchpad

Overview
********

The AM243x Launchpad is a development board that is based of a AM2434 SoC. The
Cortex R5F cores in the SoC run at 800 MHz. The board also includes a flash
chip, DIP-Switches for the boot mode selection and 2 RJ45 Ethernet ports. This
documentation focuses only on HS-FS devices since the other ones aren't
tested/supported yet.


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
There are currently multiple ways to run Zephyr on the AM2434 Launchpad. Only
the 3 most common ones are described here:

* Load a Zephyr application via UART
* Run a Zephyr application from Flash via MCUboot
* Run a Zephyr application after a TI MCU+ SDK bootloader

The first one has the advantage of being fast to set up and being easy to use.
It is recommended for initial development.

The second one is what a quite normal boot flow for a Zephyr application would
look like but has the disadvantage that some things might not work due to
missing Zephyr support. As of writing this documentation that is UART since the
clock is never set up in this boot mode.

The last one relies on the MCU+ SDK and should be used, if you need to use the
MCU+ SDK to set up some peripherals like UART clocks. Only in this bootmode the
TCM memory can be used.

Depending on the boot mode selection you require different parts of the MCU+
SDK. If you want to start an application only via UART you need the Python tools
of the MCU+ SDK and binary files of the repo.
If you want to boot from flash via MCUboot or chain-load after a SBL bootloader
from the TI MCU+ SDK you need to fully set up the SDK and build some of the examples.


Setting up the MCU+ SDK
-----------------------

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


If you plan on only booting Zephyr applications via UART you can stop here and
continue reading at `Boot method context`_.


Building the MCU+ SDK binaries
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

If you want to use MCUboot or a TI SBL bootloader you have to also build the
internal MCU+ SDK libraries (outside the ``mcu_plus_sdk`` directory):

.. code-block:: console

   make gen-buildfiles DEVICE=am243x PROFILE=release
   make libs DEVICE=am243x PROFILE=release

If you encounter compile errors you have to fix them. For that you might have to
change parameter types, remove missing source files from makefiles or download
missing headers from the TI online reference.

You additionally need to build examples. But before doing so you need to edit
them. You need to include ``kernel/dpl/HwiP.h`` and run ``HwiP_disableInt(160)``
right before the ``runCpu`` call for ``sbl_uart`` and ``sbl_ospi`` examples,
since otherwise the timer from the bootloader is still generating interrupts,
leading to a spurious interrupt.

This needs to be done in the ``main.c`` file that can be found inside
``examples/drivers/boot/<example-name>/am243x-lp/r5fss0-0_nortos`` directory.

After that you can build the examples by running the following command outside
the ``mcu_plus_sdk`` directory:

.. code-block:: console
   make -C examples/drivers/boot/<example-name>/am243x-lp/r5fss0-0_nortos/ti-arm-clang DEVICE=am243x PROFILE=RELEASE

The following examples are relevant:

+-------------------+--------------------------------------------+
| Example name      | Reason                                     |
+===================+============================================+
| sbl_uart_uniflash | Flashing binaries into the connected flash |
+-------------------+--------------------------------------------+
| sbl_uart          | SBL bootloader for booting Zephyr via UART |
+-------------------+--------------------------------------------+
| sbl_ospi          | SBL bootloader for booting from flash      |
+-------------------+--------------------------------------------+

The required output files are inside the ``ti-arm-clang`` directory of the
example and end with ``.hs_fs.tiimage``.


Boot method context
-------------------

Before going over the different boot methods a bit of context will be written
here.

The AM2434 SoC starts with a ROM bootloader that's in the SoC that boots
depending on how the boot DIP Switches are set. The image thats loaded needs to
be in a specific format. For this the ``boot`` variant exists (the full board
qualifier is named ``am243x_launchpad/am2434/r5f0_0/boot``).

When using this boot variant you need to set some Kconfig options that are
described in the next section.

If you want to build Zephyr to run after MCUboot or a TI MCU+ SDK SBL bootloader
examples you need to omit the ``boot`` variant and don't set the Kconfig options
named in the next section.


Required binary files
---------------------

The binary data that needs to be embedded into the binary is the DMSC-L firmware
and some related files. The following table will show the Kconfig names and the
path inside the cloned ``mcu_plus_sdk`` directory.

+-------------------------------+------------------------------------------------------------------------------+
| Kconfig option                | Path                                                                         |
+===============================+==============================================================================+
| CONFIG_TI_K3_SYSFW_BLOB_PATH  | source/drivers/sciclient/soc/am64x_am243x/sysfw-hs-fs-enc.bin                |
+-------------------------------+------------------------------------------------------------------------------+
| CONFIG_TI_K3_SYSFW_CERT_PATH  | source/drivers/sciclient/soc/am64x_am243x/sysfw-hs-fs-enc-cert.bin           |
+-------------------------------+------------------------------------------------------------------------------+
| CONFIG_TI_K3_BOARDCONFIG_PATH | source/drivers/sciclient/sciclient_default_boardcfg/am243x/boardcfg_blob.bin |
+-------------------------------+------------------------------------------------------------------------------+

Additionally you need to set ``CONFIG_TI_K3_PRIVATE_KEY_PATH`` when you want to
get a image that can be booted by the ROM. Due to errata ``i2413`` it's
recommended to use a "degenerate" key. Instructions to generating one are in the
AM2434 TRM and one can be found under
``source/security/security_common/tools/boot/signing/rom_degenerateKey.pem`` in
the ``mcu_plus_sdk``.


Loading an application via UART
-------------------------------
To run an application via UART you need to change the boot DIP switches on the
board into ``11100000`` position. After that you need to connect the board and
run the ``uart_bootloader.py`` script with the venv active, if you created one
during setup. The script is under ``tools/boot`` inside the ``mcu_plus_sdk``
directory. You additionally need to provide the UART interface (e.g.
``/dev/ttyACM0`` under Linux) and the file you want to run.

.. code-block:: console

   python3 uart_bootloader.py -p /dev/ttyACM0 --bootloader=<file-to-run>


Flashing data onto the flash
----------------------------

To flash data onto the flash you need to create a config file first. It should
have the following contents:

.. code-block::

   --flash-writer=<sbl_uart_uniflash-tiimage-output>
   --file=<file-to-flash> --operation=flash --flash-offset=<flash-offset>

The ``<sbl_uart_uniflash-tiimage-output>`` needs to be replaced with the path to
the output file you got when building the ``sbl_uart_uniflash`` example.

The ``<file-to-flash>`` needs to replaced with the file you want to flash,
usually a Zephyr application, MCUboot or the ``sbl_ospi`` bootloader.

The ``<flash-offset>`` is the place where the file should be flashed in
hexadecimal. For bootloaders or when starting Zephyr directly it's ``0x0`` and
for Zephyr applications after the bootloader it's usually ``0x80000``.

Then you need switch the boot DIP switches into ``11100000`` position and run
the ``uart_uniflash.py`` script that can be found under ``tools/boot`` with the
MCU+ SDK venv activated, if you created one.

.. code-block:: console
   python3 uart_uniflash.py -p /dev/ttyACM0 --cfg <path-to-config>


Booting Zephyr via UART
-----------------------

To boot via UART you need to build your application for the ``boot`` target,
including setting the Kconfig options as described in `Required binary files`_.

After that you can run it as described in `Loading an application via UART`_.
The file you need to run is
``<your-build-directory>/zephyr/zephyr.k3_rom_loadable.bin``.


Booting Zephyr from Flash via MCUboot
-------------------------------------


Building and flashing MCUboot
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

First you need to build MCUboot for the ``boot`` target. Please make sure your
version is new enough for Cortex-R support. You need to set the Kconfig options
described in `Loading an application via UART`_.

Additionally you need to set the following Kconfig options manually:

+----------------------------------------+------------+
| Symbol                                 | Value      |
+========================================+============+
| CONFIG_BOOT_RAM_LOAD                   | y          |
+----------------------------------------+------------+
| CONFIG_BOOT_IMAGE_EXECUTABLE_RAM_START | 0x70080000 |
+----------------------------------------+------------+

After building you need to flash the file
``<your-build-directory>/zephyr/zephyr.k3_rom_loadable.bin`` to ``0x0`` as
described in `Flashing data onto the flash`_.


Building and flashing a Zephyr application
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Next you can build your Zephyr application (for the non-boot variant!). Here you
also need to manually change some Kconfig options.

+-----------------------------------------+------------+
| Symbol                                  | Value      |
+=========================================+============+
| CONFIG_BOOTLOADER_MCUBOOT               | y          |
+-----------------------------------------+------------+
| CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD | y          |
+-----------------------------------------+------------+
| CONFIG_MCUBOOT_SIGNATURE_KEY_FILE       | <key-path> |
+-----------------------------------------+------------+

The ``CONFIG_MCUBOOT_SIGNATURE_KEY_FILE`` leads to your private key with which
you signed the application. The default private key is inside the mcuboot
project with the name ``root-rsa-2048.pem``.

You then need to flash the file
``<your-build-directory>/zephyr/zephyr.signed.bin`` at the offset ``0x80000`` as
described in `Flashing data onto the flash`_.


Running
^^^^^^^

After flashing you need to disconnect the board and change the boot DIP switches
into ``01000100`` position. You only need to flash the MCUboot bootloader once,
as long as you don't erase/overwrite it's area.


Booting Zephyr after a TI MCU+ SDK SBL
--------------------------------------

To start Zephyr after a TI MCU+ SBL bootloader you first need to build it
normally for the non-boot variant.

Before booting you need to convert your built Zephyr binary into a format that
the TI example bootloader can boot. You can do this with the following commands,
where ``$TI_TOOLS`` refers to the root of where your ti-tools (clang, sysconfig
etc.) are installed (``$HOME/ti`` by default) and ``$MCUPSDK`` to the root of
the MCU+ SDK (directory called ``mcu_plus_sdk``).  You might have to change
version numbers in the commands. It's expected that the ``zephyr.elf`` from the
build output is in the current directory.

.. code-block:: bash

   $TI_TOOLS/sysconfig_1.21.2/nodejs/node $MCUPSDK/tools/boot/out2rprc/elf2rprc.js ./zephyr.elf
   $MCUPSDK/tools/boot/xipGen/xipGen.out -i ./zephyr.rprc -o ./zephyr.rprc_out -x ./zephyr.rprc_out_xip --flash-start-addr 0x60000000
   $MCUPSDK/tools/boot/xipGen/xipGen.out -i ./zephyr.rprc -o ./zephyr.rprc_out -x ./zephyr.rprc_out_xip --flash-start-addr 0x60000000
   $TI_TOOLS/sysconfig_1.21.2/nodejs/node $MCUPSDK/tools/boot/multicoreImageGen/multicoreImageGen.js --devID 55 --out ./zephyr.appimage ./zephyr.rprc_out@4
   $TI_TOOLS/sysconfig_1.21.2/nodejs/node $MCUPSDK/tools/boot/multicoreImageGen/multicoreImageGen.js --devID 55 --out ./zephyr.appimage_xip ./zephyr.rprc_out_xip@4
   python3 $MCUPSDK/source/security/security_common/tools/boot/signing/appimage_x509_cert_gen.py --bin ./zephyr.appimage --authtype 1 --key $MCUPSDK/source/security/security_common/tools/boot/signing/app_degenerateKey.pem --output ./zephyr.appimage.hs_fs

After that you will have a ``zephyr.appimage.hs_fs`` file in your current directory.


Running via UART SBL
^^^^^^^^^^^^^^^^^^^^

To run the Zephyr application you then have to run a command similar to the one
described in `Loading an application via UART`_. You also need to switch the
boot DIP switches into the position described in the section.

.. code-block:: console

   python3 uart_bootloader.py -p /dev/ttyACM0 --bootloader=<sbl_uart> --file=<path-to-zephyr.appimage.hs_fs>

The ``<sbl_uart>`` refers to the path to the ``.tiimage`` output when building
the ``sbl_uart`` example. The ``<path-to-zephyr.appimage.hs_fs>``needs to be
replaced with the path to the generated ``zephyr.appimage.hs_fs`` file from the
`Booting Zephyr after a TI MCU+ SDK SBL`_ section.


Running via flash SBL
^^^^^^^^^^^^^^^^^^^^^

To run a Zephyr application after a MCU+ SDK SBL bootloader you need to flash
the output from the ``sbl_ospi`` directory (specified in `Building the MCU+ SDK
binaries`_) at ``0x0`` according to `Flashing data onto the flash`_ and the
``zephyr.appimage.hs_fs`` at ``0x80000``, also according to `Flashing data onto
the flash`_.

After that you can switch the boot DIP switches into ``01000100`` position for
booting.


Debugging
=========

For debugging you can use OpenOCD. As of now you need to compile it yourself to
get a version that supports the board. The board config file is called
``ti_am243_launchpad.cfg``.

Additionally you can use the UART interface that is natively supported, though
it doesn't work when running Zephyr from flash without TI MCU+ SBL.


References
**********

AM2434 documents:
   https://www.ti.com/product/de-de/AM2434#tech-docs

AM243x LaunchPad documents:
   https://www.ti.com/tool/LP-AM243#tech-docs

MCU+ SDK Github repository:
   https://github.com/TexasInstruments/mcupsdk-core


License
*******

This document Copyright (c) Siemens Mobility GmbH

SPDX-License-Identifier: Apache-2.0
