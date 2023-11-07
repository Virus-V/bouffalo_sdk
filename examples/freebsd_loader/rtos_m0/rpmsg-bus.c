#include "bl808_glb.h"

#include <stdint.h>
#include <rpmsg_lite.h>
#include <rpmsg_ns.h>
#include <rpmsg_queue.h>

#include <FreeRTOS.h>
#include <stdio.h>
#include "semphr.h"

#define DBG_TAG "RPMSG-BUS"
#include "log.h"

void ipc_m0_callback(uint32_t src)
{
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
            LOG_D("IPC: Got Notify for Vector %d\r\n", ffs(src) - IPC_MSG_RPMSG0 - 1);
            env_isr(ffs(src) - IPC_MSG_RPMSG0 - 1);
            break;
    }
}

void ipc_d0_callback(uint32_t src)
{
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
            LOG_D("IPC: Got Notify for Vector %d\r\n", ffs(src) - IPC_MSG_RPMSG0 - 1);
            env_isr(ffs(src) - IPC_MSG_RPMSG0 - 1);
            break;
    }
}

void ipc_lp_callback(uint32_t src)
{
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

struct rpmsg_lite_instance *ipc_rpmsg;
struct rpmsg_lite_endpoint *ipc_rpmsg_default_endpoint;

rpmsg_ns_handle ipc_rpmsg_ns;
rpmsg_queue_handle ipc_rpmsg_queue;

void rpmsg_task(void *unused)
{
    IPC_M0_Init(ipc_lp_callback, ipc_d0_callback);

    ipc_rpmsg = rpmsg_lite_remote_init((uintptr_t *)XRAM_RINGBUF_ADDR, RL_PLATFORM_BL808_M0_LINK_ID, RL_NO_FLAGS);
    if (ipc_rpmsg == RL_NULL) {
        LOG_E("Failed to create RPMSG instance\r\n");
        vTaskDelete(NULL);
        return;
    }

    ipc_rpmsg_ns = rpmsg_ns_bind(ipc_rpmsg, ipc_rpmsg_ns_callback, NULL);
    if (ipc_rpmsg_ns == RL_NULL) {
        LOG_E("Failed to bind RPMSG NS\r\n");
        vTaskDelete(NULL);
        return;
    }
    LOG_I("RPMSG NS binded\r\n");

    ipc_rpmsg_queue = rpmsg_queue_create(ipc_rpmsg);
    if (ipc_rpmsg_queue == RL_NULL) {
        LOG_E("Failed to create RPMSG queue\r\n");
        vTaskDelete(NULL);
        return;
    }

    ipc_rpmsg_default_endpoint = rpmsg_lite_create_ept(ipc_rpmsg, 17, rpmsg_queue_rx_cb, ipc_rpmsg_queue);
    if (ipc_rpmsg_default_endpoint == RL_NULL) {
        LOG_E("Failed to create RPMSG endpoint\r\n");
        vTaskDelete(NULL);
        return;
    }
    LOG_I("RPMSG endpoint created\r\n");

    LOG_D("Waiting for RPMSG link up...\r\n");
    while (RL_FALSE == rpmsg_lite_is_link_up(ipc_rpmsg)) {
      vTaskDelay(pdMS_TO_TICKS(30));
    }
    LOG_I("RPMSG link up\r\n");

    vTaskDelay(pdMS_TO_TICKS(500));

    if (rpmsg_ns_announce(ipc_rpmsg, ipc_rpmsg_default_endpoint, "rpmsg-test", RL_NS_CREATE) != RL_SUCCESS) {
        LOG_E("Failed to announce RPMSG NS\r\n");
        vTaskDelete(NULL);
        return;
    }

    uint32_t remote_addr, size = 0;
    while (1) {
        if (rpmsg_queue_recv(ipc_rpmsg, ipc_rpmsg_queue, (uint32_t *)&remote_addr, rx_msg, sizeof(rx_msg), &size, RL_BLOCK) == RL_SUCCESS) {
            LOG_I("received %s - %lx!\r\n", rx_msg, remote_addr);
            rpmsg_lite_send(ipc_rpmsg, ipc_rpmsg_default_endpoint, remote_addr, (char *)rx_msg, size, RL_BLOCK);
        }
    }

    vTaskDelete(NULL);
    return;
}
