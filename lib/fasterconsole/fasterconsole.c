#include <assert.h>
#include <dev/display.h>
#include <lib/font.h>
#include <lib/gfx.h>
#include <lib/io.h>
#include <lk/debug.h>
#include <lk/init.h>
#include <platform/bcm28xx/hvs.h>
#include <string.h>
#include <lk/console_cmd.h>


void gfxconsole_print_callback(print_callback_t *cb, const char *str, size_t len);
static void gfxconsole_putc(char c);
static int cmd_gfx_dbg(int argc, const console_cmd_args *argv);

#ifdef GFX_DEBUG_HELPERS
static int cmd_setx(int argc, const console_cmd_args *argv);
static int cmd_sety(int argc, const console_cmd_args *argv);
static int cmd_print(int argc, const console_cmd_args *argv);
static int cmd_cls(int argc, const console_cmd_args *argv);
#endif

STATIC_COMMAND_START
STATIC_COMMAND("gdb", "graphics console debug", &cmd_gfx_dbg)
#ifdef GFX_DEBUG_HELPERS
STATIC_COMMAND("setx", "", &cmd_setx)
STATIC_COMMAND("sety", "", &cmd_sety)
STATIC_COMMAND("print", "", &cmd_print)
STATIC_COMMAND("cls", "", &cmd_cls)
#endif
STATIC_COMMAND_END(fasterconsole);

static struct {
    gfx_surface *surface;
    uint rows, columns;
    uint extray; // extra pixels left over if the rows doesn't fit precisely

    uint x, y; // in chars
    uint viewport_top;
    bool wrapping;

    uint32_t front_color;
    uint32_t back_color;

    framebuffer_pos pos;

    hvs_layer layer0;
    hvs_layer layer1;
} gfxconsole;

static const int channel = PRIMARY_HVS_CHANNEL;

static print_callback_t cb = {
    .entry = { 0 },
    .print = gfxconsole_print_callback,
    .context = NULL
};

void gfxconsole_print_callback(print_callback_t *callback, const char *str, size_t len) {
    for (size_t i = 0; i < len; i++) {
        gfxconsole_putc(str[i]);
    }
}

static void clear_line(uint line) {
  const uint real_y = (line + gfxconsole.viewport_top) % gfxconsole.rows;
  gfx_fillrect(gfxconsole.surface, 0, real_y * FONT_Y, gfxconsole.surface->width, FONT_Y, gfxconsole.back_color);
}

static void adjust_sprites(void) {
  gfxconsole.layer0.viewport_h = (gfxconsole.rows - gfxconsole.viewport_top) * FONT_Y;
  gfxconsole.layer0.h = gfxconsole.layer0.viewport_h;

  gfxconsole.layer0.viewport_y = gfxconsole.viewport_top * FONT_Y;

  gfxconsole.layer1.y = gfxconsole.layer0.x + gfxconsole.layer0.h + 6;
  gfxconsole.layer1.viewport_h = gfxconsole.viewport_top * FONT_Y;
  gfxconsole.layer1.h = gfxconsole.layer1.viewport_h;
  gfxconsole.layer1.visible = gfxconsole.layer1.h > 0;

  mutex_acquire(&channels[channel].lock);
  hvs_update_dlist(channel);
  mutex_release(&channels[channel].lock);
}

static int cmd_gfx_dbg(int argc, const console_cmd_args *argv) {
  printf("viewport top: %d, y: %d, rows: %d, cols: %d\n", gfxconsole.viewport_top, gfxconsole.y, gfxconsole.rows, gfxconsole.columns);
  return 0;
}
static int cmd_setx(int argc, const console_cmd_args *argv) {
  if (argc >= 2) gfxconsole.x = argv[1].u;
  return 0;
}
static int cmd_sety(int argc, const console_cmd_args *argv) {
  if (argc >= 2) gfxconsole.y = argv[1].u;
  return 0;
}
static int cmd_print(int argc, const console_cmd_args *argv) {
  for (int arg = 1; arg < argc; arg++) {
    if (arg != 1) gfxconsole_putc('_');
    gfxconsole_print_callback(&cb, argv[arg].str, strlen(argv[arg].str));
  }
  gfxconsole_putc('\n');
  return 0;
}
static int cmd_cls(int argc, const console_cmd_args *argv) {
  gfxconsole.x = 0;
  gfxconsole.y = 0;
  gfxconsole.viewport_top = 0;
  return 0;
}

static void gfxconsole_putc(char c) {
    static enum { NORMAL, ESCAPE } state = NORMAL;
    static uint32_t p_num = 0;

    uint oldy = gfxconsole.y;
    uint real_y = (gfxconsole.y + gfxconsole.viewport_top) % gfxconsole.rows;

    switch (state) {
        case NORMAL: {
            if (c == '\n' || c == '\r') {
                gfxconsole.x = 0;
                gfxconsole.y++;
            } else if (c == 0x1b) {
                p_num = 0;
                state = ESCAPE;
            } else {
                font_draw_char(gfxconsole.surface, c, gfxconsole.x * FONT_X, real_y * FONT_Y, gfxconsole.front_color);
                gfxconsole.x++;
            }
            break;
        }

        case ESCAPE: {
            if (c >= '0' && c <= '9') {
                p_num = (p_num * 10) + (c - '0');
            } else if (c == 'D') {
                if (p_num <= gfxconsole.x)
                    gfxconsole.x -= p_num;
                state = NORMAL;
            } else if (c == '[') {
                // eat this character
            } else {
                font_draw_char(gfxconsole.surface, c, gfxconsole.x * FONT_X, real_y * FONT_Y, gfxconsole.front_color);
                gfxconsole.x++;
                state = NORMAL;
            }
            break;
        }
    }

    // wrap to left when crossing over right
    if (gfxconsole.x >= gfxconsole.columns) {
        gfxconsole.x = 0;
        gfxconsole.y++;
    }

    if (gfxconsole.y >= gfxconsole.rows) {
        gfxconsole.y = gfxconsole.rows - 1;
        gfxconsole.viewport_top += 1;
        gfxconsole.viewport_top = gfxconsole.viewport_top % gfxconsole.rows;
        clear_line(gfxconsole.y);
    }

    if (gfxconsole.y != oldy) {
        //clear_line(gfxconsole.y);
    }

    adjust_sprites();
}

void gfxconsole_start(void) {
    DEBUG_ASSERT(gfxconsole.surface == NULL);

    // set up the surface

    // calculate how many rows/columns we have
    gfxconsole.rows = gfxconsole.pos.height / FONT_Y;
    gfxconsole.columns = gfxconsole.pos.width / FONT_X;
    gfxconsole.extray = gfxconsole.pos.height - (gfxconsole.rows * FONT_Y);

    const gfx_format fmt = GFX_FORMAT_ARGB_8888;
    gfxconsole.surface = gfx_create_surface(NULL, gfxconsole.columns * FONT_X, gfxconsole.rows * FONT_Y, gfxconsole.columns * FONT_X, fmt);

    //bzero(gfxconsole.surface->ptr, gfxconsole.surface->len);
    gfx_fillrect(gfxconsole.surface, 0, 0, gfxconsole.columns * FONT_X, gfxconsole.rows * FONT_Y, 0xff0000AA);

    dprintf(SPEW, "gfxconsole: rows %d, columns %d, extray %d\n", gfxconsole.rows, gfxconsole.columns, gfxconsole.extray);

    // start in the upper left
    gfxconsole.x = 0;
    gfxconsole.y = 0;
    gfxconsole.viewport_top = 0;
    gfxconsole.wrapping = false;

    gfxconsole.front_color = 0xffffffff;
    gfxconsole.back_color = 0xff0000AA;

    clear_line(gfxconsole.y);

    mk_unity_layer(&gfxconsole.layer0, gfxconsole.surface, 50, gfxconsole.pos.x, gfxconsole.pos.y);
    mk_unity_layer(&gfxconsole.layer1, gfxconsole.surface, 50, gfxconsole.pos.x, gfxconsole.pos.y);
    gfxconsole.layer1.visible = false;
    gfxconsole.layer0.name = strdup("top");
    gfxconsole.layer1.name = strdup("bottom");

    mutex_acquire(&channels[channel].lock);
    hvs_dlist_add(channel, &gfxconsole.layer0);
    hvs_dlist_add(channel, &gfxconsole.layer1);
    hvs_update_dlist(channel);
    mutex_release(&channels[channel].lock);
    hvs_set_background_color(channel, 0x0088FF);

    // register for debug callbacks
    register_print_callback(&cb);
    //const char * testmsg = "zero\none\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine\nten\n";
    //gfxconsole_print_callback(&cb, testmsg, strlen(testmsg));
}

void gfxconsole_start_on_display(void) {
    static bool started = false;

    if (started)
        return;

    hvs_get_framebuffer_pos(PRIMARY_HVS_CHANNEL, &gfxconsole.pos);

    gfxconsole_start();
    started = true;
}

static void gfxconsole_init_hook(uint level) {
    gfxconsole_start_on_display();
}

LK_INIT_HOOK(gfxconsole, &gfxconsole_init_hook, LK_INIT_LEVEL_PLATFORM);
