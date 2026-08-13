/*
 * based heavily on https://github.com/ptesarik/vc4boot
 */

OUTPUT_ARCH(vc4)
ENTRY(_start)

MEMORY {
  /* in the 8 alias, L2 cached
   */
  ram (rwx) : ORIGIN = 0xC0000000 + 128M, LENGTH = 32M
}
SECTIONS {
  .text : {
    PROVIDE(_start = .);
    . = ALIGN(4);
    _stext = .;
    *(.text .stub .text.* .gnu.linkonce.t.*)
    *(.gnu.warning)
    KEEP (*(.init))
    KEEP (*(.fini))
    KEEP (*(.jcr))
    _etext = .;
  } >ram =0

  .text.bootloader_state : {
    KEEP(*(.bootloader_state))
  }
  .text.firmware_rev : {
    KEEP(*(.firmware_rev))
  }

  .rodata : {
    . = ALIGN(4);
    _frodata = .;
    *(.rodata .rodata.* .gnu.linkonce.r.*)
    *(.rodata1)
    /* VCE coprocessor kernels: machine code for a different instruction set,
       compiled from C in a .vce section and embedded here as data for the VC4
       side to stage into the coprocessor's code RAM. */
    . = ALIGN(4);
    KEEP(*(.vce .vce.*))
    _erodata = .;
  } >ram

  commands : {
    PROVIDE(__start_commands = .);
    KEEP(*(commands))
    PROVIDE(__stop_commands = .);
  } >ram

  /*
   * Without this, --gc-sections drops the `apps` input section (nothing
   * references the descriptors) and __start_apps/__stop_apps stay
   * weak-undefined at 0, so apps_init() iterates over an empty range and never
   * starts the shell. Boot then reaches "initializing apps" and idles
   * forever.
   */
  apps : {
    PROVIDE(__start_apps = .);
    KEEP(*(apps))
    PROVIDE(__stop_apps = .);
  } >ram

  fs_impl : {
    PROVIDE(__start_fs_impl = .);
    KEEP(*(fs_impl))
    PROVIDE(__stop_fs_impl = .);
  } > ram

  usb_hooks : {
    PROVIDE(__start_usb_hooks = .);
    KEEP(*(usb_hooks))
    PROVIDE(__stop_usb_hooks = .);
  } > ram

  .init_array : {
    PROVIDE (__init_array_start = .);
    __ctor_list = .;
    *(.init_array)
    *(.init_array.*)
    *(.ctors)
    *(.ctors.*)
    __init_array_end = .;
    __ctor_end = .;
  } >ram

  .vectorTable : ALIGN(512) {
    *(.data.vectorTable)
  } >ram

  .rel : { *(.*.rel*) }

  .data : {
    . = ALIGN(4);
    _fdata = .;
    *(.data .data.* .gnu.linkonce.d.*)
    *(.data1)
    SORT(CONSTRUCTORS)
    *(.sdata .sdata.* .gnu.linkonce.s.*)
    _edata = .;
  } >ram

  .bss : {
    . = ALIGN(4);
    _fbss = .;
    *(.dynsbss)
    *(.sbss .sbss.* .gnu.linkonce.sb.*)
    *(.scommon)
    *(.dynbss)
    *(.bss .bss.* .gnu.linkonce.b.*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    _end = .;
    PROVIDE (end = .);
  } >ram

  /* First location in stack is highest address in RAM */
  PROVIDE(_fstack = ORIGIN(ram) + LENGTH(ram) - 4);
}
