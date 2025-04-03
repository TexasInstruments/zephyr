#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/device.h>
#include <stdio.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/tisci_clock_control.h>

int main(void)
{
	
	uint64_t rate = 0;
	uint32_t rate32 = 0;
	const struct device *clock_dev;
	clock_dev = TISCI_GET_CLOCK(uart0);
	struct clock_config req = TISCI_GET_CLOCK_DETAILS(uart0);

	if (!clock_control_get_rate(clock_dev, &req, &rate32)) {
		printf("\nCurrent clock rate is:%u\n", rate32);
	}
	rate = 96000000;
	if (!clock_control_set_rate(clock_dev, &req, &rate)) {
		printf("Clock rate 96000000 makes this unreadable");
	}
	rate = 48000000;
	if (!clock_control_set_rate(clock_dev, &req, &rate)) {
		printf("\nClock rate 48000000 makes this readable");
	}
	if (!clock_control_get_rate(clock_dev, &req, &rate32)) {
		printf("\nCurrent clock rate is:%u\n", rate32);
	}
	printf("Clock status %d\n" ,clock_control_get_status(clock_dev, &req));

	return 0;
}