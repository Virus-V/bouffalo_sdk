#include <bl808_glb.h>
#include <bflb_mtimer.h>
#include <bflb_flash.h>
#include <bflb_gpio.h>

#include <board.h>
#include <log.h>

#include <rpmsg_lite.h>
#include <rpmsg_ns.h>
#include <rpmsg_queue.h>

#include "bl808_ipc.h"

char rx_msg[64];
static char helloMsg[64-16];

struct rpmsg_lite_instance *ipc_rpmsg;
struct rpmsg_lite_endpoint *ipc_rpmsg_default_endpoint;

rpmsg_ns_handle ipc_rpmsg_ns;
rpmsg_queue_handle ipc_rpmsg_queue;

void ipc_rpmsg_ns_callback(uint32_t new_ept, const char *new_ept_name, uint32_t flags, void *user_data)
{
    LOG_W("RPMSG TODO NS Callback Ran!\r\n");
    LOG_W("Endpoint: %s - endpoint %d - flags %d\r\n", new_ept_name, new_ept, flags);
}

int32_t rpmsg_bm_rx_cb(void *payload, uint32_t payload_len, uint32_t src, void *priv)
{
    //printf("payload: %p, %d, %d\r\n", payload, payload_len, src);
    return RL_RELEASE;
}

void ipc_m0_callback(uint32_t src)
{
    //LOG_I("IPC: MO Callback %x\r\n", src);
    switch (ffs(src) - 1) {
        case IPC_MSG_PING:
            break;
        case IPC_MSG_PONG:
            /* nothing todo */
            break;
        case IPC_MSG_RPMSG0:
        case IPC_MSG_RPMSG1:
        case IPC_MSG_RPMSG2:
        case IPC_MSG_RPMSG3:
            /* env_isr is in the porting layer of rpmsg-lite */
            //LOG_D("IPC: Got Notify for Vector %d\r\n", ffs(src) - IPC_MSG_RPMSG0 - 1);
            env_isr(ffs(src) - IPC_MSG_RPMSG0 - 1);
            break;
    }
}

void ipc_lp_callback(uint32_t src)
{
    //LOG_D("IPC: LP Callback %x\r\n", src);
    while(1);
}

int main(void)
{
    LOG_I("Starting....\r\n");
    board_init();

    LOG_I("Starting 0x64_coproc on %d\r\n", GLB_Get_Core_Type());
    LOG_I("Flash Offset at 0x%08lx\r\n", bflb_flash_get_image_offset());

    IPC_D0_Init(/* m0 callback */ ipc_m0_callback, /* lp callback*/ ipc_lp_callback);

    printf("count: %d, RL_VRING_OVERHEAD:%d\r\n", RL_WORD_ALIGN_DOWN(XRAM_RINGBUF_SIZE - (uint32_t)RL_VRING_OVERHEAD) / 64, RL_VRING_OVERHEAD);

    ipc_rpmsg = rpmsg_lite_master_init((uintptr_t *)XRAM_RINGBUF_ADDR, XRAM_RINGBUF_SIZE, RL_PLATFORM_BL808_M0_LINK_ID, RL_NO_FLAGS);
    if (ipc_rpmsg == RL_NULL) {
        LOG_E("Failed to create rpmsg\r\n");
        while(1);
    }

    LOG_D("rpmsg addr %lx, remaining %ld, total: %ld\r\n", ipc_rpmsg->sh_mem_base, ipc_rpmsg->sh_mem_remaining, ipc_rpmsg->sh_mem_total);

    LOG_D("Waiting for RPMSG link up\r\n");
    while (RL_FALSE == rpmsg_lite_is_link_up(ipc_rpmsg)) {}
    LOG_D("RPMSG link up\r\n");

    ipc_rpmsg_ns = rpmsg_ns_bind(ipc_rpmsg, ipc_rpmsg_ns_callback, NULL);
    if (ipc_rpmsg_ns == RL_NULL) {
        LOG_E("Failed to bind RPMSG NS\r\n");
        while (1) {}
    }
    LOG_D("RPMSG NS binded\r\n");

    ipc_rpmsg_default_endpoint = rpmsg_lite_create_ept(ipc_rpmsg, 16, rpmsg_bm_rx_cb, NULL);
    if (ipc_rpmsg_default_endpoint == RL_NULL) {
        LOG_E("Failed to create RPMSG endpoint\r\n");
        while (1) {}
    }

    memset(helloMsg, 0x55, sizeof(helloMsg));

    uint64_t last_ms = CPU_Get_MTimer_MS();
    uint64_t send_count = 0;
    while (1) {
      uint64_t now;
      int ret = rpmsg_lite_send(ipc_rpmsg, ipc_rpmsg_default_endpoint, 16, (char *)helloMsg, sizeof(helloMsg), RL_BLOCK);
      if (ret == RL_SUCCESS) {
        send_count += sizeof(helloMsg);

        now = CPU_Get_MTimer_MS();

        /* 如果时间大于1s */
        if ((now - last_ms) > 1000) {
          /* 计算这个时间间隔的吞吐 */
          uint32_t throughput = (send_count * 8000) / (now - last_ms);
          LOG_I("sent %d bps, qps:%d\r\n", throughput, send_count / sizeof(helloMsg));
          send_count = 0;
          last_ms = now;
        }
      }
    }

    while(1);
    /* we should never get here */

}
