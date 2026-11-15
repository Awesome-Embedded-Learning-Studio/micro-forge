/*
 * hello.c - STM32F103 bare-metal demo: prints "hello world" over the
 * semihosting console every ~3M CPU cycles, forever.
 * Build: arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -O2 -ffreestanding
 *        -nostdlib -Wl,--section-start=.vectors=0x08000000,-Ttext=0x08000100
 */
typedef unsigned int u32;

__attribute__((noreturn)) void reset(void);

static void semi_write0(const char* s) {
    __asm volatile("movs r0, #4\n"  /* SYS_WRITE0 */
                   "mov r1, %0\n"
                   "bkpt 0xAB\n"
                   :
                   : "r"(s)
                   : "r0", "r1", "memory");
}

__attribute__((used, section(".vectors")))
const void* vectors[2] = {(void*)0x20005000U, (void*)reset};

__attribute__((noreturn)) void reset(void) {
    for (;;) {
        semi_write0("hello world\n");
        for (volatile u32 i = 0; i < 500000U; ++i) {
        }
    }
}
