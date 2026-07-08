#include <stdint.h>

#define RCC_APB2ENR (*((volatile uint32_t*)0x40021018))
#define GPIOA_CRH (*((volatile uint32_t*)0x40010804))
#define GPIOA_ODR (*((volatile uint32_t*)0x4001080C))
#define GPIOA_BSRR (*((volatile uint32_t*)0x40010810))

void delay(volatile unsigned int count) {
    while (count--)
        ;
}

void reset_handler(void) {
    RCC_APB2ENR |= (1 << 2); /* Enable GPIOA clock */

    /* Set PA5 to output push-pull, 2MHz (CNF5=00, MODE5=10) */
    GPIOA_CRH = (GPIOA_CRH & ~(0xF << 20)) | (0x2 << 20);

    while (1) {
        GPIOA_BSRR = (1 << 5); /* Set PA5 */
        delay(20000);
        GPIOA_BSRR = (1 << (16 + 5)); /* Reset PA5 */
        delay(20000);
    }
}
