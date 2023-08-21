.section .rodata
sprite_tga_start:
.incbin "app/chips-challenge/sprite-sheet.tga"
sprite_tga_end:

map_start:
.incbin "app/chips-challenge/maps/map_data.bin"
map_end:

.align 4
.global sprite_tga
sprite_tga:
  .int sprite_tga_start
.global sprite_tga_size
sprite_tga_size:
  .int sprite_tga_end - sprite_tga_start

.align 4
.global map_data
map_data:
  .int map_start
.global level_count
level_count:
  .int (map_end - map_start) / 1024
