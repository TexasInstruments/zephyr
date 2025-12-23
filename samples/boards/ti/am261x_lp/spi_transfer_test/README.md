# SPI Transfer Test

## Overview

This sample demonstrates SPI loopback testing with concurrent thread execution to verify:
1. SPI controller functionality in loopback mode (MOSI connected to MISO)
2. SPI driver's arbitration capabilities under concurrent access
3. DMA transfer correctness with cache maintenance operations

## Test Description

The test creates **4 concurrent threads**, each executing identical test logic:
- Threads perform multiple iterations of SPI transceive operations
- Each iteration transmits a unique pattern and verifies loopback reception
- Uses proper cache maintenance (flush/invalidate) for DMA coherence

### Test Patterns
Four distinct 8-bit patterns are used for verification:
1. `{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08}` - Sequential increment
2. `{0xF1, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8}` - High byte sequential
3. `{0xA5, 0x5A, 0xA5, 0x5A, 0xA5, 0x5A, 0xA5, 0x5A}` - Alternating nibble pattern
4. `{0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF}` - Zero/FF alternating

## Build and Run

```bash
# Build for AM261x LP board
west build -b am261x_lp samples/boards/ti/am261x_lp/spi_transfer_test

# Flash to hardware
west flash

# Monitor output via UART console
```

## Expected Output

Each thread reports:
```
[INF] SPI Loopback Test Started (Thread X)
[INF] Running test pattern Y
[INF] Thread Z: Testing pattern with first byte: 0xNN
<callback> Test Callback activated. SPI transfer completed successfully!
[INF] Thread Z:Pattern test PASSED - Loopback successful
[INF] Thread Z: Transmitted/received data (first 8 bytes):
0xNN 0xNN 0xNN 0xNN 0xNN 0xNN 0xNN 0xNN
```

After all threads complete:
```
========================================
FINAL TEST SUMMARY
========================================
Threads passed: 4 / 4
RESULT: ALL TESTS PASSED
========================================
```
