#include <app.h>
#include <lib/fs.h>
#include <lib/tga.h>
#include <lk/console_cmd.h>
#include <lk/err.h>
#include <lk/list.h>
#include <lk/reg.h>
#include <platform/bcm28xx/clock.h>
#include <platform/bcm28xx/hvs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * mounts partition 2 from the SD card (ext2 or ext4 supported)
 * loads every file in `/images/*.tga` that is under 5mb
 * displays the first image as soon as its loaded
 * cycles between all loaded images, showing each for 10 seconds
 * the raw image data must fit within the LENGTH in start.ld
 * changing the ORIGIN in start.ld to 0 makes image data load more quickly
 */

struct list_node image_names;

typedef struct {
  struct list_node node;
  char name[FS_MAX_FILE_LEN];
  uint32_t size;
} image_name;

hvs_layer *layer;

typedef struct {
  struct list_node node;
  gfx_surface *image;
  char name[FS_MAX_FILE_LEN];
} loaded_image;

struct list_node loaded_images;

thread_t *loading_thread;
wait_queue_t queue;

static int cmd_show_images(int argc, const console_cmd_args *argv) {
  image_name *entry;
  list_for_every_entry(&image_names, entry, image_name, node) {
    printf("name: %s, %d KB\n", entry->name, entry->size/1024);
  }
  return 0;
}

static void load_image(const char *name) {
  uint32_t t1 = *REG32(ST_CLO);
  char *fullpath = malloc(FS_MAX_FILE_LEN);
  filehandle *fh;
  gfx_surface *gfx;

  // concat the paths
  strcpy(fullpath, "/root/images/");
  strlcat(fullpath, name, FS_MAX_PATH_LEN);
  printf("name is %s\n", fullpath);

  // open the file
  int ret = fs_open_file(fullpath, &fh);
  if (ret) {
    printf("open of %s failed with err %d\n", fullpath, ret);
    free(fullpath);
    return;
  }
  free(fullpath);

  // get the size
  struct file_stat stat;
  ret = fs_stat_file(fh, &stat);
  if (ret) {
    printf("stat failed with %d\n", ret);
    return;
  }

  // read the whole file into memory and close the handle
  uint8_t *rawimage = malloc(stat.size);
  fs_read_file(fh, rawimage, 0, stat.size);
  fs_close_file(fh);
  puts("decoding");
  uint32_t t2 = *REG32(ST_CLO);

  // decode the image, then free the raw data
  gfx = tga_decode(rawimage, stat.size, GFX_FORMAT_ARGB_8888);
  free(rawimage);
  uint32_t t3 = *REG32(ST_CLO);

  printf("opened %s of %d x %d, reading took %d uSec, decoding took %d uSec\n", name, gfx->width, gfx->height, t2-t1, t3-t2);

  loaded_image *i = malloc(sizeof(*i));
  i->image = gfx;
  strcpy(i->name, name);

  list_add_tail(&loaded_images, &i->node);
}

static int cmd_load_image(int argc, const console_cmd_args *argv) {
  if (!layer) {
  }

  return 0;
}

STATIC_COMMAND_START
STATIC_COMMAND("slideshow_list", "list all images", &cmd_show_images)
STATIC_COMMAND("slideshow_load", "load the first image", &cmd_load_image)
STATIC_COMMAND_END(slideshow);

static int slideshow_loader(void *x) {
  while (!list_is_empty(&image_names)) {
    image_name *n = list_remove_head_type(&image_names, image_name, node);
    load_image(n->name);
    free(n);
    THREAD_LOCK(state);
    wait_queue_wake_all(&queue, true, NO_ERROR);
    THREAD_UNLOCK(state);
    printf("image done loading at %d\n", *REG32(ST_CLO));
  }
  printf("all images loaded at %d\n", *REG32(ST_CLO));
  return 0;
}

static void slideshow_entry(const struct app_descriptor *app, void *args) {
  status_t ret;
  dirhandle *handle;
  struct dirent ent;
  layer = NULL;
  struct file_stat stat;

  printf("slideshow starting at %d uSec since boot\n", *REG32(ST_CLO));

  list_initialize(&image_names);
  list_initialize(&loaded_images);
  wait_queue_init(&queue);

  ret = fs_open_dir("/root/images", &handle);
  if (ret) {
    printf("err1: %d\n", ret);
    return;
  }
  char *fullpath = malloc(FS_MAX_FILE_LEN);
  while ((ret = fs_read_dir(handle, &ent)) >= 0) {
    char name[FS_MAX_FILE_LEN];
    strncpy(name, ent.name, FS_MAX_FILE_LEN-1);
    strtok(name, ".");
    char * extension = strtok(NULL, ".");
    if (strncmp(extension, "tga", 3) == 0) { // its a tga file
      strcpy(fullpath, "/root/images/");
      strlcat(fullpath, ent.name, FS_MAX_PATH_LEN);

      filehandle *fh;
      ret = fs_open_file(fullpath, &fh);
      if (ret) {
        printf("open of %s failed with err %d\n", fullpath, ret);
        continue;
      }

      ret = fs_stat_file(fh, &stat);
      if (ret) {
        printf("stat failed with %d\n", ret);
        continue;
      }
      fs_close_file(fh);
      if (stat.size < (5*1024*1024)) {
        image_name *n = malloc(sizeof(*n));
        strncpy(n->name, ent.name, FS_MAX_FILE_LEN-1);
        n->size = stat.size;
        list_add_tail(&image_names, &n->node);
      } else {
        puts("file too big, skipping");
      }
    }
  }
  fs_close_dir(handle);
  free(fullpath);

  // begin loading all images into ram
  loading_thread = thread_create("slideshow loader", slideshow_loader, NULL, DEFAULT_PRIORITY, ARCH_DEFAULT_STACK_SIZE);
  thread_resume(loading_thread);

  // wait until the first image is loaded
  THREAD_LOCK(state);
  wait_queue_block(&queue, INFINITE_TIME);
  THREAD_UNLOCK(state);

  loaded_image *entry;
  layer = malloc(sizeof(*layer));
  int channel = 1;
  bool add_layer = true;
  uint32_t t1, t2;
  while (true) {
    list_for_every_entry(&loaded_images, entry, loaded_image, node) {
      // change the image in the hvs_hayer
      MK_UNITY_LAYER(layer, entry->image, 60, 50, 20);
      layer->name = entry->name;
      // render it at 1/3rd size
      layer->w /= 3;
      layer->h /= 3;

      mutex_acquire(&channels[channel].lock);
      if (add_layer) {
        // if it isnt on-screen yet, add it
        hvs_dlist_add(channel, layer);
        add_layer = false;
        printf("first image visible at %d\n", *REG32(ST_CLO));
      }
      // request a page-flip
      hvs_update_dlist(channel);
      //printf("flip %d %d\n", *REG32(ST_CLO), t2-t1);
      mutex_release(&channels[channel].lock);

      t1 = *REG32(ST_CLO);
      thread_sleep(10 * 1000);
      t2 = *REG32(ST_CLO);
    }
  }
}

APP_START(slideshow)
  .entry = slideshow_entry,
APP_END
