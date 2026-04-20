/*
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <dl_unicommi2ct.h>
#include <dl_gpio.h>

/* Defines for APP_I2C_CONTROLLER */
#define APP_I2C_CONTROLLER_INST                                     UC2_INST_PTR
#define APP_I2C_CONTROLLER_BUS_SPEED_HZ                                   100000
#define GPIO_APP_I2C_CONTROLLER_SDA_PORT                                   GPIO0
#define GPIO_APP_I2C_CONTROLLER_SDA_PIN                           DL_GPIO_PIN_11
#define GPIO_APP_I2C_CONTROLLER_IOMUX_SDA                      (IOMUX_PINCM_PA11)
#define GPIO_APP_I2C_CONTROLLER_IOMUX_SDA_FUNC                   IOMUX_PA11_UC2_TX_SDA
#define GPIO_APP_I2C_CONTROLLER_SCL_PORT                                   GPIO0
#define GPIO_APP_I2C_CONTROLLER_SCL_PIN                           DL_GPIO_PIN_23
#define GPIO_APP_I2C_CONTROLLER_IOMUX_SCL                      (IOMUX_PINCM_PA23)
#define GPIO_APP_I2C_CONTROLLER_IOMUX_SCL_FUNC                   IOMUX_PA23_UC2_RX_SCL

/* Defines for APP_I2C_TARGET */
#define APP_I2C_TARGET_INST                                         UC1_INST_PTR
#define APP_I2C_TARGET_TARGET_OWN_ADDR                                      0x50
#define GPIO_APP_I2C_TARGET_SDA_PORT                                       GPIO0
#define GPIO_APP_I2C_TARGET_SDA_PIN                               DL_GPIO_PIN_10
#define GPIO_APP_I2C_TARGET_IOMUX_SDA                         (IOMUX_PINCM_PA10)
#define GPIO_APP_I2C_TARGET_IOMUX_SDA_FUNC              IOMUX_PA10_UC1_TX_SDA_PICO
#define GPIO_APP_I2C_TARGET_SCL_PORT                                       GPIO0
#define GPIO_APP_I2C_TARGET_SCL_PIN                                DL_GPIO_PIN_9
#define GPIO_APP_I2C_TARGET_IOMUX_SCL                          (IOMUX_PINCM_PA9)
#define GPIO_APP_I2C_TARGET_IOMUX_SCL_FUNC               IOMUX_PA9_UC1_RX_SCL_SCLK

void SYSCFG_DL_initPower(void)
{
    DL_GPIO_reset(GPIO0);
    DL_GPIO_reset(GPIO1);
    DL_GPIO_reset(GPIO2);
    DL_GPIO_reset(GPIO3);

    DL_I2CT_reset(APP_I2C_TARGET_INST);

    DL_GPIO_enablePower(GPIO0);
    DL_GPIO_enablePower(GPIO1);
    DL_GPIO_enablePower(GPIO2);
    DL_GPIO_enablePower(GPIO3);

    DL_I2CT_enablePower(APP_I2C_TARGET_INST);
}

void SYSCFG_DL_Pinmux_init(void)
{
    DL_GPIO_initPeripheralAnalogFunction(IOMUX_PINCM_PC16_X1);
    DL_GPIO_initPeripheralAnalogFunction(IOMUX_PINCM_PC17_X2);

	DL_GPIO_initPeripheralInputFunctionFeatures(
		 GPIO_APP_I2C_CONTROLLER_IOMUX_SDA, GPIO_APP_I2C_CONTROLLER_IOMUX_SDA_FUNC,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_PULL_UP,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
	DL_GPIO_initPeripheralInputFunctionFeatures(
		 GPIO_APP_I2C_CONTROLLER_IOMUX_SCL, GPIO_APP_I2C_CONTROLLER_IOMUX_SCL_FUNC,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_PULL_UP,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
    DL_GPIO_enableHiZ(GPIO_APP_I2C_CONTROLLER_IOMUX_SDA);
    DL_GPIO_enableHiZ(GPIO_APP_I2C_CONTROLLER_IOMUX_SCL);

	DL_GPIO_initPeripheralInputFunctionFeatures(
		 GPIO_APP_I2C_TARGET_IOMUX_SDA, GPIO_APP_I2C_TARGET_IOMUX_SDA_FUNC,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_PULL_UP,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
	DL_GPIO_initPeripheralInputFunctionFeatures(
		 GPIO_APP_I2C_TARGET_IOMUX_SCL, GPIO_APP_I2C_TARGET_IOMUX_SCL_FUNC,
		 DL_GPIO_INVERSION_DISABLE, DL_GPIO_RESISTOR_PULL_UP,
		 DL_GPIO_HYSTERESIS_DISABLE, DL_GPIO_WAKEUP_DISABLE);
    DL_GPIO_enableHiZ(GPIO_APP_I2C_TARGET_IOMUX_SDA);
    DL_GPIO_enableHiZ(GPIO_APP_I2C_TARGET_IOMUX_SCL);
}

static const DL_I2CT_ClockConfig gAPP_I2C_TARGETClockConfig = {
    .clockSel = DL_I2CT_CLOCK_BUSCLK,
    .divideRatio = DL_I2CT_CLOCK_DIVIDE_1,
};

void SYSCFG_DL_APP_I2C_TARGET_init(void) {
    DL_I2CT_setClockConfig(APP_I2C_TARGET_INST,
        (DL_I2CT_ClockConfig *) &gAPP_I2C_TARGETClockConfig);

    /* Configure Target Mode */
    DL_I2CT_setOwnAddress(APP_I2C_TARGET_INST, APP_I2C_TARGET_TARGET_OWN_ADDR);
    DL_I2CT_setTXFIFOThreshold(APP_I2C_TARGET_INST, DL_I2CT_TX_FIFO_LEVEL_1_2_EMPTY);
    DL_I2CT_setRXFIFOThreshold(APP_I2C_TARGET_INST, DL_I2CT_RX_FIFO_LEVEL_NOT_EMPTY);

    /* Clock stretching would deadlock when controller and target share a single
     * CPU with a blocking polling transfer — target can't drain its RX FIFO
     * while the controller is busy-waiting for SCL to be released. */
    DL_I2CT_disableClockStretching(APP_I2C_TARGET_INST);

    /* Enable module */
    DL_I2CT_enable(APP_I2C_TARGET_INST);
}

void dl_target_init(void)
{
    SYSCFG_DL_initPower();
    SYSCFG_DL_Pinmux_init();
    //SYSCFG_DL_APP_I2C_TARGET_init();
}

/* target I2C buffer */
static volatile char rx_buf[128];

static volatile char *rx_buf_ptr = rx_buf;
static volatile int rx_remaining_bytes = 26;

static volatile bool is_rx_complete = false;

void target_receive_bytes(void)
{
    while (!DL_I2CT_isRXFIFOEmpty(APP_I2C_TARGET_INST) && rx_remaining_bytes > 0U)
    {
        *rx_buf_ptr = DL_I2CT_receiveData(APP_I2C_TARGET_INST);
        rx_buf_ptr++;
        rx_remaining_bytes--;
    }

    /* Check for STOP or completion */

    bool isStopDetected = DL_I2CT_getRawInterruptStatus(APP_I2C_TARGET_INST, DL_I2CT_INTERRUPT_STOP);
    if (isStopDetected)
    {
        DL_I2CT_clearInterruptStatus(APP_I2C_TARGET_INST, DL_I2CT_INTERRUPT_STOP);
    }

    /* Transfer is complete if stop is detected or all bytes received */

    is_rx_complete = (isStopDetected && rx_remaining_bytes == 0U) || (rx_remaining_bytes == 0U);
}
