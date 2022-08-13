// based on drivers/net/ethernet/broadcom/genet/bcmgenet.c from linux
// for PHY control, look at the cmd_bits variable in drivers/net/ethernet/broadcom/genet/bcmmii.c

#include <lk/reg.h>
#include <stdio.h>
#include <lk/console_cmd.h>
#include <platform/bcm28xx.h>
#include <lk/debug.h>
#include <platform/interrupts.h>
#include <lk/init.h>

static int cmd_genet_dump(int argc, const console_cmd_args *argv);

STATIC_COMMAND_START
STATIC_COMMAND("dump_genet", "print genet information", &cmd_genet_dump)
STATIC_COMMAND_END(genet);

#define SYS_REV_CTRL (GENET_BASE + 0x0)
#define UMAC_CMD     (GENET_BASE + 0x808)


/* Tx/Rx Dma Descriptor common bits*/
#define DMA_BUFLENGTH_MASK		0x0fff
#define DMA_BUFLENGTH_SHIFT		16
#define DMA_OWN				0x8000
#define DMA_EOP				0x4000
#define DMA_SOP				0x2000
#define DMA_WRAP			0x1000
/* Tx specific Dma descriptor bits */
#define DMA_TX_UNDERRUN			0x0200
#define DMA_TX_APPEND_CRC		0x0040
#define DMA_TX_OW_CRC			0x0020
#define DMA_TX_DO_CSUM			0x0010
#define DMA_TX_QTAG_SHIFT		7

/* Rx Specific Dma descriptor bits */
#define DMA_RX_CHK_V3PLUS		0x8000
#define DMA_RX_CHK_V12			0x1000
#define DMA_RX_BRDCAST			0x0040
#define DMA_RX_MULT			0x0020
#define DMA_RX_LG			0x0010
#define DMA_RX_NO			0x0008
#define DMA_RX_RXER			0x0004
#define DMA_RX_CRC_ERROR		0x0002
#define DMA_RX_OV			0x0001
#define DMA_RX_FI_MASK			0x001F
#define DMA_RX_FI_SHIFT			0x0007
#define DMA_DESC_ALLOC_MASK		0x00FF

#define UMAC_IRQ_RXDMA_MBDONE   (1 << 13)
#define UMAC_IRQ_RXDMA_PDONE    (1 << 14)
#define UMAC_IRQ_RXDMA_BDONE    (1 << 15)
#define UMAC_IRQ_RXDMA_DONE   UMAC_IRQ_RXDMA_MBDONE

#define GENET_VERSION_TARGET 5

#if GENET_VERSION_TARGET == 5
#define GENET_64bit
#define RDMA_OFFSET 0x2000
#define TDMA_OFFSET 0x4000
typedef struct {
  uint32_t tdma_read_ptr; // 00
  uint32_t tdma_read_ptr_hi; // 04
  uint32_t tdma_cons_index;  // 08
  uint32_t tdma_prod_index;  // 0c

  uint32_t dma_ring_buf_size; // 10
  uint32_t dma_start_addr;    // 14
  uint32_t dma_start_addr_hi; // 18
  uint32_t dma_end_addr;      // 1c

  uint32_t dma_end_addr_hi;   // 20
  uint32_t dma_mbuf_done_thresh; // 24
  uint32_t tdma_flow_period;     // 28
  uint32_t tdma_write_ptr;       // 2c

  uint32_t tdma_write_ptr_hi;    // 30
  uint32_t padding[3];           // 34/38/3c
} dma_ring_config;
#endif

/* total number of Buffer Descriptors, same for Rx/Tx */
#define TOTAL_DESC        256

typedef struct {
  uint32_t length_status;
  uint32_t addr_lo;
#ifdef GENET_64bit
  uint32_t addr_hi;
#endif
} genet_dma_descriptor;

#define DMA_DESC_SIZE (sizeof(genet_dma_descriptor))

/* DMA rings size */
#define DMA_RING_SIZE     (0x40)
#define DMA_RINGS_SIZE      (DMA_RING_SIZE * (DESC_INDEX + 1))


#define GENET_RDMA_REG_OFF  (RDMA_OFFSET + TOTAL_DESC * DMA_DESC_SIZE)
#define GENET_TDMA_REG_OFF  (TDMA_OFFSET + TOTAL_DESC * DMA_DESC_SIZE)

dma_ring_config *get_tdma_config(int ring) {
  dma_ring_config *config = (dma_ring_config*)(GENET_BASE + GENET_TDMA_REG_OFF + (DMA_RING_SIZE * ring));
  return config;
}

dma_ring_config *get_rdma_config(int ring) {
  dma_ring_config *config = (dma_ring_config*)(GENET_BASE + GENET_RDMA_REG_OFF + (DMA_RING_SIZE * ring));
  return config;
}

static void print_dma_config(int ring, const dma_ring_config *cfg, bool tx) {
  if (tx) printf("T");
  else printf("R");
  printf("%d: read ptr: %d", ring, cfg->tdma_read_ptr);
  printf(", write ptr: %d", cfg->tdma_write_ptr);
  printf(", dma ringbuf size: %d\n", cfg->dma_ring_buf_size);
  printf("dma start/end 0x%x/0x%x\n", cfg->dma_start_addr, cfg->dma_end_addr);
}

static void print_descriptor(int index, const genet_dma_descriptor *desc, bool tx) {
  if ((desc->length_status == 0) && (desc->addr_lo == 0)) return;
  dprintf(INFO, "%d: lenstat:0x%x addr:0x%x length: %d", index, desc->length_status, desc->addr_lo, desc->length_status >> DMA_BUFLENGTH_SHIFT);
  uint32_t lenstat = desc->length_status;
  if (tx) {
    if (lenstat & DMA_TX_DO_CSUM) dprintf(INFO, " DMA_TX_DO_CSUM");         // 0x10
    if (lenstat & DMA_TX_OW_CRC)  dprintf(INFO, " DMA_TX_OW_CRC");          // 0x20
    if (lenstat & DMA_TX_APPEND_CRC) dprintf(INFO, " DMA_TX_APPEND_CRC");   // 0x40
    //if (lenstat & DMA_TX_UNDERRUN)   dprintf(INFO, " DMA_TX_UNDERRUN");     // 0x200
  } else {
    if (lenstat & DMA_RX_CRC_ERROR)   dprintf(INFO, " DMA_RX_CRC_ERROR");   // 0x0002
    if (lenstat & DMA_RX_RXER)        dprintf(INFO, " DMA_RX_RXER");        // 0x0004
    if (lenstat & DMA_RX_NO)          dprintf(INFO, " DMA_RX_NO");          // 0x0008
    if (lenstat & DMA_RX_LG)          dprintf(INFO, " DMA_RX_LG");          // 0x0010
    if (lenstat & DMA_RX_BRDCAST)     dprintf(INFO, " DMA_RX_BRDCAST");     // 0x0040
  }
  if (lenstat & DMA_WRAP)          dprintf(INFO, " DMA_WRAP");            // 0x1000
  if (lenstat & DMA_SOP)            dprintf(INFO, " DMA_SOP");            // 0x2000
  if (lenstat & DMA_EOP)            dprintf(INFO, " DMA_EOP");            // 0x4000
  if (lenstat & DMA_OWN)            dprintf(INFO, " DMA_OWN");            // 0x8000
  dprintf(INFO, "\n");
}

static int cmd_genet_dump(int argc, const console_cmd_args *argv) {
  uint32_t reg = *REG32(SYS_REV_CTRL);
  uint8_t major = (reg >> 24 & 0x0f);
  if (major == 6) major = 5;
  else if (major == 5) major = 4;
  else if (major == 0) major = 1;

  dprintf(INFO, "found GENET controller version %d\n", major);

  if (major == GENET_VERSION_TARGET) {
    dprintf(INFO, "it is supported\n");
    genet_dma_descriptor * tx_ring = (genet_dma_descriptor*)(GENET_BASE + TDMA_OFFSET);
    for (int i=0; i<256; i++) {
      print_descriptor(i, &tx_ring[i], true);
    }
    genet_dma_descriptor * rx_ring = (genet_dma_descriptor*)(GENET_BASE + RDMA_OFFSET);
    for (int i=0; i<256; i++) {
      print_descriptor(i, &rx_ring[i], false);
    }
    for (int ring=0; ring<=16; ring++) {
      dma_ring_config *tconfig = get_tdma_config(ring);
      dma_ring_config *rconfig = get_rdma_config(ring);
      print_dma_config(ring, tconfig, true);
      print_dma_config(ring, rconfig, false);
    }
    printf("UMAC_CMD: 0x%x\n", *REG32(UMAC_CMD));
  }
  return 0;
}

static enum handler_return gener_isr_a(void *arg) {
  printf("genet a\n");
  return INT_NO_RESCHEDULE;
}

static enum handler_return gener_isr_b(void *arg) {
  printf("genet b\n");
  return INT_NO_RESCHEDULE;
}

static void genet_init(uint level) {
  register_int_handler(29, &gener_isr_a, NULL);
  register_int_handler(30, &gener_isr_b, NULL);

  unmask_interrupt(29);
  unmask_interrupt(30);

  *REG32(GENET_BASE + 0x214) = UMAC_IRQ_RXDMA_DONE;
}

LK_INIT_HOOK(genet, &genet_init, LK_INIT_LEVEL_PLATFORM);
