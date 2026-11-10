/* Stage 1 occupies the first 128 KiB in the same alias. */
MEMORY {
  ram (rwx) : ORIGIN = 0x80020000, LENGTH = 128K
}
PHDRS {
  text PT_LOAD FLAGS(0x7);
  data PT_LOAD FLAGS(0x6);
}
GROUP(-lgcc)
SECTIONS {
  _start = .;
  .vectorTable : {
    . = 0x0;
    KEEP(*(.data.vectorTable))
  } :text
  .text : {
    _text = .;
    KEEP(*(.text.start))
    *(.text)
    *(.text.*)
    *(.gnu.warning)
    _etext = .;
  } :text
  .rodata : {
    *(.rodata)
    *(.rodata.*)
    _erodata = .;
  } :text
  commands : {
    PROVIDE(__start_commands = .);
    KEEP(*(commands))
    PROVIDE(__stop_commands = .);
  } >ram :text
  fs_impl : {
    PROVIDE(__start_fs_impl = .);
    KEEP(*(fs_impl))
    PROVIDE(__stop_fs_impl = .);
  } >ram :text
  usb_hooks : {
    PROVIDE(__start_usb_hooks = .);
    KEEP(*(usb_hooks))
    PROVIDE(__stop_usb_hooks = .);
  } >ram :text
  .init_array : {
    . = ALIGN(16);
    PROVIDE(__ctor_list = .);
    KEEP(*(.init_array))
    KEEP(*(.init_array.*))
    KEEP(*(.ctors))
    KEEP(*(.ctors.*))
    __ctor_end = .;
  } :text
  /* Keep writable data in a distinct load segment.  Without the page
   * boundary, small source changes can make the linker place .data in the
   * preceding RWE segment while the generated RW segment starts earlier;
   * stage-1 then clears the latter over the former during ELF loading. */
  . = ALIGN(CONSTANT (MAXPAGESIZE));
  .data : {
    *(.data)
    *(.data.*)
    _edata = .;
  } :data
  .bss : {
    . = ALIGN(4);
    _fbss = .;
    *(.bss)
    *(.bss.*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    _end = .;
  } :data
  _fstack = ORIGIN(ram) + LENGTH(ram) - 4;
}
