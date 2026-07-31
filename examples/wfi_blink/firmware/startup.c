#include <stdint.h>

extern uint32_t _estack;
extern uint32_t _sidata;
extern uint32_t _sdata;
extern uint32_t _edata;
extern uint32_t _sbss;
extern uint32_t _ebss;

extern int main(void);
extern void SystemInit(void);
extern void SysTick_Handler(void);

void Reset_Handler(void) {
    uint32_t* src = &_sidata;
    uint32_t* dst = &_sdata;
    while (dst < &_edata) {
        *dst++ = *src++;
    }

    dst = &_sbss;
    while (dst < &_ebss) {
        *dst++ = 0;
    }

    SystemInit();
    main();

    while (1) {
        __asm volatile("nop");
    }
}

__attribute__((section(".isr_vector"))) const uint32_t vector_table[] = {
    (uint32_t)&_estack,      /* 0:  Initial SP */
    (uint32_t)Reset_Handler, /* 1:  Reset */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,                         /* 2-9 */
    0,                         /* 10: reserved */
    0,                         /* 11: SVCall */
    0,                         /* 12: Debug Monitor */
    0,                         /* 13: reserved */
    0,                         /* 14: PendSV */
    (uint32_t)SysTick_Handler, /* 15: SysTick */
};
