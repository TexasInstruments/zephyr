.. zephyr:code-sample:: uart-dma-loopback-ti
   :name: UART DMA Loopback Test
   :relevant-api: uart_interface dma_interface

   Test UART with DMA using hardware loopback.

Overview
********

This sample demonstrates UART communication with DMA transfer on the TI AM261x LaunchPad.
It uses DMA to handle UART transmission and implements a loopback test by connecting the
UART TX pin to the UART RX pin with a physical jumper wire.
The sample transmits test data via UART with DMA, receives it back through the loopback,
and verifies data integrity.

Hardware Setup
**************

**This sample requires a physical jumper wire connection.**

Connect the UART TX pin to the UART RX pin with a jumper wire to create a hardware loopback.

Refer to your board's overlay file (``boards/<your_board>.overlay``) to identify the specific
pin assignments for UART TX and RX that need to be connected.

Key Features
************

- **DMA-based UART**: Uses DMA controller for UART transmission
- **Async UART API**: Demonstrates asynchronous UART operations with callbacks
- **Cache Management**: Proper cache line alignment and invalidation for cache-coherent data access
- **Data Verification**: Compares transmitted and received buffers to validate loopback functionality
- **Error Handling**: Includes return codes indicating test success or failure

Building and Running
********************

Build for the AM261x LaunchPad:

.. code-block:: bash

   west build -b am261x_lp/am2612/r5f0_0 samples/boards/ti/am261x_lp/uart_dma_loopback_test

Flash to the board:

.. code-block:: bash

   west flash

Sample Output
*************

With the jumper wire properly connected between UART TX and RX, the console output should display:

.. code-block:: console

   Hi from UART. This is an intentionally long message, my friend! Lorem Ipsum!
   UART DMA loopback test passed!

Without the jumper wire, or if the connection is incorrect:

.. code-block:: console

   (received data does not match)
   UART DMA loopback test failed!

Technical Details
*****************

UART Configuration
==================

The sample uses the ``uart_dut`` alias defined in the board overlay to access the UART device.
The UART is configured for asynchronous operation with DMA channels allocated for both
transmission and reception.

Cache Coherency
===============

Buffers used for DMA transfers are aligned to ``CONFIG_DCACHE_LINE_SIZE`` to avoid cache
coherency issues:

.. code-block:: c

   char tx_buf[96] __aligned(CONFIG_DCACHE_LINE_SIZE) = "...";
   char rx_buf[96] __aligned(CONFIG_DCACHE_LINE_SIZE);

Before DMA transmission, the transmit buffer cache is flushed to ensure the DMA controller
reads fresh data from main memory:

.. code-block:: c

   sys_cache_data_flush_range((void *)tx_buf, sizeof(tx_buf));

After DMA reception completes, the receive buffer cache is invalidated to ensure the CPU
reads the freshly transferred data from main memory:

.. code-block:: c

   sys_cache_data_invd_range((void *)rx_buf, sizeof(rx_buf));

Return Values
=============

- ``0``: Test passed - transmitted and received data match
- ``-1``: Test failed - data mismatch detected
- Error code: UART transmission error encountered
