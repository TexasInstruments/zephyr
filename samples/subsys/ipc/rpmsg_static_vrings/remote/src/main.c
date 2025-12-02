#include <zephyr/ipc/ipc_service.h>

#define TOTAL_PINGS 10

volatile bool bound = false;
const struct device *inst0;
struct ipc_ept ept0;
int ret;

char recv_str[32] = "";

static K_SEM_DEFINE(recv_complete, 0, 1);
static K_SEM_DEFINE(ept_bound, 0, 1);


static void bound_cb(void *priv)
{
   /* Endpoint bounded */
   bound = true;
}

static uint32_t total_recvd = 0;

static void recv_cb(const void *data, size_t len, void *priv)
{
   total_recvd++;

   /* Data received */
   memcpy(recv_str, data, len);

   char message[32] = "";
   snprintf(message, sizeof(message), "Remote : pong %d", total_recvd);

   ret = ipc_service_send(&ept0, &message, sizeof(message));

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
   const struct device *inst0;

   inst0 = DEVICE_DT_GET(DT_NODELABEL(ipc0));
   ret = ipc_service_open_instance(inst0);
   ret = ipc_service_register_endpoint(inst0, &ept0, &ept0_cfg);

   uint32_t hs_addr = 0x72000000;

   uint32_t hs1_value = 0x05050505;
   uint32_t hs2_value = 0x4;

   while(*((volatile uint32_t *)(hs_addr)) != hs1_value) {
      // k_sleep(K_MSEC(10));
   }
   *((volatile uint32_t *)(hs_addr)) = hs2_value;

   /* Wait for endpoint bound (bound_cb called) */
   while (!bound) {
      printk("Remote : waiting for endpoint to be bound...\r\n");
      k_sleep(K_MSEC(100));
   }
   printk("endpoint bound\r\n");

   while(total_recvd < 8*TOTAL_PINGS) {
      printk("Remote : waiting for total_recd to complete, current count : %d\r\n", total_recvd);
      k_sleep(K_MSEC(100));
   }

   printk("Remote : Test Complete\r\n");
   while(1){
      k_sleep(K_MSEC(1000));
   }
}
