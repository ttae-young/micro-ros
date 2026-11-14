/*
 * STM32L552ZE-Q linker script with the sections required by the
 * FreeRTOS Cortex-M33 MPU port (MPU wrappers v2).
 */

ENTRY(Reset_Handler)

_estack = ORIGIN(RAM) + LENGTH(RAM);
_sstack = _estack - _Min_Stack_Size;

_Min_Heap_Size = 0x200;
_Min_Stack_Size = 0x400;

MEMORY
{
  RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 192K
  RAM2  (xrw) : ORIGIN = 0x20030000, LENGTH = 64K
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
}

__FLASH_segment_start__ = ORIGIN(FLASH);
__FLASH_segment_end__   = ORIGIN(FLASH) + LENGTH(FLASH) - 1;
__SRAM_segment_start__  = ORIGIN(RAM);
__SRAM_segment_end__    = ORIGIN(RAM) + LENGTH(RAM) - 1;

SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  /* FreeRTOS kernel code: privileged execution only. */
  .privileged_functions : ALIGN(32)
  {
    . = ALIGN(32);
    __privileged_functions_start__ = .;
    KEEP(*(privileged_functions))
    . = ALIGN(32);
    __privileged_functions_end__ = . - 1;
  } >FLASH

  /* Entry veneers callable by unprivileged tasks. */
  .freertos_system_calls : ALIGN(32)
  {
    . = ALIGN(32);
    __syscalls_flash_start__ = .;
    KEEP(*(freertos_system_calls))
    . = ALIGN(32);
    __syscalls_flash_end__ = . - 1;
  } >FLASH

  .text : ALIGN(32)
  {
    . = ALIGN(32);
    __unprivileged_flash_start__ = .;
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    KEEP(*(.init))
    KEEP(*(.fini))
    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(32);
    __unprivileged_flash_end__ = . - 1;
  } >FLASH

  .ARM.extab (READONLY) :
  {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >FLASH

  .ARM (READONLY) :
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >FLASH

  .preinit_array (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .init_array (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
    . = ALIGN(4);
  } >FLASH

  .fini_array (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
    . = ALIGN(4);
  } >FLASH

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;

    /* FreeRTOS kernel data: privileged access only. */
    . = ALIGN(32);
    __privileged_sram_start__ = .;
    KEEP(*(privileged_data))
    . = ALIGN(32);
    __privileged_sram_end__ = . - 1;

    *(.data)
    *(.data*)
    *(.RamFunc)
    *(.RamFunc*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT>FLASH

  . = ALIGN(4);
  .bss :
  {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  /* Mutually exclusive allocator payload pools, explicitly initialized.
   * Ordinary allocator: 64 KiB. LowFat: 13 size classes * 4 KiB = 52 KiB.
   * Kernel heap remains in RAM; LowFat metadata also remains in RAM. */
  .allocator_pool (NOLOAD) : ALIGN(4096)
  {
    *(.baseline_heap)
    *(.lowfat_heap)
  } >RAM2

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /DISCARD/ :
  {
    libc.a(*)
    libm.a(*)
    libgcc.a(*)
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
