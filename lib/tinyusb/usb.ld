SECTIONS {
  .rodata : {
    PROVIDE(__start_usb_hooks = .);
    KEEP(*(usb_hooks))
    PROVIDE(__stop_usb_hooks = .);
  } > ram
}
