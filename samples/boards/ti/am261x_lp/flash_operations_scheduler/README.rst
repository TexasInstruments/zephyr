.. zephyr:code-sample:: flash_operations_scheduler
   :name: Flash Operations Scheduler

   Print "Hello World" to the console.

Overview
********

A Sample to showcase the usage of the Flash Operations Scheduler Driver from MCU_PLUS_SDK
and write to the Flash while the Flash is being used in the DAC mode for other usecases. 

Building and Running
********************

This application can be built and executed on QEMU as follows:

.. zephyr-app-commands::
   :zephyr-app: samples/flash_operations_scheduler
   :host-os: unix
   :board: am261x_lp
   :goals: run
   :compact:

To build for another board, change "am261x_lp" above to that board's name.

Sample Output
=============

.. code-block:: console

   *** Booting Zephyr OS build v4.2.0-79-g87d73aed36f9 ***
   Flash (OSPI0 DAC mode) reads succesfully without abort, OSPI0 is in DAC mode
   FLSOPSKD Wrapper Erase: Starting erase from offset 0x00200000 to offset 0x00201000
   FLSOPSKD Wrapper Erase: Successfully erased page at offset 0x00200000
   FLSOPSKD Wrapper Write: Write Operation Success | Ticks : 691053
   write completion status : 0 |   SUCCESS.         fill : 1. Iteration : 0
   FLSOPSKD Wrapper Erase: Starting erase from offset 0x00200000 to offset 0x00201000
   FLSOPSKD Wrapper Erase: Successfully erased page at offset 0x00200000
   FLSOPSKD Wrapper Write: Write Operation Success | Ticks : 690860
   write completion status : 0 |   SUCCESS.         fill : 0. Iteration : 1
   FLSOPSKD Wrapper Erase: Starting erase from offset 0x00200000 to offset 0x00201000
   FLSOPSKD Wrapper Erase: Successfully erased page at offset 0x00200000
   FLSOPSKD Wrapper Write: Write Operation Success | Ticks : 690871
   write completion status : 0 |   SUCCESS.         fill : 1. Iteration : 2
   FLSOPSKD Wrapper Erase: Starting erase from offset 0x00200000 to offset 0x00201000

