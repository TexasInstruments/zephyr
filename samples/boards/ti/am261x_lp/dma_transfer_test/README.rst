AM261x DMA Transfer Test Sample
################################

This sample demonstrates Direct Memory Access (DMA) memory-to-memory transfer operations on the TI AM261x LaunchPad board. It showcases how to configure and execute a DMA transfer, handle completion callbacks, and use cache management APIs to ensure data coherency.

Overview
********

The sample transfers a test string from one memory location to another using DMA, then verifies that the data was transferred correctly.
This sample also includes cache invalidation operations to demonstrate proper cache handling on systems with data cache.

Key Features
************

- **DMA Configuration**: Sets up memory-to-memory transfer with burst length of 8 bytes
- **Interrupt-driven**: Uses DMA completion callbacks with semaphore synchronization
- **Cache Management**: Demonstrates cache line alignment and cache invalidation after DMA transfers
- **Data Verification**: Compares source and destination buffers to ensure transfer integrity
- **Error Handling**: Includes timeout protection and error checking at each step

Building and Running
********************

Build for the AM261x LaunchPad:

.. code-block:: bash

   west build -b am261x_lp/am2612/r5f0_0 samples/boards/ti/am261x_lp/dma_transfer_test

Flash to the board:

.. code-block:: bash

   west flash

Expected Output
***************

.. code-block:: console

   Starting DMA memory-to-memory transfer
   Source data: Hello, testing DMA memory-to-memory transfer!
   DMA transfer completed successfully!
   Received data: Hello, testing DMA memory-to-memory transfer!
   Memory-to-memory DMA transfer verified successfully!
   Test passed!

Technical Details
*****************

DMA Transfer Configuration
==========================

The DMA channel is configured with:

- **Channel Direction**: Memory-to-memory (MEMORY_TO_MEMORY)
- **Data Size**: 1 byte per transfer
- **Burst Length**: 8 bytes
- **Block Size**: 64 bytes (size of tx_data buffer)
- **Callback**: Enabled for completion notification

Cache Considerations
====================

Buffers are aligned to ``CONFIG_DCACHE_LINE_SIZE`` to avoid cache line conflicts:

.. code-block:: c

   static char tx_data[64] __aligned(CONFIG_DCACHE_LINE_SIZE) = ...
   static char rx_data[64] __aligned(CONFIG_DCACHE_LINE_SIZE) = ...

After the DMA transfer completes, the destination buffer cache is invalidated to ensure the CPU reads the freshly transferred data from main memory:

.. code-block:: c

   sys_cache_data_invd_range((void *)rx_data, sizeof(rx_data));

Synchronization
===============

The sample uses a kernel semaphore to synchronize the main thread with the DMA completion interrupt:

1. Main thread configures and starts DMA transfer
2. DMA completion callback signals the semaphore
3. Main thread waits on semaphore with 5-second timeout
4. After transfer, cache is invalidated and data is verified
