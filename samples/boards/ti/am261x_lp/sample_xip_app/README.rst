.. zephyr:code-sample:: sample_xip_app
	 :name: TI AM261x LP XIP Application
	 :relevant-api: gpio_interface

	 Run a blinky application on the main core and a helloworld application on the second core.

Overview
********

This sample demonstrates how to create a multi-core application for the
TI AM261x LP platform. It consists of two applications:

1. The main core runs a Blinky LED application
2. The remote core runs a Hello World application

The sample shows how to set up a sysbuild-based project that builds
and links applications for multiple cores.

Requirements
************

- TI AM261x LP development board

Building and Running
********************

Build the sample using sysbuild:

.. zephyr-app-commands::
	 :zephyr-app: samples/boards/ti/am261x_lp/sample_xip_app
	 :board: am261x_lp/am2612/r5f0_0
	 :goals: build
	 :west-args: --sysbuild
	 :compact:

An mcelf file should be generated in the build directory after a successful build.
Flash the application using TI's UniFlash tool.

You should see the LED on the board blinking and messages printed to the console
from both the main core and remote core.

Sample Output
*************

.. code-block:: console

	 *** Booting Zephyr OS build zephyr-vX.X.X-XXXXXXX ***
	 Main Core Blinky Started
	 LED state: ON
	 LED state: OFF
	 ...

	 *** Booting Zephyr OS build zephyr-vX.X.X-XXXXXXX ***
	 Remote Core Hello World Started!
	 Hello World from Remote Core! (am261x_lp/am2612/r5f0_1)
	 Hello World from Remote Core! (am261x_lp/am2612/r5f0_1)
	 ...
