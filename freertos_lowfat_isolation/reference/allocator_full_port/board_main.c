#include "main.h"
#include "cmsis_os.h"
#include <string.h>
#include <stdio.h>
extern int lowfat_run_tests(void);
volatile int lowfat_test_result = -1;
void test_log(const char *message)
{
  (void)HAL_UART_Transmit(&hcom_uart[COM1], (uint8_t *)message, (uint16_t)strlen(message), 1000);
  (void)HAL_UART_Transmit(&hcom_uart[COM1], (uint8_t *)"\r\n", 2, 1000);
}
void LowfatTestTask(void *argument)
{
  (void)argument;
  BSP_LED_Init(LED3);
  COM_InitTypeDef init = {0};
  init.BaudRate = 115200; init.WordLength = COM_WORDLENGTH_8B;
  init.StopBits = COM_STOPBITS_1; init.Parity = COM_PARITY_NONE;
  init.HwFlowCtl = COM_HWCONTROL_NONE;
  if (BSP_COM_Init(COM1, &init) != BSP_ERROR_NONE) {
    lowfat_test_result = -2;
    for (;;) { BSP_LED_Toggle(LED3); osDelay(100); }
  }
  osDelay(2000);
  test_log("LOWFAT standalone: 8 regions x 2048 bytes, sizes 16..128");
  lowfat_test_result = lowfat_run_tests();
  char line[80];
  snprintf(line, sizeof(line), "LOWFAT %s result=%d", lowfat_test_result == 0 ? "PASS" : "FAIL", lowfat_test_result);
  if (lowfat_test_result == 0) BSP_LED_On(LED1);
  for (;;) {
    test_log(line);
    BSP_LED_Toggle(lowfat_test_result == 0 ? LED2 : LED3);
    osDelay(lowfat_test_result == 0 ? 2000 : 500);
  }
}
