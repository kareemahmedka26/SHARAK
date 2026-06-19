/*
 * startup.c — minimal Cortex-M3 startup for the QEMU lm3s6965evb.
 *
 * Provides the interrupt vector table, the reset handler that initializes
 * .data/.bss, and a catch-all default handler. No CMSIS, no vendor SDK --
 * this is the real "what actually happens at boot" code, kept tiny on purpose.
 *
 * WHY this exists (REQ-FW-002): we link with -nostdlib, so there is NO crt0
 * and NO vendor CRT to set up the C runtime for us. Before main() can run, two
 * things the C standard lets a program assume must be made true by hand:
 *   1. Initialized globals (.data) must hold their initial VALUES. Those values
 *      are stored in FLASH (non-volatile) but the variables live in RAM, so the
 *      bytes must be COPIED from flash to RAM at boot.
 *   2. Zero-initialized globals (.bss) must read as zero. RAM contents are
 *      undefined at power-on, so .bss must be explicitly cleared.
 * Reset_Handler does exactly those two loops, then calls main().
 *
 * Cortex-M boot protocol (architectural, not vendor-specific): on reset the
 * core loads two 32-bit words from the vector table at 0x00000000 — word[0]
 * into the Main Stack Pointer (MSP), word[1] into the PC. That is why entry 0
 * of g_vectors below is the initial stack pointer VALUE, not a function pointer.
 */
#include <stdint.h>

extern int main(void);

/*
 * Symbols provided by the linker script (firmware/lm3s6965.ld). They are
 * declared as objects only so we can take their ADDRESSES; their "values" are
 * meaningless — `&_sdata` is the address the linker placed, which is what the
 * copy/zero loops actually need.
 */
extern uint32_t _sidata;   /* .data load address (LMA) in FLASH         */
extern uint32_t _sdata;    /* .data run start  (VMA) in RAM             */
extern uint32_t _edata;    /* .data run end    (VMA) in RAM             */
extern uint32_t _sbss;     /* .bss start in RAM                          */
extern uint32_t _ebss;     /* .bss end in RAM                            */
extern uint32_t _estack;   /* top of stack (end of RAM); see linker .ld  */

void Reset_Handler(void);
void Default_Handler(void);

typedef void (*vector_t)(void);

/*
 * Interrupt vector table. Placed in its own section (.isr_vector) which the
 * linker script forces to the very start of FLASH (address 0x00000000) with
 * KEEP() so --gc-sections cannot discard it. The first 16 system entries are
 * all the lm3s6965evb needs to boot under QEMU; device IRQs would follow.
 *
 * `used` stops the optimizer from dropping a table it thinks is unreferenced;
 * `const` lets it live in read-only FLASH alongside .text.
 */
__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[] = {
    (vector_t)(&_estack),  /*  0: initial stack pointer (loaded into MSP)   */
    Reset_Handler,         /*  1: reset  (loaded into PC at power-on)        */
    Default_Handler,       /*  2: NMI                                        */
    Default_Handler,       /*  3: HardFault                                  */
    Default_Handler,       /*  4: MemManage                                  */
    Default_Handler,       /*  5: BusFault                                   */
    Default_Handler,       /*  6: UsageFault                                 */
    0, 0, 0, 0,            /*  7-10: reserved (must be zero)                 */
    Default_Handler,       /* 11: SVCall                                     */
    Default_Handler,       /* 12: Debug Monitor                             */
    0,                     /* 13: reserved                                   */
    Default_Handler,       /* 14: PendSV                                     */
    Default_Handler        /* 15: SysTick                                    */
};

void Reset_Handler(void)
{
    /*
     * Copy initialized data from its FLASH load address (_sidata, the LMA) to
     * its RAM run address (_sdata.._edata, the VMA). This word-by-word copy is
     * the concrete reason .data has a separate LMA and VMA in the linker
     * script: a global like `int x = 5;` has its `5` baked into flash but is
     * read/written in RAM at runtime.
     */
    uint32_t *src = &_sidata;
    for (uint32_t *dst = &_sdata; dst < &_edata; ) {
        *dst++ = *src++;
    }

    /* Zero-initialize the .bss section (uninitialized / =0 globals). */
    for (uint32_t *b = &_sbss; b < &_ebss; ) {
        *b++ = 0u;
    }

    (void)main();

    for (;;) { /* main() is not expected to return; trap if it does */ }
}

void Default_Handler(void)
{
    for (;;) { /* trap unexpected exceptions so faults are observable, not silent */ }
}
