#include "board.h"
#include "bflb_mtimer.h"
#include "bl808_glb.h"

#include <stdint.h>
#include <rpmsg_lite.h>
#include <rpmsg_ns.h>
#include <rpmsg_queue.h>

#include <FreeRTOS.h>
#include "semphr.h"

#define DBG_TAG "MAIN"
#include "log.h"

struct rpmsg_lite_instance *ipc_rpmsg;
struct rpmsg_lite_endpoint *ipc_rpmsg_default_endpoint;

rpmsg_ns_handle ipc_rpmsg_ns;
rpmsg_queue_handle ipc_rpmsg_queue;

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

void ipc_d0_callback(uint32_t src)
{
    //LOG_I("IPC: D0 Callback %x %x\r\n", src, ffs(src));
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
    LOG_D("IPC: LP Callback %x\r\n", src);
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
            env_isr(ffs(src) - IPC_MSG_RPMSG0 - 1);
            break;
    }
}

void ipc_rpmsg_ns_callback(uint32_t new_ept, const char *new_ept_name, uint32_t flags, void *user_data)
{
    LOG_W("RPMSG TODO NS Callback Ran!\r\n");
    LOG_W("Endpoint: %s - endpoint %d - flags %d\r\n", new_ept_name, new_ept, flags);
}

char rx_msg[64-16];
static char helloMsg[64-16];

int32_t rpmsg_bm_rx_cb(void *payload, uint32_t payload_len, uint32_t src, void *priv)
{
    //printf("payload: %p, %d, %d\r\n", payload, payload_len, src);
    return RL_RELEASE;
}

void rpmsg_task(void *unused)
{
    volatile uint32_t remote_addr;
    uint32_t size;

    IPC_M0_Init(/* lp callback*/ ipc_lp_callback, /* d0 callback */ ipc_d0_callback);

    ipc_rpmsg = rpmsg_lite_remote_init((uintptr_t *)XRAM_RINGBUF_ADDR, RL_PLATFORM_BL808_M0_LINK_ID, RL_NO_FLAGS);
    LOG_D("rpmsg addr %lx, remaining %lx, total: %lx\r\n", ipc_rpmsg->sh_mem_base, ipc_rpmsg->sh_mem_remaining, ipc_rpmsg->sh_mem_total);

    ipc_rpmsg_ns = rpmsg_ns_bind(ipc_rpmsg, ipc_rpmsg_ns_callback, NULL);
    if (ipc_rpmsg_ns == RL_NULL) {
        LOG_W("Failed to bind RPMSG NS\r\n");
        vTaskDelete(NULL);
        return;
    }
    LOG_D("RPMSG NS binded\r\n");

    ipc_rpmsg_queue = rpmsg_queue_create(ipc_rpmsg);
    if (ipc_rpmsg_queue == RL_NULL) {
        LOG_W("Failed to create RPMSG queue\r\n");
        vTaskDelete(NULL);
        return;
    }

#if 0
    ipc_rpmsg_default_endpoint = rpmsg_lite_create_ept(ipc_rpmsg, 16, rpmsg_queue_rx_cb, ipc_rpmsg_queue);
#else
    ipc_rpmsg_default_endpoint = rpmsg_lite_create_ept(ipc_rpmsg, 16, rpmsg_bm_rx_cb, NULL);
#endif
    if (ipc_rpmsg_default_endpoint == RL_NULL) {
        LOG_W("Failed to create RPMSG endpoint\r\n");
        vTaskDelete(NULL);
        return;
    }

    LOG_D("RPMSG endpoint created\r\n");

    LOG_D("Waiting for RPMSG link up\r\n");
    while (RL_FALSE == rpmsg_lite_is_link_up(ipc_rpmsg)) {}
    LOG_D("RPMSG link up\r\n");

    if (rpmsg_ns_announce(ipc_rpmsg, ipc_rpmsg_default_endpoint, "rpmsg-test", RL_NS_CREATE) != RL_SUCCESS) {
        LOG_W("Failed to announce RPMSG NS\r\n");
        vTaskDelete(NULL);
        return;
    }

    int ret;

    /* Wait Hello handshake message from Remote Core. */
    ret = rpmsg_queue_recv(ipc_rpmsg, ipc_rpmsg_queue, (uint32_t *)&remote_addr, helloMsg, sizeof(helloMsg), &size,
                                   RL_BLOCK);
    LOG_I("Got Hello message from remote core: %s %lx %d\r\n", helloMsg, remote_addr, size);

    if (ret != RL_SUCCESS) {
        LOG_D("rpmsg_queue_recv return %d\r\n", ret);
        vTaskDelete(NULL);
        return;
    }

    while (1) {
        if (rpmsg_queue_recv(ipc_rpmsg, ipc_rpmsg_queue, (uint32_t *)&remote_addr, rx_msg, sizeof(rx_msg), &size, RL_BLOCK) == RL_SUCCESS) {
            //LOG_I("received %s - %lx!\r\n", rx_msg, remote_addr);
            //rpmsg_lite_send(ipc_rpmsg, ipc_rpmsg_default_endpoint, remote_addr, (char *)rx_msg, size, RL_BLOCK);
        }
    }

    vTaskDelete(NULL);
    return;
}

int main(void)
{
    TaskHandle_t rpmsg_task_handle = NULL;
    board_init();

    LOG_I("E907 start...\r\n");
    LOG_I("mtimer clk:%d; configCPU_CLOCK_HZ:%d\r\n", CPU_Get_MTimer_Clock(), configCPU_CLOCK_HZ);

    configASSERT((configMAX_PRIORITIES > 4));

    LOG_I("About to Start RPMSG Task\r\n");
    if (xTaskCreate(rpmsg_task, "RPMSG_TASK", 2048 + 512, NULL, tskIDLE_PRIORITY + 1U, &rpmsg_task_handle) != pdPASS) {
        printf("\r\nFailed to create rpmsg task\r\n");
        return -1;
    }

    vTaskStartScheduler();

    while (1) {
    }
}
