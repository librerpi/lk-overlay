/*
 * based heavily on https://github.com/ptesarik/vc4boot
 */

OUTPUT_FORMAT("elf32-vc4", "elf32-vc4", "elf32-vc4")
OUTPUT_ARCH(vc4)
ENTRY(_start)
GROUP(-lgcc)

MEMORY {
  /* in the 8 alias, L2 cached
   */
  ram (rwx) : ORIGIN = 0xC0000000 + 64M, LENGTH = 32M
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

  .rodata : {
    . = ALIGN(4);
    _frodata = .;
    *(.rodata .rodata.* .gnu.linkonce.r.*)
    *(.rodata1)
    _erodata = .;
  } >ram

  commands : {
    PROVIDE(__start_commands = .);
    KEEP(*(commands))
    PROVIDE(__stop_commands = .);
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
