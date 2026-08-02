#include "stm32f1xx_hal.h"

uint32_t SystemCoreClock = 8000000U;

void SystemInit(void) {
    /* Run on HSI 8 MHz default. */
}

void SysTick_Handler(void) {
    HAL_IncTick();
}

int main(void) {
    HAL_Init(); /* Configures SysTick for a 1 ms tick + TICKINT. */

    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);

    while (1) {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        uint32_t start = HAL_GetTick();
        /* Sleep (WFI) until 500 SysTick ticks elapse. Each WFI is the
         * industry-standard fast-forward target: a simulator (QEMU sleep=no,
         * Simics hypersimulation, micro-forge P2.a) skips straight to the
         * next SysTick IRQ instead of busy-stepping the 8000-cycle wait. */
        while ((HAL_GetTick() - start) < 500) {
            __WFI();
        }
    }
}
