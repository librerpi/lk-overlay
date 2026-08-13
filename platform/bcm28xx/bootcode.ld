MEMORY {
  ram (rwx) : ORIGIN = 0x80000000, LENGTH = 128K
}
SECTIONS {
  . = ORIGIN(ram);
  _start = .;
  .vectorTable : {
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

  lk_init : {
    __start_lk_init = .;
    KEEP(*(lk_init))
    __stop_lk_init = .;
  } > ram

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
  } > ram

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
  _fstack = ORIGIN(ram) + LENGTH(ram);
}
ASSERT((_end - _start) <= LENGTH(ram), "bss wont fit within L2")
