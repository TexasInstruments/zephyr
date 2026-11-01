/*
 * Copyright (c) 2026 Conclusive Engineering Sp. z o.o.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdint.h>

#include <zephyr/multi_heap/shared_multi_heap.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ti_wifi_cc3xxx_heap, LOG_LEVEL_INF);

#define WIFI_NODE DT_NODELABEL(wlan0)

#define RAM_REGION_ITEM(node_id, prop, idx)                                                        \
	{                                                                                          \
		.addr = (uintptr_t)DT_REG_ADDR(DT_PHANDLE_BY_IDX(node_id, prop, idx)),             \
		.size = DT_REG_SIZE(DT_PHANDLE_BY_IDX(node_id, prop, idx)),                        \
		.attr = SMH_REG_ATTR_NON_CACHEABLE,                                                \
	},

static struct shared_multi_heap_region ram_regions[] = {
	DT_FOREACH_PROP_ELEM(WIFI_NODE, heap_regions, RAM_REGION_ITEM)};

int ti_wifi_multi_heap_init(void)
{
	int ret;
	size_t total_heap_bytes = 0;

	ret = shared_multi_heap_pool_init();
	if (ret && ret != -EALREADY) {
		LOG_ERR("Failed to initialize shared multi-heap pool (err: %d)", ret);
		return ret;
	}

	for (size_t i = 0; i < ARRAY_SIZE(ram_regions); i++) {
		ret = shared_multi_heap_add(&ram_regions[i], NULL);
		if (ret) {
			LOG_ERR("Failed to add region %zu [Addr: 0x%lx, Size: %zu B] (err: %d)", i,
				(unsigned long)ram_regions[i].addr, ram_regions[i].size, ret);
			continue;
		}

		total_heap_bytes += ram_regions[i].size;

		LOG_INF("Registered region %zu -> Addr: 0x%lx, Size: %zu KB", i,
			(unsigned long)ram_regions[i].addr, ram_regions[i].size / KB(1));
	}

	LOG_INF("Heap for SimpleLink SDK successfully initialized. Total capacity: %zu KB",
		total_heap_bytes / KB(1));

	return (total_heap_bytes > 0 ? 0 : -1);
}
