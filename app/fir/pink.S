.global pink_start
.global pink_end
.data
pink_start:
// 22050Hz 16bit signed
.incbin "../../../app/fir/pink.raw"
pink_end:


.text
.global do_fir
do_fir:
  v16ld HX(0++,32), (r1+=r2) REP r0
  v16ld HX(0++,0),  (r3+=r2) REP r0
  vmul32.ss -, HX(0++,0), HX(0++,32) REP r0 CLRA SACC
  vgetaccs32 -, -, 15 SUMS r0
  rts
