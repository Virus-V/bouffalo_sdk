#include "board.h"
#include "bflb_mtimer.h"
#include "bl808_glb.h"

#include <stdint.h>
#include <FreeRTOS.h>
#include "semphr.h"

#define DBG_TAG "MAIN"
#include "log.h"

extern void rpmsg_task(void *unused);
static TaskHandle_t rpmsg_task_handle = NULL;

int main(void)
{
    board_init();

    LOG_I("E907 start...\r\n");
    LOG_I("mtimer clk:%d; configCPU_CLOCK_HZ:%d\r\n", CPU_Get_MTimer_Clock(), configCPU_CLOCK_HZ);

    configASSERT((configMAX_PRIORITIES > 4));

    LOG_I("About to Start RPMSG Task\r\n");
    if (xTaskCreate(rpmsg_task, "RPMSG_TASK", 2048 + 512, NULL, tskIDLE_PRIORITY + 1U, &rpmsg_task_handle) != pdPASS) {
        LOG_F("\r\nFailed to create rpmsg task\r\n");
        return -1;
    }

    vTaskStartScheduler();

    while (1) {
    }
}
