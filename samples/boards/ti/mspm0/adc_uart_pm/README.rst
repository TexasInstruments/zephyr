.. zephyr:code-sample:: mspm0_adc_uart_pm
   :name: MSPM0 ADC sampling with power management
   :relevant-api: adc_interface pm_apis

   Sample an ADC input every three seconds while using system and device power
   management on TI MSPM0 LaunchPads.

Overview
********

This sample sleeps for three seconds, reads an ADC channel, prints the raw
value and voltage on the UART console, and repeats. Zephyr's default PM policy
is restricted to ``RUN0/SLEEP0``. The tickless kernel is enabled, and a timer
peripheral acts as the system-timer companion so Cortex-M SysTick can remain
stopped for the sleep interval.

The ADC peripheral and UART console use device runtime PM. The MSPM0 ADC and UART
drivers acquire and release their device policy locks around active transfers;
``CONFIG_PM_POLICY_DEVICE_CONSTRAINTS`` enables those locks.

Board configuration
*******************

The sample's ``boards`` directory selects an ADC channel and a timer peripheral
for each supported board. The timer is configured as the system-timer companion,
and the ADC and UART peripherals are configured for runtime power management.
