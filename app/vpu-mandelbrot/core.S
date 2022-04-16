.text
.align 2


#define SHIFT 22
#define v_px    H32(0,0)
#define v_py    H32(1,0)
#define v_px2   H32(2,0)
#define v_py2   H32(3,0)
#define v_pxy2  H32(4,0)
#define v_ua    H32(5,0)
#define v_ua_lo    H16(5,0)
#define v_ua_hi    H16(5,32)
#define v_tmp   H32(6,0)
#define v_count H32(7,0)

#define v_u H32(8,0)

#define v_v H32(9,0)
#define v_ustart H32(11,0)
#define v_ustart_lo H16(11,0)
#define v_ustart_hi H16(11,32)



#define v_ua1    H32(12,0)
#define v_ua1_lo    H16(12,0)
#define v_ua1_hi    H16(12,32)
#define v_ua2    H32(13,0)
#define v_ua2_lo    H16(13,0)
#define v_ua2_hi    H16(13,32)
#define v_ua3    H32(14,0)
#define v_ua3_lo    H16(14,0)
#define v_ua3_hi    H16(14,32)

#define v_tmp1   H32(15,0)
#define v_tmp2   H32(16,0)
#define v_tmp3   H32(17,0)

#define MMIO_BASE_PHYS             0x7e000000
#define ST_BASE                    (MMIO_BASE_PHYS + 0x3000)
#define ST_CLO                     (ST_BASE + 0x4)

//#define D 17476

.global mandel_asm

mandel_asm:
  ; r0 = addr of an uint32_t[16]
  ; r1 = ustart
  ; r2 = vstart
  ; r3 = delta
  ; r4 = width
  ; r5 = height

  ; local vars
  ; r5 = output addr, increments by 16*4 bytes after each batch
  ; r6 = width/16
  ; r7 = height

  stm lr,(--sp)
  stm r6-r15, (--sp)


  ; div width and move h to a safe place
  div.uu r6, r4, 16
  mov r7, r5

  ; move dest buffer to a safe place
  mov r5, r0

  mov r0, 11
  v32mov H32(0++, 0), #0 REP r0


  ; i have NOT tested this enough
  v32ld v_ustart, (r5)
  ;;vmul32.uu v_tmp1, v_ustart_lo, r3
  ;;vmull.ss v_ustart_hi, v_ustart_hi, r3
  ;;v32add v_ustart, v_ustart, v_tmp1
  v32add v_ustart, v_ustart, r1


  v32mov v_v, r2


  mov r2, #ST_CLO
  ld r12, (r2)

  mov r10, #0
  do_screen:
    v32mov v_u, v_ustart
    mov r4, #0
    do_line:
      v32mov v_px, #0
      v32mov v_py, #0
      mov r1, #0
      mov r2, #64
      v32mov v_count, r2
      mov r9, #((1<<SHIFT)*16)
      mandel_inner:
        v32dist   v_ua1, v_px, H32(10, 0)
        v32dist   v_ua2, v_py, H32(10, 0)
        v32dist   v_ua3, v_px, v_py

        vmul32.uu v_tmp1, v_ua1_hi, v_ua1_hi
        vmul32.uu v_tmp2, v_ua2_hi, v_ua2_hi
        vmul32.uu v_tmp3, v_ua3_hi, v_ua3_hi

        v32shl    v_px2, v_tmp1, 32-SHIFT
        v32shl    v_py2, v_tmp2, 32-SHIFT
        v32shl    v_pxy2, v_tmp3, 32-SHIFT

        vmul32.uu v_tmp1, v_ua1_lo, v_ua1_hi
        vmul32.uu v_tmp2, v_ua2_lo, v_ua2_hi
        vmul32.uu v_tmp3, v_ua3_lo, v_ua3_hi

        v32lsr    v_tmp1, v_tmp1, SHIFT-17
        v32lsr    v_tmp2, v_tmp2, SHIFT-17
        v32lsr    v_tmp3, v_tmp3, SHIFT-17

        v32add    v_px2, v_px2, v_tmp1
        v32add    v_py2, v_py2, v_tmp2
        v32add    v_pxy2, v_pxy2, v_tmp3

        vmul32.uu v_tmp1, v_ua1_lo, v_ua1_lo
        vmul32.uu v_tmp2, v_ua2_lo, v_ua2_lo
        vmul32.uu v_tmp3, v_ua3_lo, v_ua3_lo

        v32lsr    v_tmp1, v_tmp1, SHIFT
        v32lsr    v_tmp2, v_tmp2, SHIFT
        v32lsr    v_tmp3, v_tmp3, SHIFT

        v32add    v_px2, v_px2, v_tmp1
        v32add    v_py2, v_py2, v_tmp2
        v32add    v_pxy2, v_pxy2, v_tmp3

        v32sub v_px, v_px2, v_py2
        v32add v_px, v_px, v_u
        v32add v_py, v_px2, v_py2

        v32mov v_tmp1, r1
        v32sub -, v_py, r9 SETF
        v32min v_count, v_count, v_tmp1 IFNN

        v32sub v_py, v_py, v_pxy2
        v32add v_py, v_py, v_v

        addcmpblt r1, #1, r2, mandel_inner

      v32shl v_count, v_count, #(2+8+8)
      v32st v_count, (r5)
      add r5, 16*4

      shl r8, r3, 4
      v32add v_u, v_u, r8

      ; has $width pixels been calculated yet?
      addcmpblt r4, #1, r6, do_line

   v32add v_v, v_v, r3

   ; has $height scanlines been calculated yet?
   addcmpblt r10, #1, r7, do_screen


  mov r2, #ST_CLO
  ld r0, (r2)

  sub r0, r12

  ldm r6-r15, (sp++)
  ldm pc,(sp++)

