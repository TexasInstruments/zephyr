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

IPC RPMSG TI - MCU_PLUS_SDK and Zephyr Interoperability
*******************************************************

AM261x SoC supports IPC RPMSG communication between MCU-PLUS SDK and Zephyr OS. Important points to note,

 * Zephyr Should always act as Host while MCU-PLUS SDK acts as Remote.
 * Zephyr uses OpenAMP framework for IPC RPMSG communication, whereas MCU_PLUS_SDK uses custom RPMSG implementation.
 * Ensure that both Zephyr and MCU-PLUS SDK use same RPMSG configuration (shared memory address, size, vring size etc.) for successful communication.

.. note::

    - The tx and rx base address for the VRINGS should match on both sides for successful communication.
    - Consider a breakpoint at the `open` function in the zephyr ipc rpmsg static vrings backend driver (`subsys/ipc/ipc_service/backends/ipc_rpmsg_static_vrings.c`) to ensure that the vring base addresses match on both sides.

        - This can be viewed at `data->vr->tx_addr and data->vr->rx_addr`.
        - This rx and tx addresses should match the tx and rx base addresses in the ti_drivers_config.c file in the MCU-PLUS-SDK application, respectively.

.. note::

    1. The MCU_PLUS_SDK is designed to work RPMSG communication between peers and as remote core to a linux host. Therefore, some modifications are needed to make it work with Zephyr as host.
    2. The following patch needs to be applied on the MCU-PLUS-SDK to make it interoperable with Zephyr as host and build the libs.

        .. code-block:: diff

            diff --git a/source/drivers/ipc_rpmsg.h b/source/drivers/ipc_rpmsg.h
            index de3d9e66112..99fa9cef235 100755
            --- a/source/drivers/ipc_rpmsg.h
            +++ b/source/drivers/ipc_rpmsg.h
            @@ -225,6 +225,7 @@ typedef struct
                                                                * is specified in the resource table
                                                                */
                uint16_t linuxCoreId; /** ID of linux core */
            +    uint16_t hostCoreId; /** ID of host core */
                uint8_t  isCrcEnabled; /* CRC Enable/Disable flag */
                RPMessage_CrcHookFxn crcHookFxn; /* Hook Function to be provided by application for CRC calculation */
            } RPMessage_Params;
            diff --git a/source/drivers/ipc_rpmsg/ipc_rpmsg.c b/source/drivers/ipc_rpmsg/ipc_rpmsg.c
            index 697cd9a6f88..2df6683ad69 100755
            --- a/source/drivers/ipc_rpmsg/ipc_rpmsg.c
            +++ b/source/drivers/ipc_rpmsg/ipc_rpmsg.c
            @@ -608,6 +608,18 @@ uint32_t RPMessage_isLinuxCore(uint16_t coreId)
                return isLinuxCore;
            }

            +uint32_t RPMessage_isHostCore(uint16_t coreId)
            +{
            +    uint32_t isHostCore = 0;
            +
            +    if((coreId == gIpcRpmsgCtrl.hostCoreId) || RPMessage_isLinuxCore(coreId))
            +    {
            +        isHostCore = 1;
            +    }
            +
            +    return isHostCore;
            +}
            +
            int32_t  RPMessage_waitForLinuxReady(uint32_t timeout)
            {
                int32_t status = SystemP_FAILURE;
            diff --git a/source/drivers/ipc_rpmsg/ipc_rpmsg_priv.h b/source/drivers/ipc_rpmsg/ipc_rpmsg_priv.h
            index c20951d2a13..474ed520222 100755
            --- a/source/drivers/ipc_rpmsg/ipc_rpmsg_priv.h
            +++ b/source/drivers/ipc_rpmsg/ipc_rpmsg_priv.h
            @@ -250,6 +250,7 @@ typedef struct
                void  *controlEndPtCallbackArgs; /* user callback args for control message */
                const RPMessage_ResourceTable *linuxResourceTable; /* resource table used with linux */
                uint16_t linuxCoreId; /* Core ID of core running linux */
            +    uint16_t hostCoreId;  /* Core ID of host core, if any */
                uint8_t  isCrcEnabled; /* CRC Enable/Disable flag */
                RPMessage_CrcHookFxn crcHookFxn; /* Hook Function provided by application for CRC calculation. */
            } IpcRpmsg_Ctrl;
            @@ -273,6 +274,7 @@ static inline void IpcRpMsg_dataAndInstructionBarrier(void)

            /* utility function to find if core ID runs linux */
            uint32_t RPMessage_isLinuxCore(uint16_t coreId);
            +uint32_t RPMessage_isHostCore(uint16_t coreId);

            /* functions for VRING TX handling and initialization */
            void     RPMessage_vringCheckEmptyTxBuf(uint16_t remoteCoreId);
            diff --git a/source/drivers/ipc_rpmsg/ipc_rpmsg_vring.c b/source/drivers/ipc_rpmsg/ipc_rpmsg_vring.c
            index 0aac2ffa630..c6dc7e3d069 100644
            --- a/source/drivers/ipc_rpmsg/ipc_rpmsg_vring.c
            +++ b/source/drivers/ipc_rpmsg/ipc_rpmsg_vring.c
            @@ -106,7 +106,7 @@ int32_t RPMessage_vringPutFullTxBuf(uint16_t remoteCoreId, uint16_t vringBufId,
                int32_t status = SystemP_FAILURE;
                uint32_t elapsedTicks, startTicks;

            -    if(RPMessage_isLinuxCore(remoteCoreId) != 0U)
            +    if(RPMessage_isHostCore(remoteCoreId) != 0U)
                {
                    /* for linux we need to send the TX VRING ID in the mailbox message */
                    txMsgValue = RPMESSAGE_LINUX_TX_VRING_ID;
            @@ -187,7 +187,7 @@ int32_t RPMessage_vringGetFullRxBuf(uint16_t remoteCoreId, uint16_t *vringBufId)

                oldIntState = HwiP_disable();

            -    if(RPMessage_isLinuxCore(remoteCoreId) != 0U)
            +    if(RPMessage_isHostCore(remoteCoreId) != 0U)
                {
                    /* There's nothing available */
                    if (vringObj->lastAvailIdx != vringObj->avail->idx)
            @@ -230,7 +230,7 @@ void RPMessage_vringPutEmptyRxBuf(uint16_t remoteCoreId, uint16_t vringBufId)

                oldIntState = HwiP_disable();

            -    if(RPMessage_isLinuxCore(remoteCoreId) != 0U)
            +    if(RPMessage_isHostCore(remoteCoreId) != 0U)
                {
                    struct vring_used_elem *used;

            @@ -278,7 +278,7 @@ uint32_t RPMessage_vringIsFullRxBuf(uint16_t remoteCoreId)

                oldIntState = HwiP_disable();

            -    if(RPMessage_isLinuxCore(remoteCoreId) != 0U)
            +    if(RPMessage_isHostCore(remoteCoreId) != 0U)
                {
                    if (vringObj->lastAvailIdx == vringObj->avail->idx)
                    {
            @@ -413,7 +413,8 @@ void RPMessage_vringReset(uint16_t remoteCoreId, uint16_t isTx, const RPMessage_
                */
                offset_desc  = 0;
                offset_avail = offset_desc  + (sizeof(struct vring_desc) * numBuf);
            -    offset_used  = offset_avail + RPMessage_align( (sizeof(uint16_t) * (uint32_t)(2U + (uint32_t)numBuf)), align);
            +    /* changes to match the OpenAMP implementation in Zephyr */
            +    offset_used  = offset_avail + RPMessage_align( (sizeof(uint16_t) * (uint32_t)(3U + (uint32_t)numBuf)), align);
                offset_buf   = offset_used  + RPMessage_align( (sizeof(uint16_t) * 2U) + (sizeof(struct vring_used_elem) * (uint32_t)numBuf), align);

                RPMessage_vringResetInternal(vringObj,

    3. Add a hostCoreId field in RPMessage_Params structure to specify the host core ID in the ti_drivers_config.c file. like the following

        .. code-block:: C

                /* IPC RPMessage */
                {
                    RPMessage_Params rpmsgParams;
                    int32_t status;
                    /* initialize parameters to default */
                    RPMessage_Params_init(&rpmsgParams);

                    /*---------Changes begin---------*/
                    /* Adding the hostCoreID for the core that runs Zephyr */
                    rpmsgParams.hostCoreId = CSL_CORE_ID_R5FSS0_0;  // -> Adding the hostCoreID for the core that runs Zephyr
                    /* Matching the tx and rx Base Address from the Zephyr */
                    /* TX VRINGs */
                    rpmsgParams.vringTxBaseAddr[CSL_CORE_ID_R5FSS0_0] = (uintptr_t)(0x720020E4);
                    /* RX VRINGs */
                    rpmsgParams.vringRxBaseAddr[CSL_CORE_ID_R5FSS0_0] = (uintptr_t)(0x72002004);
                    /*---------Changes End---------*/

                    /* Other VRING properties */
                    rpmsgParams.vringSize = IPC_RPMESSAGE_VRING_SIZE;
                    rpmsgParams.vringNumBuf = IPC_RPMESSAGE_NUM_VRING_BUF;
                    rpmsgParams.vringMsgSize = IPC_RPMESSAGE_MAX_VRING_BUF_SIZE;
                    rpmsgParams.isCrcEnabled = 0;

                    /* initialize the IPC RP Message module */
                    status = RPMessage_init(&rpmsgParams);
                    DebugP_assert(status==SystemP_SUCCESS);
                }

    4. Use the RPMessage_announce function to notify Zephyr OS that the remote core is ready for communication over the announced service name.
    5. Since Zephyr's mbox driver doesn't support the ipc-notify, the sync-all API from SDK should not be used and Application needs to create a custom handshake for this implementation.

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