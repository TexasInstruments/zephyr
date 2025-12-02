#include <zephyr/ipc/ipc_service.h>
#include <zephyr/kernel.h>

#define TOTAL_PINGS 10000

volatile bool bound = false;
struct ipc_ept ept0;
const struct device *inst0;
int ret;

char recv_str[32] = "";

static K_SEM_DEFINE(recv_complete, 0, 1);
static K_SEM_DEFINE(ept_bound, 0, 1);

static void bound_cb(void *priv)
{
   /* Endpoint bounded */
   bound = true;
}

static void recv_cb(const void *data, size_t len, void *priv)
{
	static uint32_t num_enters = 0;
   /* Data received */
   memcpy(recv_str, data, len);

	num_enters++;

   k_sem_give(&recv_complete);
}

static struct ipc_ept_cfg ept0_cfg = {
   .name = "my_ping_service",
   .cb = {
      .bound    = bound_cb,
      .received = recv_cb,
   },
};

int main(void)
{
   inst0 = DEVICE_DT_GET(DT_NODELABEL(ipc0));
   ret = ipc_service_open_instance(inst0);
   ret = ipc_service_register_endpoint(inst0, &ept0, &ept0_cfg);

   /* handshake for the rtos core | we are re-using the status*/
   uint32_t hs_addr = 0x72000000;

   uint32_t hs1_value = 0x05050505;
   uint32_t hs2_value = 0x4;

   *((volatile uint32_t *)(hs_addr)) = hs1_value;
   while(*((volatile uint32_t *)(hs_addr)) != hs2_value) {
      // k_sleep(K_MSEC(10));
   }

   /* Wait for endpoint bound (bound_cb called) */
   while (!bound) {
	   printk("Main : waiting for endpoint to be bound...\r\n");
      k_sleep(K_MSEC(100));
   }
   printk("endpoint bound\r\n");

   for (int i = 0; i < 8*TOTAL_PINGS; i++) {

      char msgStr[32] = "";

      sprintf(msgStr, "ping id : %d",i);

      printk("Sending from r5f0-0 %d\r\n", i);

      ret = ipc_service_send(&ept0, &msgStr, sizeof(msgStr));

      k_sem_take(&recv_complete, K_FOREVER);

      printk("Recieved : %s\r\n", recv_str);
   }

   printk("Main : Test Complete");

   while(1){
      k_sleep(K_MSEC(1000));
   }
}
