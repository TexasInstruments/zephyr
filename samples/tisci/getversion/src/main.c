#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <zephyr/drivers/firmware/tisci/ti_sci.h>

int main(void)
{
	const struct device *dmsc = DEVICE_DT_GET(DT_NODELABEL(dmsc));
	ti_sci_cmd_get_revision(dmsc);
	return 0;
}