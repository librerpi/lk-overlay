#include <stdint.h>
#include <stdio.h>
#include "tusb.h"

static tusb_desc_device_t desc_device;

static void print_device_descriptor(tuh_xfer_t* xfer);
static void print_utf16(uint16_t *temp_buf, size_t buf_len);

// English
#define LANGUAGE_ID 0x0409

// Invoked when device is mounted (configured)
void tuh_mount_cb (uint8_t daddr)
{
  printf("Device attached, address = %d\r\n", daddr);

  // Get Device Descriptor
  // TODO: invoking control transfer now has issue with mounting hub with multiple devices attached, fix later
  tuh_descriptor_get_device(daddr, &desc_device, 18, print_device_descriptor, 0);
}

void print_device_descriptor(tuh_xfer_t* xfer)
{
  if ( XFER_RESULT_SUCCESS != xfer->result )
  {
    printf("Failed to get device descriptor\r\n");
    return;
  }

  uint8_t const daddr = xfer->daddr;

  printf("Device %u: ID %04x:%04x\r\n", daddr, desc_device.idVendor, desc_device.idProduct);
  printf("Device Descriptor:\r\n");
  printf("  bLength             %u\r\n"     , desc_device.bLength);
  printf("  bDescriptorType     %u\r\n"     , desc_device.bDescriptorType);
  printf("  bcdUSB              %04x\r\n"   , desc_device.bcdUSB);
  printf("  bDeviceClass        %u\r\n"     , desc_device.bDeviceClass);
  printf("  bDeviceSubClass     %u\r\n"     , desc_device.bDeviceSubClass);
  printf("  bDeviceProtocol     %u\r\n"     , desc_device.bDeviceProtocol);
  printf("  bMaxPacketSize0     %u\r\n"     , desc_device.bMaxPacketSize0);
  printf("  idVendor            0x%04x\r\n" , desc_device.idVendor);
  printf("  idProduct           0x%04x\r\n" , desc_device.idProduct);
  printf("  bcdDevice           %04x\r\n"   , desc_device.bcdDevice);

  // Get String descriptor using Sync API
  uint16_t temp_buf[128];

  printf("  iManufacturer       %u     "     , desc_device.iManufacturer);
  if (XFER_RESULT_SUCCESS == tuh_descriptor_get_manufacturer_string_sync(daddr, LANGUAGE_ID, temp_buf, sizeof(temp_buf)) )
  {
    print_utf16(temp_buf, TU_ARRAY_SIZE(temp_buf));
  }
  printf("\r\n");

  printf("  iProduct            %u     "     , desc_device.iProduct);
  if (XFER_RESULT_SUCCESS == tuh_descriptor_get_product_string_sync(daddr, LANGUAGE_ID, temp_buf, sizeof(temp_buf)))
  {
    print_utf16(temp_buf, TU_ARRAY_SIZE(temp_buf));
  }
  printf("\r\n");

  printf("  iSerialNumber       %u     "     , desc_device.iSerialNumber);
  if (XFER_RESULT_SUCCESS == tuh_descriptor_get_serial_string_sync(daddr, LANGUAGE_ID, temp_buf, sizeof(temp_buf)))
  {
    print_utf16(temp_buf, TU_ARRAY_SIZE(temp_buf));
  }
  printf("\r\n");

  printf("  bNumConfigurations  %u\r\n"     , desc_device.bNumConfigurations);

  // Get configuration descriptor with sync API
  if (XFER_RESULT_SUCCESS == tuh_descriptor_get_configuration_sync(daddr, 0, temp_buf, sizeof(temp_buf)))
  {
    //parse_config_descriptor(daddr, (tusb_desc_configuration_t*) temp_buf);
  }
}

static int _count_utf8_bytes(const uint16_t *buf, size_t len) {
    size_t total_bytes = 0;
    for (size_t i = 0; i < len; i++) {
        uint16_t chr = buf[i];
        if (chr < 0x80) {
            total_bytes += 1;
        } else if (chr < 0x800) {
            total_bytes += 2;
        } else {
            total_bytes += 3;
        }
        // TODO: Handle UTF-16 code points that take two entries.
    }
    return (int) total_bytes;
}

static void _convert_utf16le_to_utf8(const uint16_t *utf16, size_t utf16_len, uint8_t *utf8, size_t utf8_len) {
    // TODO: Check for runover.
    (void)utf8_len;
    // Get the UTF-16 length out of the data itself.

    for (size_t i = 0; i < utf16_len; i++) {
        uint16_t chr = utf16[i];
        if (chr < 0x80) {
            *utf8++ = chr & 0xffu;
        } else if (chr < 0x800) {
            *utf8++ = (uint8_t)(0xC0 | (chr >> 6 & 0x1F));
            *utf8++ = (uint8_t)(0x80 | (chr >> 0 & 0x3F));
        } else {
            // TODO: Verify surrogate.
            *utf8++ = (uint8_t)(0xE0 | (chr >> 12 & 0x0F));
            *utf8++ = (uint8_t)(0x80 | (chr >> 6 & 0x3F));
            *utf8++ = (uint8_t)(0x80 | (chr >> 0 & 0x3F));
        }
        // TODO: Handle UTF-16 code points that take two entries.
    }
}

static void print_utf16(uint16_t *temp_buf, size_t buf_len) {
    size_t utf16_len = ((temp_buf[0] & 0xff) - 2) / sizeof(uint16_t);
    size_t utf8_len = (size_t) _count_utf8_bytes(temp_buf + 1, utf16_len);
    _convert_utf16le_to_utf8(temp_buf + 1, utf16_len, (uint8_t *) temp_buf, sizeof(uint16_t) * buf_len);
    ((uint8_t*) temp_buf)[utf8_len] = '\0';

    printf((char*)temp_buf);
}
