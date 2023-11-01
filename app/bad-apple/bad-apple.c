#include <app.h>
#include <dev/audio.h>
#include <kernel/mutex.h>
#include <kernel/thread.h>
#include <lib/bcache.h>
#include <lib/fs.h>
#include <lib/tga.h>
#include <lk/console_cmd.h>
#include <lk/debug.h>
#include <lk/err.h>
#include <lk/reg.h>
#include <math.h>
#include <platform/bcm28xx/clock.h>
#include <platform/bcm28xx/dpi.h>
#include <platform/bcm28xx/hvs.h>
#include <platform/bcm28xx/print_timestamp.h>
#include <stdlib.h>
#include <string.h>
#include <usbhooks.h>
#include <cksum-helper/cksum-helper.h>

/*
 * yt-dlp 'https://www.youtube.com/watch?v=FtutLA63Cp8'
 * ffmpeg -i 【東方】Bad\ Apple\!\!\ ＰＶ【影絵】\ \[FtutLA63Cp8\].webm 'frame%04d.png'
 * ./generate-bin
 * ffmpeg -i 【東方】Bad\ Apple\!\!\ ＰＶ【影絵】\ \[FtutLA63Cp8\].webm bad-apple.wav
 */

#define logf(fmt, ...) { print_timestamp(); printf("[badapple:%s:%d]: "fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__); }

extern bcache_t *last_ext2_cache;
static const int channel = PRIMARY_HVS_CHANNEL;

extern volatile uint32_t* dlist_memory;

#if 0
static int cmd_tone(int argc, const console_cmd_args *argv) {
  int16_t *buffer = malloc(2*1024);
  const float increment = (float)1/800;
  float j = 0;
  for (int repeats = 0; repeats < 20; repeats++) {
    for (int i=0; i<512; i++) {
      buffer[i++] = sin(j) * 1024;
      buffer[i] = sin(j) * 1024;
      j += increment;
    }
    audio_push_stereo(buffer, 512);
  }
  free(buffer);
  return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("tone", "play a tone", cmd_tone)
STATIC_COMMAND_END(badapple);
#endif

static void bad_apple_entry(const struct app_descriptor *app, void *args) {
  bcache_dump(last_ext2_cache, "ext4 read cache stats");
  dump_threads_stats();
}

static status_t mount_drive(const char *name) {
  printf("mounting %s to /bad-apple\n", name);
  status_t ret = fs_mount("/bad-apple", "ext2", name);
  if (ret) {
    printf("mount failure: %d\n", ret);
    return ret;
  }
  return NO_ERROR;
}

static status_t open_file(const char *path, filehandle **handle, uint64_t *size) {
  struct file_stat stat;
  status_t ret;

  ret = fs_open_file(path, handle);
  if (ret) {
    printf("fopen failure: %d\n", ret);
    return ret;
  }
  ret = fs_stat_file(*handle, &stat);
  if (ret) {
    printf("stat failure: %d\n", ret);
    return ret;
  }
  *size = stat.size;

  return NO_ERROR;
}

static void create_palette(int width, int height, int stride, uint8_t *image, hvs_layer *layer) {
  int32_t screen_width, screen_height;
  screen_width = (hvs_channels[channel].dispctrl >> 12) & 0xfff;
  screen_height = (hvs_channels[channel].dispctrl & 0xfff);

  const int screen_h_center = screen_width/2;
  const int screen_v_center = screen_height/2;

  const int x = screen_h_center - (width/2);
  const int y = screen_v_center - (height/2);

  gfx_surface *dummy = gfx_create_surface(NULL, 10, 10, 10, GFX_FORMAT_ARGB_8888);
  mk_unity_layer(layer, dummy, 60, width, height);
  const int dlist_length = 8;
  hvs_allocate_premade(layer, dlist_length);
  uint32_t *d = layer->premade_dlist;
  d[0] = CONTROL_VALID | CONTROL_PIXEL_ORDER(HVS_PIXEL_ORDER_ABGR) | CONTROL_UNITY | CONTROL_FORMAT(HVS_PIXEL_FORMAT_PALETTE) | CONTROL_WORDS(dlist_length);
  d[1] = POS0_X(x) | POS0_Y(y) | POS0_ALPHA(0xff);
  d[2] = POS2_H(height) | POS2_W(width) | (alpha_mode_pipeline << 30);
  d[3] = 0xDEADBEEF; // POS3 context
  d[4] = (uint32_t)image;
  d[5] = 0xDEADBEEF; // context 0 context
  d[6] = stride;
  d[7] = (0 << 30) | BIT(26) | (0xf00 << 2);
  layer->name = strdup("video");

  // AARRGGBB
  dlist_memory[0xf00] = 0x00000000;
  dlist_memory[0xf01] = 0xffffffff;
}

static void make_palette_visible(hvs_layer *layer) {
  mutex_acquire(&channels[channel].lock);
  hvs_dlist_add(channel, layer);
  hvs_update_dlist(channel);
  mutex_release(&channels[channel].lock);
}

static void play_video_once(uint64_t total_bytes, filehandle *video) {
  status_t ret;
  uint8_t *buffera, *bufferb;
  bool frameANext = true;
  const int width = 480;
  const int height = 360;
  const int stride = width/8;
  const int frame_bytes = stride * height;
  hvs_layer bad_apple_layer;

  buffera = malloc(frame_bytes);
  bufferb = malloc(frame_bytes);

  create_palette(width, height, stride, buffera, &bad_apple_layer);
  uint32_t *d = bad_apple_layer.premade_dlist;

  const int frames = total_bytes / frame_bytes;
  logf("%d frames found\n", frames);
  make_palette_visible(&bad_apple_layer);

  uint8_t *nextFrame = buffera;

  uint32_t start = *REG32(ST_CLO);
  for (int i=0; i<frames; i++) {
    //printf("frame %d\n", i);
    ret = fs_read_file(video, nextFrame, i * frame_bytes, frame_bytes);
    if (ret != frame_bytes) printf("read error %d\n", ret);

    // update dlist, so the newly loaded frame is visible
    d[4] = (uint32_t)nextFrame;

    // schedule dlist update on next vsync
    mutex_acquire(&channels[channel].lock);
    hvs_update_dlist(channel);
    mutex_release(&channels[channel].lock);

    frameANext = !frameANext;

    nextFrame = frameANext ? buffera : bufferb;

    //printf("frame %d\n", i+1);
    hvs_wait_vsync(channel);
    hvs_wait_vsync(channel);
  }
  uint32_t stop = *REG32(ST_CLO);
  uint32_t spent = stop - start;
  free(buffera);
  list_delete(&bad_apple_layer.node);
  logf("read and displayed all frames in %d uSec\n", spent);
  logf("uSec per frame: %d\n", spent / frames);
  logf("video EOF\n");
}

typedef struct {
  char tag[4]; // must be RIFF
  uint32_t filesize;
  char type[4]; // must be WAVE

  char format_chunk[4]; // must be "fmt "
  uint32_t format_length;
  uint16_t format_type; // 1==pcm
  uint16_t format_channels;
  uint32_t format_rate;
  uint32_t format_bytes_per_sec;
  uint16_t format_bytes_per_sample; // bytes for a single sample, on all channels
  uint16_t format_bits; // bits per sample

  char data_chunk[4]; // must be "data"
  uint32_t data_size;
} wav_header_t;

static status_t load_and_play_chunk(filehandle *wav, void *buffer, int offset, int bytes, bool stereo) {
  uint32_t start = *REG32(ST_CLO);
  int ret = fs_read_file(wav, buffer, offset, bytes);
  uint32_t mid = *REG32(ST_CLO);
  int16_t *samples = (int16_t*)buffer;
  if (stereo) {
    audio_push_stereo(samples, bytes/4);
  } else {
    audio_push_mono_16bit(samples, bytes/2);
  }
  uint32_t end = *REG32(ST_CLO);
  uint32_t read_time = mid - start;
  uint32_t play_time = end - mid;
  if (read_time > 2000) printf("read time: %d uSec\n", read_time);
  if (play_time > 42000) printf("play time: %d uSec\n", play_time);
  return ret;
}

void print_counts(void);

static int bad_apple_audio(void *_fh) {
  filehandle *wav = _fh;
  assert(sizeof(wav_header_t) == 44);
  printf("sizeof(wav_header_t) == %d\n", sizeof(wav_header_t));
  wav_header_t *header;
  uint8_t *buffer = malloc(8192);
  header = (wav_header_t*)buffer;
  int data_offset = 0;
  int data_size = 0;
  struct file_stat stat;
  fs_stat_file(wav, &stat);

  if (fs_read_file(wav, buffer, 0, 2048) != 2048) {
    logf("error reading wav header\n");
    return -1;
  }

  void *ptr = buffer + 0xc;
  while (true) {
    char tag[5];
    uint32_t chunk_size;
    memcpy(tag, ptr, 4);
    tag[4] = 0;
    memcpy(&chunk_size, ptr + 4, 4);
    printf("%d tag: '%s', size: %d\n", ((uint8_t*)ptr) - buffer, tag, chunk_size);

    if (chunk_size == 0) break;
    if (strncmp(tag, "data", 4) == 0) {
      data_offset = (((uint8_t*)ptr) - buffer) + 8;
      data_size = chunk_size;
      break;
    }

    ptr = ptr + 8 + chunk_size;
  }

  assert(strncmp(header->tag, "RIFF", 4) == 0);
  printf("size: %d\n", header->filesize);
  assert(strncmp(header->type, "WAVE", 4) == 0);
  assert(strncmp(header->format_chunk, "fmt ", 4) == 0);
  printf("fmt length: %d\n", header->format_length);
  printf("fmt type: %d\n", header->format_type);
  printf("channels: %d\n", header->format_channels);
  printf("rate: %d\n", header->format_rate);
  printf("bytes/sec: %d\n", header->format_bytes_per_sec);
  printf("bytes per %d samples: %d\n", header->format_channels, header->format_bytes_per_sample);
  printf("bits per sample: %d\n", header->format_bits);

  printf("data begins at offset 0x%x\n", data_offset);

  printf("%d samples in total\n", data_size/4);

  //assert(header->format_type == 1);

  uint32_t samplerate = header->format_rate;
  uint16_t audio_channels = header->format_channels;
  uint16_t bits = header->format_bits;
  assert(bits == 16);

  const int chunk_size = 8192;
  printf("playing %d bytes per chunk, %d uSec each\n", chunk_size, ((chunk_size/header->format_bytes_per_sample) * 1000000) / samplerate);

  //bool mono = audio_channels == 1;
  bool stereo = audio_channels == 2;
  audio_start(samplerate, stereo);

  uint32_t start = *REG32(ST_CLO);
  load_and_play_chunk(wav, buffer, data_offset, 4096-data_offset, stereo);
  for (unsigned int i = 4096; i < stat.size; i += chunk_size) {
    //logf("loading chunk %d\n", i/8192);
    int ret = load_and_play_chunk(wav, buffer, i, chunk_size, stereo);
    if (ret != chunk_size) {
      printf("short read %d\n", ret);
      break;
    }
  }
  uint32_t stop = *REG32(ST_CLO);
  uint32_t delta = stop - start;
  logf("audio EOF\n");
  printf("played audio for %d uSec\n", delta);
  const uint64_t a = samplerate;
  const uint64_t b = a * delta;
  const uint64_t c = b / 1000000;
  printf("%d estimated samples played\n", (uint32_t)c);
  //print_counts();
  dump_threads_stats();

  return 0;
}

static int bad_apple_audio_raw(void *unused) {
  filehandle *fh;
  uint ret = fs_open_file("/bad-apple/bad-apple-u8.raw", &fh);
  assert(ret == 0);
  struct file_stat stat;
  ret = fs_stat_file(fh, &stat);
  assert(ret == 0);
  void *buffer = malloc(stat.size);
  assert(buffer);

  ret = fs_read_file(fh, buffer, 0, stat.size);
  assert(ret == stat.size);

#if 0
  uint8_t hash[sha256_implementation.hash_size];
  hash_blob(&sha256_implementation, buffer, stat.size, hash);
  print_hash(hash, sha256_implementation.hash_size);
#endif
  fs_close_file(fh);

  audio_start(48214, false);
  audio_push_mono_8bit(buffer, stat.size);

  logf("audio EOF\n");
  dump_threads_stats();
  return 0;
}

static gfx_surface *double_width(const gfx_surface *in) {
  gfx_surface *out = gfx_create_surface(NULL, in->width * 2, in->height, in->stride * 2, GFX_FORMAT_ARGB_8888);
  uint32_t *data_in = in->ptr;
  uint32_t *data_out = out->ptr;
  for (uint i=0; i < (in->stride * in->pixelsize * in->height); i++) {
    data_out[i*2] = data_in[i];
    data_out[(i*2)+1] = data_in[i];
  }
  return out;
}

static void teletext_test(void) {
  filehandle *fh;
  status_t ret;
  struct file_stat stat;

  ret = fs_open_file("/bad-apple/hello-world-teletext.tga", &fh);
  logf("%d\n", ret);
  ret = fs_stat_file(fh, &stat);
  logf("%d\n", ret);
  void *buffer = malloc(stat.size);
  fs_read_file(fh, buffer, 0, stat.size);

  uint32_t start = *REG32(ST_CLO);
  gfx_surface *gfx_orig = tga_decode(buffer, stat.size, GFX_FORMAT_ARGB_8888);
  gfx_surface *gfx = double_width(gfx_orig);
  uint32_t end = *REG32(ST_CLO);
  logf("%p\n", gfx);
  logf("decoding took %d uSec\n", end - start);
  free(buffer);

  hvs_layer layer;
  mk_unity_layer(&layer, gfx, 60, 360, 26);
  hvs_allocate_premade(&layer, 17);
  layer.x = 0;
  layer.y = 0;
  layer.w = gfx->width;
  layer.h = gfx->height;
  layer.name = strdup("teletext");
  layer.alpha_mode = alpha_mode_fixed;
  hvs_regen_noscale_noviewport(&layer);

  mutex_acquire(&channels[channel].lock);
  hvs_dlist_add(channel, &layer);
  hvs_update_dlist(channel);
  mutex_release(&channels[channel].lock);
}

static int bad_apple_main(void *_dev) {
  const char *device = _dev;
  filehandle *video = NULL;
  filehandle *audio = NULL;
  uint64_t total_bytes, audio_bytes;
  logf("starting\n");
  dump_threads_stats();

  if (mount_drive(device)) return 0;

  //teletext_test();
  //goto unmount;

  if (open_file("/bad-apple/bad-apple.bin", &video, &total_bytes)) goto unmount;
  if (open_file("/bad-apple/bad-apple.wav", &audio, &audio_bytes)) goto unmount;

  logf("audio file %lld bytes\n", audio_bytes);

  thread_t *thread = thread_create("bad apple audio", bad_apple_audio, audio, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE);
  thread_resume(thread);

  play_video_once(total_bytes, video);

  int retcode;
  logf("waiting for audio thread to end\n");
  thread_join(thread, &retcode, INFINITE_TIME);
  logf("audio thread ended\n");


  logf("exiting\n");
  dump_threads_stats();
  unmount:
    if (video) {
      fs_close_file(video);
      video = NULL;
    }
    if (audio) {
      fs_close_file(audio);
      audio = NULL;
    }
    fs_unmount("/bad-apple");
  return 0;
}

static void bad_apple_usb_init(void) {
  puts("bad apple detected usb init");
}

static void bad_apple_msd_probed(const char *name) {
  printf("drive %s connected\n", name);
  char *buffer = malloc(64);
  snprintf(buffer, 64, "%sp1", name);

  thread_t *thread = thread_create("bad apple video", bad_apple_main, buffer, DEFAULT_PRIORITY, DEFAULT_STACK_SIZE);
  thread_detach_and_resume(thread);
}

USB_HOOK_START(bad_apple)
  .init = bad_apple_usb_init,
  .msd_probed = bad_apple_msd_probed,
USB_HOOK_END
