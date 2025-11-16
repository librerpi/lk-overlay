#include <arch.h>
#include <assert.h>
#include <lib/elf.h>
#include <lwip/apps/http_client.h>
#include <lwip/apps/tftp_client.h>
#include <lwip/dhcp.h>
#include <lwip/netif.h>
#include <lz4.h>
#include <net-utils.h>
#include <platform/time.h>

#ifdef WITH_LIB_CKSUM_HELPER
#include <cksum-helper/cksum-helper.h>
#endif

#include "stage1.h"

static struct netif *last_netif = NULL;

NETIF_DECLARE_EXT_CALLBACK(stage1_nic_ctx);

static void stage1_nic_status(struct netif* netif, netif_nsc_reason_t reason, const netif_ext_callback_args_t *args) {
  // TODO, if left idle after a failure, a dhcp renew will trigger a second attempt and fail
  //puts("NIC status cb");
  if (netif_is_up(netif)) {
    const struct dhcp *d = netif_dhcp_data(netif);
    if (d) {
      last_netif = netif;
      //printf("filename: %s\n", d->boot_file_name);
      //printf("dhcp server: %s\n", ipaddr_ntoa(&d->server_ip_addr));
      //printf("my ip: %s\n", ipaddr_ntoa(&netif->ip_addr));
      //printf("next server: %s\n", ipaddr_ntoa(&d->offered_si_addr));
      add_boot_target("network");
    }
  }
}

void netboot_init() {
  netif_add_ext_callback(&stage1_nic_ctx, stage1_nic_status);
}

#ifdef HTTP
err_t http_recv_fun(void *arg, struct altcp_pcb *conn, struct pbuf *p, err_t err) {
}
#endif

typedef enum { mode_tftp
#ifdef HTTP
, mode_http
#endif
} bootmode;
typedef enum { comp_none, comp_lz4 } bootcomp;

void try_to_netboot(void) {
  ssize_t ret;
  const int buffer_size = 1024*1024*2;
  uint8_t *buffer = malloc(buffer_size);
  const bootmode mode = mode_tftp;
  const bootcomp comp = comp_lz4;

  ip_addr_t hostip;
  if (netif_is_up(last_netif)) {
    const struct dhcp *d = netif_dhcp_data(last_netif);
    if (d) {
      hostip = d->offered_si_addr;
    } else return;
  } else return;

  ssize_t size_used;

  uint64_t start = current_time_hires();
  switch (mode) {
    case mode_tftp:
    {
      if (comp == comp_lz4) {
        size_used = tftp_blocking_get(hostip, "rpi/lk.elf.lz4", buffer_size, buffer);

#ifdef WITH_LIB_CKSUM_HELPER
        uint8_t hash[sha256_implementation.hash_size];
        hash_blob(&sha256_implementation, buffer, size_used, hash);
        print_hash(hash, sha256_implementation.hash_size);
#endif

        uint8_t *buffer2 = malloc(buffer_size);
        ret = LZ4_decompress_safe((const char*)(buffer+8), (char *)buffer2, (size_used-8), buffer_size);
        free(buffer);
        buffer = buffer2;
        size_used = ret;
      } else {
        size_used = tftp_blocking_get(hostip, "rpi/lk.elf", buffer_size, buffer);
      }
      break;
    }
#ifdef HTTP
    case mode_http:
    {
      httpc_connection_t settings = {
        .use_proxy = false,
        .result_fn = NULL,
        .headers_done_fn = NULL
      };
      httpc_state_t *connection;
      err_t ret2 = httpc_get_file_dns("router.localnet", 80, "/rpi/lk.elf", &settings, http_recv_fun, callback_arg, &connection);
      break;
    }
#endif
  }
  uint64_t stop = current_time_hires();

  if (true) {
    double delta = (double)(stop - start) / 1000 / 1000;
    printf("%d bytes received in %f Sec\n", (uint32_t)size_used, delta);
    printf("%f kbytes/sec\n", (double)size_used / delta / 1024);
  }

  assert(size_used > 0);

  elf_handle_t *stage2_elf = malloc(sizeof(elf_handle_t));
  ret = elf_open_handle_memory(stage2_elf, buffer, size_used);
  if (ret) {
    printf("failed to elf open: %ld\n", ret);
    return;
  }
  void *entry = load_and_run_elf(stage2_elf);
  free(buffer);
  if (false) {
    arch_chain_load(entry, 0, 0, 0, 0);
  }
  return;
}
