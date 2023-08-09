MEMORY {
  ram (rwx) : ORIGIN = 0x80000000, LENGTH = 128K
}
GROUP(-lgcc)
SECTIONS {
  _start = .;
  .vectorTable : {
    . = 0x0;
    KEEP(*(.data.vectorTable))
  }
  .text : {
    _text = .;
    KEEP(*(.text.start))
    *(.text)
    *(.text.*)
    *(.gnu.warning)
    _etext = .;
  }

  .rodata : {
    *(.rodata)
    *(.rodata.*)
    _erodata = .;
  }

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
    . = ALIGN(16);
    PROVIDE(__ctor_list = .);
    KEEP(*(.init_array))
    KEEP(*(.init_array.*))
    KEEP(*(.ctors))
    KEEP(*(.ctors.*))
    __ctor_end = .;
  }

  .data : {
    *(.data)
    *(.data.*)
    _edata = .;
  }

  .bss : {
    . = ALIGN(4);
    _fbss = .;
    *(.bss)
    *(.bss.*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    _end = .;
  }
  _fstack = ORIGIN(ram) + LENGTH(ram) - 4;
}
/* ASSERT((_end - _start) <= LENGTH(ram), "bss wont fit within L2") */
