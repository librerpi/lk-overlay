#include <png.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// gcc app/bad-apple/generate-bin.c -o app/bad-apple/generate-bin -lpng
// input is a pile of .png files
// output is a raw binary video, just a stream of 1bit pixels

int main(int argc, char **argv) {
  FILE *fh = fopen("bad-apple.bin", "wb");
  char buffer[32];
  for (int i=0; i<6572; i++) {
    snprintf(buffer, 32, "frame%04d.png", i+1);
    printf("file %s\n", buffer);

    png_image image;
    memset(&image, 0, sizeof(image));
    image.version = PNG_IMAGE_VERSION;

    if (png_image_begin_read_from_file(&image, buffer) != 0) {
      png_bytep buffer;

      image.format = 0;

      buffer = malloc(PNG_IMAGE_SIZE(image));
      uint8_t *out_buffer = malloc(image.width * image.height / 8);
      bzero(out_buffer, image.width * image.height / 8);
      const int out_stride = image.width/8;

      if (png_image_finish_read(&image, NULL, buffer, 0, NULL) != 0) {
        for (int row=0; row<image.height; row++) {
          //printf("in ");
          for (int col=0; col<image.width; col++) {
            const uint8_t pixel = buffer[(row*image.width)+col];
            const int bit = col % 8;
            const int byte = col / 8;
            //printf("%02x ", pixel);
            if (pixel > 128) {
              out_buffer[(row*out_stride) + byte] |= 1 << bit;
            }
          }
          //printf("\nout ");
          for (int i=0; i<out_stride; i++) {
            //printf("%02x ", out_buffer[(row*out_stride) + i]);
          }
          //puts("");
        }
        fwrite(out_buffer, out_stride * image.height, 1, fh);
        png_image_free(&image);
        free(buffer);
        free(out_buffer);
      }
    }
  }
  fclose(fh);
  return 0;
}
