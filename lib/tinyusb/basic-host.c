#include <stdint.h>
#include <platform/bcm28xx/print_timestamp.h>
#include <stdio.h>
#include "tusb.h"
#include <usb_utils.h>
#include <stdlib.h>


#if CFG_TUH_MSC
#include <lib/bio.h>
#include <lib/partition.h>
#include <lk/err.h>
#endif

static tusb_desc_device_t desc_device;

static void print_device_descriptor(tuh_xfer_t* xfer);
static void parse_config_descriptor(uint8_t dev_addr, tusb_desc_configuration_t const* desc_cfg);
static void print_utf16(uint16_t *temp_buf, size_t buf_len);
static uint16_t count_interface_total_len(tusb_desc_interface_t const* desc_itf, uint8_t itf_count, uint16_t max_len);

#define logf(fmt, ...) { print_timestamp(); printf("[DWC2:%s:%d]: "fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__); }

// English
#define LANGUAGE_ID 0x0409

// Invoked when device is mounted (configured)
void tuh_mount_cb (uint8_t daddr)
{
  logf("Device attached, address = %d\r\n", daddr);

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
  // TODO, tries to read 256 bytes, even if its only 32 bytes long
  if (XFER_RESULT_SUCCESS == tuh_descriptor_get_configuration_sync(daddr, 0, temp_buf, sizeof(temp_buf)))
  {
    parse_config_descriptor(daddr, (tusb_desc_configuration_t*) temp_buf);
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

static void parse_config_descriptor(uint8_t dev_addr, tusb_desc_configuration_t const* desc_cfg)
{
  uint8_t const* desc_end = ((uint8_t const*) desc_cfg) + tu_le16toh(desc_cfg->wTotalLength);
  uint8_t const* p_desc   = tu_desc_next(desc_cfg);

  printf("config descriptor\n");
  printf("  bLength: %d\n", desc_cfg->bLength);
  printf("  bDescriptorType: %d\n", desc_cfg->bDescriptorType);
  printf("  wTotalLength: %d\n", desc_cfg->wTotalLength);
  printf("  bNumInterfaces: %d\n", desc_cfg->bNumInterfaces);
  printf("  bConfigurationValue: %d\n", desc_cfg->bConfigurationValue);
  printf("  iConfiguration: %d\n", desc_cfg->iConfiguration);
  printf("  bmAttributes: 0x%x\n", desc_cfg->bmAttributes);
  printf("  bMaxPower: %d\n", desc_cfg->bMaxPower);

  // parse each interfaces
  while( p_desc < desc_end )
  {
    uint8_t assoc_itf_count = 1;

    // Class will always starts with Interface Association (if any) and then Interface descriptor
    if ( TUSB_DESC_INTERFACE_ASSOCIATION == tu_desc_type(p_desc) )
    {
      tusb_desc_interface_assoc_t const * desc_iad = (tusb_desc_interface_assoc_t const *) p_desc;
      assoc_itf_count = desc_iad->bInterfaceCount;

      p_desc = tu_desc_next(p_desc); // next to Interface
    }

    // must be interface from now
    if( TUSB_DESC_INTERFACE != tu_desc_type(p_desc) ) return;
    tusb_desc_interface_t const* desc_itf = (tusb_desc_interface_t const*) p_desc;

    uint16_t const drv_len = count_interface_total_len(desc_itf, assoc_itf_count, (uint16_t) (desc_end-p_desc));

    // probably corrupted descriptor
    if(drv_len < sizeof(tusb_desc_interface_t)) return;
    printf("  interface\n");
    printf("    bLength: %d\n", desc_itf->bLength);
    printf("    bDescriptorType: %d\n", desc_itf->bDescriptorType);
    printf("    bInterfaceNumber: %d\n", desc_itf->bInterfaceNumber);
    printf("    bAlternateSetting: %d\n", desc_itf->bAlternateSetting);
    printf("    bNumEndpoints: %d\n", desc_itf->bNumEndpoints);
    printf("    bInterfaceClass: %d\n", desc_itf->bInterfaceClass);
    printf("    bInterfaceSubClass: %d\n", desc_itf->bInterfaceSubClass);
    printf("    bInterfaceProtocol: %d\n", desc_itf->bInterfaceProtocol);
    printf("    iInterface: %d\n", desc_itf->iInterface);
    for (int i=0; i<desc_itf->bNumEndpoints; i++) {
      tusb_desc_endpoint_t *desc_ep = (tusb_desc_endpoint_t*)((uint8_t*)desc_itf + desc_itf->bLength + (sizeof(tusb_desc_endpoint_t) * i));
      printf("    endpoint %d\n", i);
      print_endpoint(desc_ep);
    }

    // only open and listen to HID endpoint IN
    if (desc_itf->bInterfaceClass == TUSB_CLASS_HID)
    {
      //open_hid_interface(dev_addr, desc_itf, drv_len);
    }

    // next Interface or IAD descriptor
    p_desc += drv_len;
  }
}


static uint16_t count_interface_total_len(tusb_desc_interface_t const* desc_itf, uint8_t itf_count, uint16_t max_len)
{
  uint8_t const* p_desc = (uint8_t const*) desc_itf;
  uint16_t len = 0;

  while (itf_count--)
  {
    // Next on interface desc
    len += tu_desc_len(desc_itf);
    p_desc = tu_desc_next(p_desc);

    while (len < max_len)
    {
      // return on IAD regardless of itf count
      if ( tu_desc_type(p_desc) == TUSB_DESC_INTERFACE_ASSOCIATION ) return len;

      if ( (tu_desc_type(p_desc) == TUSB_DESC_INTERFACE) &&
           ((tusb_desc_interface_t const*) p_desc)->bAlternateSetting == 0 )
      {
        break;
      }

      len += tu_desc_len(p_desc);
      p_desc = tu_desc_next(p_desc);
    }
  }

  return len;
}

#if CFG_TUH_MSC

typedef struct {
  bdev_t bdev;
  int dev_addr;
  int lun;
  size_t block_size;
} usb_dev_t;

#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})

typedef struct {
  event_t evt;
  bool success;
} callback_state_t;

static bool tuh_msc_read_block_cb(uint8_t dev_addr, tuh_msc_complete_data_t const* cb_data) {
  callback_state_t *cbs = (callback_state_t*)cb_data->user_arg;
  cbs->success = cb_data->csw->status == MSC_CSW_STATUS_PASSED;
  event_signal(&cbs->evt, true);
  return true;
}

static ssize_t tuh_msc_read_block(bdev_t *bdev, void *buf, bnum_t block, uint count) {
  usb_dev_t *dev = container_of(bdev, usb_dev_t, bdev);
  callback_state_t cbs = {
    .evt = EVENT_INITIAL_VALUE(cbs.evt, false, 0)
  };

  tuh_msc_read10(dev->dev_addr, dev->lun, buf, block, count, &tuh_msc_read_block_cb, (uint32_t)&cbs);
  event_wait(&cbs.evt);
  event_destroy(&cbs.evt);
  if (cbs.success) {
    return count * dev->block_size;
  } else {
    return ERR_GENERIC;
  }
}

static int part_prober(void *arg) {
  partition_publish(arg,0);
  free(arg);
  return 0;
}

void tuh_msc_mount_cb(uint8_t dev_addr) {
  printf("MSD found at %d\n", dev_addr);
  int total_luns = tuh_msc_get_maxlun(dev_addr);
  char buffer[20];
  for (int lun = 0; lun < total_luns; lun++) {
    printf("LUN %d\n", lun);
    snprintf(buffer, 20, "usb%d_%d", dev_addr, lun);
    usb_dev_t *dev = malloc(sizeof(usb_dev_t));
    dev->dev_addr = dev_addr;
    dev->lun = lun;
    dev->block_size = tuh_msc_get_block_size(dev_addr, lun);
    bnum_t block_count = tuh_msc_get_block_count(dev_addr, lun);
    bio_initialize_bdev(&dev->bdev, buffer, dev->block_size, block_count, 0, NULL, BIO_FLAGS_NONE);
    dev->bdev.read_block = tuh_msc_read_block;
    bio_register_device(&dev->bdev);

    thread_t * thread = thread_create(buffer, part_prober, strdup(buffer), DEFAULT_PRIORITY, DEFAULT_STACK_SIZE);
    thread_detach_and_resume(thread);
  }
}
void tuh_msc_umount_cb(uint8_t dev_addr) {
  printf("MSD at %d lost\n", dev_addr);
}
#endif
