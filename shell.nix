{ risc ? false, qemu ? false }:

(import ./. { inherit risc qemu; }).shell
