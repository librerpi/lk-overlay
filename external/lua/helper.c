#include <lk/debug.h>
#include <locale.h>
#include <lstring.h>
#include <lua.h>
#include <math.h>
#include <platform/time.h>

double frexp(double x, int *exp) {
  panic("TODO\n");
}

double strtod(const char *nptr, char **endptr) {
  panic("TODO\n");
}

struct lconv *localeconv(void) {
  panic("TODO");
}

typedef struct {
  uint32_t lr, sp;
  uint32_t r6, r7, r8, r9;
  uint32_t r10, r11, r12, r13, r14, r15, r16, r17, r18, r19;
  uint32_t r20, r21, r22, r23;
} vc4_jmp_buf;

void print_jmp_buf(vc4_jmp_buf *buf) {
  printf("lr:0x%x sp:0x%x r6:0x%x r7:0x%x r8:0x%x r9:0x%x\n", buf->lr, buf->sp, buf->r6, buf->r7, buf->r8, buf->r9);
}

void setjmp_post(vc4_jmp_buf *buf) {
  printf("setjmp(%p) save done\n", buf);
  print_jmp_buf(buf);
}

void longjmp_pre(vc4_jmp_buf *buf, uint32_t val, uint32_t lr) {
  printf("longjmp(%p, %d), caller=0x%x\n", buf, val, lr);
  print_jmp_buf(buf);
}

typedef struct {
  uint64_t now;
  void *pubfun;
} entropy;

unsigned int lk_luai_makeseed (void) {
  entropy buff;
  buff.now = current_time_hires();
  buff.pubfun = &lua_newstate;
  return luaS_hash((const char *)&buff, sizeof(buff), buff.now);
}
