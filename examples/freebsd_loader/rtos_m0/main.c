#include "board.h"
#include "bflb_mtimer.h"
#include "bl808_glb.h"
#include <stdint.h>

#define DBG_TAG "MAIN"
#include "log.h"

int main(void) {
  board_init();

  LOG_I("E907 start...\r\n");
  LOG_I("mtimer clk:%d\r\n", CPU_Get_MTimer_Clock());

  while (1) {
  }
}
