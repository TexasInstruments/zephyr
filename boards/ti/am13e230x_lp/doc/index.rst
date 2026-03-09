.. zephyr:board:: am13e230x_lp

Overview
********

The AM13E230x LaunchPad is a development board based on the AM13E230x SoC, a 200MHz ARM Cortex M33 real-time motor control MCU with edge AI, TMU and 512KB on-chip flash.

See the `TI AM13E230x Product Page`_ for details.

|

Hardware
********

* Processor:

  + Arm Cortex M33 @ 200MHz

* Memory:

  + 128KB SRAM
  + 512KB on-chip flash

* Debug:

  + XDS110 based JTAG

Details present in `AM13E230x Microcontrollers datasheet`_

|

Supported Features
******************

.. zephyr:board-supported-hw::

|

Flashing and Debugging
**********************

Early support for OpenOCD is available in the ``asm-hal_ti`` repository as a patch: `AM13E230x OpenOCD support patch`_.

It is possible to load both SRAM-only (``XIP=n``) and flash-resident (``XIP=y``) applications using ``west flash``.

For flash-resident applications, OpenOCD is currently the recommended path for debugging.

|

Building a patched OpenOCD
==========================

*   Clone ``https://github.com/openocd-org/openocd``

*   Apply the provided patch on top of upstream OpenOCD

*   Initialize submodules: ``git submodule update --init``

*   ``./bootstrap``

*   ``./configure --enable-internal-jimtcl``

*   ``make -j$(nproc)``

*    Launch OpenOCD: ``src/openocd -s tcl -f tcl/board/ti/am13e230x-launchpad.cfg``

*    You should see::
        
        Open On-Chip Debugger 0.12.0+dev-02425-g755333b81 (2026-03-08-06:45)
        Licensed under GNU GPL v2
        For bug reports, read
        	http://openocd.org/doc/doxygen/bugs.html
        Info : XDS110: vid/pid = 0451/bef3
        Info : XDS110: firmware version = 3.0.0.41
        Info : XDS110: hardware version = 0x002f
        Info : XDS110: connected to target via JTAG
        Info : XDS110: TCK set to 2500 kHz
        Info : clock speed 10000 kHz
        Info : JTAG tap: AM13E230X.cpu tap/device found: 0x6ba00477 (mfg: 0x23b (ARM Ltd), part: 0xba00, ver: 0x6)
        Info : [AM13E230X.cpu] Cortex-M33 r1p0 processor detected
        Info : [AM13E230X.cpu] target has 8 breakpoints, 4 watchpoints
        Info : [AM13E230X.cpu] Examination succeed
        Info : [AM13E230X.cpu] starting gdb server on 3333
        Info : Listening on port 3333 for gdb connections
        Info : JTAG tap: AM13E230X.cpu tap/device found: 0x6ba00477 (mfg: 0x23b (ARM Ltd), part: 0xba00, ver: 0x6)
        [AM13E230X.cpu] halted due to debug-request, current mode: Thread 
        xPSR: 0xf9000000 pc: 0x0000204c msp: 0x20001688
        Info : Listening on port 6666 for tcl connections
        Info : Listening on port 4444 for telnet connections

|

Debugging using OpenOCD
=======================

*   Simply run ``west debug`` in your application directory

*   To launch the debugger manually, install a GDB binary that supports ARM Cortex-M (common package names: ``arm-none-eabi-gdb``, ``gdb-multiarch``)

*   Build your Zephyr application

*   Launch GDB with the generated ELF: ``arm-none-eabi-gdb <path to application>/build/zephyr/zephyr.elf``

*   (In the GDB shell) ``monitor reset halt``

*   Load the application: ``load``

*   Set breakpoints, for example: ``break main``

*   Start the application: ``continue``

*   Your application should run to ``main``. Enjoy!

|

Known Issues
************

- CCS loading for ``XIP=y`` builds is currently broken

|

References
**********

*   `TI AM13E230x Product Page`_ 

*   `AM13E230x Microcontrollers datasheet`_

*   `AM13E230x OpenOCD support patch`_

.. _TI AM13E230x Product Page:
    https://www.ti.com/product/AM13E23019

.. _AM13E230x Microcontrollers datasheet:
    https://www.ti.com/lit/gpn/am13e23019

.. _AM13E230x OpenOCD support patch:
   https://github.com/TexasInstruments/asm-hal_ti/blob/master/am13/openocd_am13e230x_support.patch

|

License
*******

This document Copyright (c) 2026 Texas Instruments Incorporated

SPDX-License-Identifier: Apache-2.0