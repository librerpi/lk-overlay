.section .rodata
sprite_tga_start:
.incbin "app/chips-challenge/sprite-sheet.tga"
sprite_tga_end:

.align 4
.global sprite_tga
sprite_tga:
  .int sprite_tga_start
.global sprite_tga_size
sprite_tga_size:
  .int sprite_tga_end - sprite_tga_start
