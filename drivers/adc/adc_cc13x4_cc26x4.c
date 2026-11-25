/*
 * Copyright (c) 2022 Florin Stancu <niflostancu@gmail.com>
 * Copyright (c) 2026 Texas Instruments Incorporated
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_cc13x4_cc26x4_adc

#include <errno.h>

#define LOG_LEVEL CONFIG_ADC_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(adc_cc13x4_cc26x4);

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/drivers/adc.h>
#include <soc.h>

#include <inc/hw_types.h>
#include <driverlib/interrupt.h>
#include <driverlib/ioc.h>
#include <driverlib/rom.h>
#include <driverlib/prcm.h>
#include <driverlib/aux_adc.h>
#include <ti/devices/cc13x4_cc26x4/inc/hw_aux_evctl.h>
#include <inc/hw_aux_aiodio.h>

#define ADC_CONTEXT_USES_KERNEL_TIMER
#include "adc_context.h"

#define MAX_CHAN_ID 0x10

static const uint8_t cc13x4_ch_to_dio[] = {
	23, 24, 25, 26, 27, 28, 29, 30,
};
static const uint8_t cc13x4_ch_to_auxio[] = {
	26, 25, 24, 23, 22, 21, 20, 19,
};

static void cc13x4_adc_pin_configure(uint8_t dio, uint8_t auxio)
{
	uint32_t base = AUX_AIODIO0_BASE + ((auxio / 8U) * 0x1000U);
	uint8_t local = auxio % 8U;

	IOCPinTypeAux(dio);

	HWREG(base + AUX_AIODIO_O_IOMODE) =
		(HWREG(base + AUX_AIODIO_O_IOMODE) & ~(0x3U << (2U * local))) |
		(0x1U << (2U * local));

	HWREG(base + AUX_AIODIO_O_GPIODIE) &= ~(1U << local);
}

struct adc_cc13x4_cc26x4_sample_time_entry {
	uint16_t time_us;
	uint8_t reg_value;
};

static const struct adc_cc13x4_cc26x4_sample_time_entry adc_cc13x4_cc26x4_sample_times[] = {
	{2, AUXADC_SAMPLE_TIME_2P7_US},      {5, AUXADC_SAMPLE_TIME_5P3_US},
	{10, AUXADC_SAMPLE_TIME_10P6_US},    {21, AUXADC_SAMPLE_TIME_21P3_US},
	{42, AUXADC_SAMPLE_TIME_42P6_US},    {85, AUXADC_SAMPLE_TIME_85P3_US},
	{170, AUXADC_SAMPLE_TIME_170_US},    {341, AUXADC_SAMPLE_TIME_341_US},
	{682, AUXADC_SAMPLE_TIME_682_US},    {1370, AUXADC_SAMPLE_TIME_1P37_MS},
	{2730, AUXADC_SAMPLE_TIME_2P73_MS},  {5460, AUXADC_SAMPLE_TIME_5P46_MS},
	{10900, AUXADC_SAMPLE_TIME_10P9_MS},
};

struct adc_cc13x4_cc26x4_data {
	struct adc_context ctx;
	const struct device *dev;
	uint32_t ref_source;
	uint8_t sample_time;
	uint16_t *buffer;
	uint16_t *repeat_buffer;
};

struct adc_cc13x4_cc26x4_cfg {
	unsigned long base;
	void (*irq_cfg_func)(void);
};

static void adc_context_start_sampling(struct adc_context *ctx)
{
	struct adc_cc13x4_cc26x4_data *data = CONTAINER_OF(ctx, struct adc_cc13x4_cc26x4_data, ctx);

	data->repeat_buffer = data->buffer;

	AUXADCEnableSync(data->ref_source, data->sample_time, AUXADC_TRIGGER_MANUAL);
	AUXADCGenManualTrigger();
}

static void adc_context_update_buffer_pointer(struct adc_context *ctx, bool repeat)
{
	struct adc_cc13x4_cc26x4_data *data = CONTAINER_OF(ctx, struct adc_cc13x4_cc26x4_data, ctx);

	if (repeat) {
		data->buffer = data->repeat_buffer;
	} else {
		data->buffer++;
	}
}

static int adc_cc13x4_cc26x4_init(const struct device *dev)
{
	struct adc_cc13x4_cc26x4_data *data = dev->data;
	const struct adc_cc13x4_cc26x4_cfg *config = dev->config;

	data->dev = dev;

	AUXADCDisable();

	HWREG(AUX_EVCTL_BASE + AUX_EVCTL_O_EVTOMCUFLAGSCLR) =
		(AUX_EVCTL_EVTOMCUFLAGS_AUX_ADC_IRQ | AUX_EVCTL_EVTOMCUFLAGS_AUX_ADC_DONE);

	config->irq_cfg_func();

	adc_context_unlock_unconditionally(&data->ctx);
	return 0;
}

static int adc_cc13x4_cc26x4_channel_setup(const struct device *dev,
					   const struct adc_channel_cfg *channel_cfg)
{
	struct adc_cc13x4_cc26x4_data *data = dev->data;
	const uint8_t ch = channel_cfg->channel_id;
	uint16_t sample_time_us = 0;
	uint8_t i;

	if (ch > MAX_CHAN_ID) {
		LOG_ERR("Channel 0x%X is not supported, max 0x%X", ch, MAX_CHAN_ID);
		return -EINVAL;
	}

	switch (ADC_ACQ_TIME_UNIT(channel_cfg->acquisition_time)) {
	case ADC_ACQ_TIME_TICKS:
		data->sample_time = (uint16_t)ADC_ACQ_TIME_VALUE(channel_cfg->acquisition_time);
		break;
	case ADC_ACQ_TIME_MICROSECONDS:
		sample_time_us = (uint16_t)ADC_ACQ_TIME_VALUE(channel_cfg->acquisition_time);
		break;
	case ADC_ACQ_TIME_NANOSECONDS:
		sample_time_us =
			(uint16_t)(ADC_ACQ_TIME_VALUE(channel_cfg->acquisition_time) * 1000);
		break;
	default:
		data->sample_time = AUXADC_SAMPLE_TIME_170_US;
		break;
	}
	if (sample_time_us) {
		data->sample_time = adc_cc13x4_cc26x4_sample_times[0].reg_value;
		for (i = 0; i < ARRAY_SIZE(adc_cc13x4_cc26x4_sample_times); i++) {
			if (adc_cc13x4_cc26x4_sample_times[i].time_us >= sample_time_us) {
				break;
			}
			data->sample_time = adc_cc13x4_cc26x4_sample_times[i].reg_value;
		}
		if (i >= ARRAY_SIZE(adc_cc13x4_cc26x4_sample_times)) {
			LOG_ERR("Acquisition time is not valid");
			return -EINVAL;
		}
	}

	if (channel_cfg->differential) {
		LOG_ERR("Differential channels are not supported");
		return -EINVAL;
	}

	if (channel_cfg->gain != ADC_GAIN_1) {
		LOG_ERR("Gain is not valid");
		return -EINVAL;
	}

	if (channel_cfg->reference == ADC_REF_INTERNAL) {
		data->ref_source = AUXADC_REF_FIXED;
	} else if (channel_cfg->reference == ADC_REF_VDD_1) {
		data->ref_source = AUXADC_REF_VDDS_REL;
	} else {
		LOG_ERR("Reference is not valid");
		return -EINVAL;
	}

	LOG_DBG("Setup %d acq time %d", ch, data->sample_time);

	AUXADCDisable();
	AUXADCSelectInput(ch);

	if (ch >= ADC_COMPB_IN_AUXIO7 && ch <= ADC_COMPB_IN_AUXIO0) {
		uint8_t idx = ch - ADC_COMPB_IN_AUXIO7;

		cc13x4_adc_pin_configure(cc13x4_ch_to_dio[idx], cc13x4_ch_to_auxio[idx]);
		LOG_DBG("ch 0x%02X -> DIO%u AUXIO%u (analog input)", ch, cc13x4_ch_to_dio[idx],
			cc13x4_ch_to_auxio[idx]);
	}

	return 0;
}

static int adc_cc13x4_cc26x4_read_common(const struct device *dev,
					 const struct adc_sequence *sequence, bool asynchronous,
					 struct k_poll_signal *sig)
{
	struct adc_cc13x4_cc26x4_data *data = dev->data;
	int rv;
	size_t exp_size;

	if (sequence->resolution != 12) {
		LOG_ERR("Only 12 Resolution is supported, but %d got", sequence->resolution);
		return -EINVAL;
	}

	exp_size = sizeof(uint16_t);
	if (sequence->options) {
		exp_size *= (1 + sequence->options->extra_samplings);
	}

	if (sequence->buffer_size < exp_size) {
		LOG_ERR("Required buffer size is %u, but %u got", exp_size, sequence->buffer_size);
		return -ENOMEM;
	}

	data->buffer = sequence->buffer;

	adc_context_lock(&data->ctx, asynchronous, sig);
	adc_context_start_read(&data->ctx, sequence);
	rv = adc_context_wait_for_completion(&data->ctx);
	adc_context_release(&data->ctx, rv);
	return rv;
}

static void adc_cc13x4_cc26x4_isr(const struct device *dev)
{
	struct adc_cc13x4_cc26x4_data *data = dev->data;
	uint32_t ev_status =
		(HWREG(AUX_EVCTL_BASE + AUX_EVCTL_O_EVTOMCUFLAGS) &
		 (AUX_EVCTL_EVTOMCUFLAGS_AUX_ADC_IRQ | AUX_EVCTL_EVTOMCUFLAGS_AUX_ADC_DONE));
	uint32_t fifo_status;
	uint32_t adc_value;

	HWREG(AUX_EVCTL_BASE + AUX_EVCTL_O_EVTOMCUFLAGSCLR) = ev_status;
	fifo_status = AUXADCGetFifoStatus();
	LOG_DBG("ISR flags 0x%08X fifo 0x%08X", ev_status, fifo_status);
	if ((fifo_status & (AUX_ANAIF_ADCFIFOSTAT_OVERFLOW | AUX_ANAIF_ADCFIFOSTAT_UNDERFLOW))) {
		AUXADCFlushFifo();
	}
	if ((fifo_status & AUX_ANAIF_ADCFIFOSTAT_EMPTY_M)) {
		return;
	}
	adc_value = AUXADCPopFifo();
	LOG_DBG("ADC buf %04X val %d", (unsigned int)data->buffer, adc_value);
	*data->buffer = adc_value;
	AUXADCDisable();

	adc_context_on_sampling_done(&data->ctx, dev);
}

static int adc_cc13x4_cc26x4_read(const struct device *dev, const struct adc_sequence *sequence)
{
	return adc_cc13x4_cc26x4_read_common(dev, sequence, false, NULL);
}

#ifdef CONFIG_ADC_ASYNC
static int adc_cc13x4_cc26x4_read_async(const struct device *dev,
					const struct adc_sequence *sequence,
					struct k_poll_signal *async)
{
	return adc_cc13x4_cc26x4_read_common(dev, sequence, true, async);
}
#endif

static DEVICE_API(adc, cc13x4_cc26x4_driver_api) = {
	.channel_setup = adc_cc13x4_cc26x4_channel_setup,
	.read = adc_cc13x4_cc26x4_read,
#ifdef CONFIG_ADC_ASYNC
	.read_async = adc_cc13x4_cc26x4_read_async,
#endif
	.ref_internal = 4300,
};

#define CC13x4_CC26x4_ADC_INIT(index)                                                              \
	static void adc_cc13x4_cc26x4_cfg_func_##index(void)                                       \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(index), DT_INST_IRQ(index, priority),                     \
			    adc_cc13x4_cc26x4_isr, DEVICE_DT_INST_GET(index), 0);                  \
		irq_enable(DT_INST_IRQN(index));                                                   \
	}                                                                                          \
	static const struct adc_cc13x4_cc26x4_cfg adc_cc13x4_cc26x4_cfg_##index = {                \
		.base = DT_INST_REG_ADDR(index),                                                   \
		.irq_cfg_func = adc_cc13x4_cc26x4_cfg_func_##index,                                \
	};                                                                                         \
	static struct adc_cc13x4_cc26x4_data adc_cc13x4_cc26x4_data_##index = {                    \
		ADC_CONTEXT_INIT_TIMER(adc_cc13x4_cc26x4_data_##index, ctx),                       \
		ADC_CONTEXT_INIT_LOCK(adc_cc13x4_cc26x4_data_##index, ctx),                        \
		ADC_CONTEXT_INIT_SYNC(adc_cc13x4_cc26x4_data_##index, ctx),                        \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(index, &adc_cc13x4_cc26x4_init, NULL,                                \
			      &adc_cc13x4_cc26x4_data_##index, &adc_cc13x4_cc26x4_cfg_##index,     \
			      POST_KERNEL, CONFIG_ADC_INIT_PRIORITY, &cc13x4_cc26x4_driver_api);

DT_INST_FOREACH_STATUS_OKAY(CC13x4_CC26x4_ADC_INIT)
