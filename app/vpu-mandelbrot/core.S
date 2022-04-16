.text
.align 2

#define SHIFT 22
#define v_zr        H32(0,0)
#define v_zi        H32(1,0)
#define v_zr2       H32(2,0)
#define v_zi2       H32(3,0)
#define v_diff2     H32(4,0)
#define v_zr_abs    H32(5,0)
#define v_zr_abs_lo     H16(5,0)
#define v_zr_abs_hi     H16(5,32)
#define v_zi_abs    H32(6,0)
#define v_zi_abs_lo     H16(6,0)
#define v_zi_abs_hi     H16(6,32)
#define v_diff_abs  H32(7,0)
#define v_diff_abs_lo   H16(7,0)
#define v_diff_abs_hi   H16(7,32)
#define v_cr        H32(8,0)
#define v_ci        H32(9,0)
#define v_crstart   H32(10,0)
#define v_tmp1      H32(11,0)
#define v_tmp2      H32(12,0)
#define v_tmp3      H32(13,0)
#define v_zero      H32(14,0)
#define v_count     H32(15,0)


#define MMIO_BASE_PHYS             0x7e000000
#define ST_BASE                    (MMIO_BASE_PHYS + 0x3000)
#define ST_CLO                     (ST_BASE + 0x4)

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

  mov r0, 15
  v32mov H32(0++, 0), #0 REP r0

  v32ld v_crstart, (r5)
  v32add v_crstart, v_crstart, r1

  v32mov v_ci, r2

  mov r2, #ST_CLO
  ld r12, (r2)

  mov r10, #0
  do_screen:
    v32mov v_cr, v_crstart
    mov r4, #0
    do_line:
      v32mov v_zr, #0
      v32mov v_zi, #0
      mov r1, #0
      mov r2, #64
      v32mov v_count, r2
      mov r9, #((1<<SHIFT)*4)
      mandel_inner:
        ; 32bit fixedpoint numbers, but we only have a 16x16=32 multiplier
        ; we need three squares, zr^2, zi^2 and diff^2, which is (zr-zi)^2,
        ; start by getting the absolute value of those
        v32dist   v_zr_abs, v_zr, v_zero
        v32dist   v_zi_abs, v_zi, v_zero
        v32dist   v_diff_abs, v_zr, v_zi

        ; X = ((X_hi^2) << 32) >> SHIFT where X = {zr, zi, diff}
        vmul32.uu v_tmp1, v_zr_abs_hi, v_zr_abs_hi
        vmul32.uu v_tmp2, v_zi_abs_hi, v_zi_abs_hi
        vmul32.uu v_tmp3, v_diff_abs_hi, v_diff_abs_hi
        v32shl    v_zr2, v_tmp1, 32-SHIFT
        v32shl    v_zi2, v_tmp2, 32-SHIFT
        v32shl    v_diff2, v_tmp3, 32-SHIFT

        ; X += (2 * (X_hi * X_lo) << 16) >> SHIFT
        vmul32.uu v_tmp1, v_zr_abs_lo, v_zr_abs_hi
        vmul32.uu v_tmp2, v_zi_abs_lo, v_zi_abs_hi
        vmul32.uu v_tmp3, v_diff_abs_lo, v_diff_abs_hi
        v32lsr    v_tmp1, v_tmp1, SHIFT-17
        v32lsr    v_tmp2, v_tmp2, SHIFT-17
        v32lsr    v_tmp3, v_tmp3, SHIFT-17
        v32add    v_zr2, v_zr2, v_tmp1
        v32add    v_zi2, v_zi2, v_tmp2
        v32add    v_diff2, v_diff2, v_tmp3

        ; zr = zr^2 - zi^2 + u, mag2 = zr^2 + zi^2
        v32sub v_zr, v_zr2, v_zi2
        v32add v_zr, v_zr, v_cr
        v32add v_zi, v_zr2, v_zi2

        ; if the point has exited, update the count
        v32mov v_tmp1, r1
        v32sub -, v_zi, r9 SETF
        v32min v_count, v_count, v_tmp1 IFNN

        ; zi = 2*zr*zi + v = zr^2 + zi^2 - (zr^2 - 2*zr*zi + zi^2) + ci
        ; zi = mag2 - diff2 + ci
        v32sub v_zi, v_zi, v_diff2
        v32add v_zi, v_zi, v_ci

        addcmpblt r1, #1, r2, mandel_inner

      v32shl v_count, v_count, #(2+8+8)
      v32st v_count, (r5)
      add r5, 16*4

      shl r8, r3, 4
      v32add v_cr, v_cr, r8

      ; have $width pixels been calculated yet?
      addcmpblt r4, #1, r6, do_line

   v32add v_ci, v_ci, r3

  ; have $height scanlines been calculated yet?
  addcmpblt r10, #1, r7, do_screen


  mov r2, #ST_CLO
  ld r0, (r2)

  sub r0, r12

  ldm r6-r15, (sp++)
  ldm pc, (sp++)

