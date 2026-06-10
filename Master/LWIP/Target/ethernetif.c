/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : ethernetif.c
  * Description        : This file provides code for the configuration
  *                      of the ethernetif.c MiddleWare.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "lwip/opt.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
#include "netif/etharp.h"
#include "lwip/ethip6.h"
#include "ethernetif.h"
#include "lan8720.h"
#include <string.h>
#include "cmsis_os.h"
#include "lwip/tcpip.h"

/* Within 'USER CODE' section, code will be kept by default at each generation */
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* Private define ------------------------------------------------------------*/
/* The time to block waiting for input. */
#define TIME_WAITING_FOR_INPUT ( portMAX_DELAY )
/* Time to block waiting for transmissions to finish */
#define ETHIF_TX_TIMEOUT (2000U)
/* USER CODE BEGIN OS_THREAD_STACK_SIZE_WITH_RTOS */
/* Stack size of the interface thread */
#define INTERFACE_THREAD_STACK_SIZE ( 1024 )
/* USER CODE END OS_THREAD_STACK_SIZE_WITH_RTOS */
/* Network interface name */
#define IFNAME0 's'
#define IFNAME1 't'

/* ETH Setting  */
#define ETH_DMA_TRANSMIT_TIMEOUT               ( 20U )
#define ETH_TX_BUFFER_MAX             ((ETH_TX_DESC_CNT) * 2U)

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/* Private variables ---------------------------------------------------------*/
/*
@Note: This interface is implemented to operate in zero-copy mode only:
        - Rx Buffers will be allocated from LwIP stack Rx memory pool,
          then passed to ETH HAL driver.
        - Tx Buffers will be allocated from LwIP stack memory heap,
          then passed to ETH HAL driver.

@Notes:
  1.a. ETH DMA Rx descriptors must be contiguous, the default count is 4,
       to customize it please redefine ETH_RX_DESC_CNT in ETH GUI (Rx Descriptor Length)
       so that updated value will be generated in stm32xxxx_hal_conf.h
  1.b. ETH DMA Tx descriptors must be contiguous, the default count is 4,
       to customize it please redefine ETH_TX_DESC_CNT in ETH GUI (Tx Descriptor Length)
       so that updated value will be generated in stm32xxxx_hal_conf.h

  2.a. Rx Buffers number must be between ETH_RX_DESC_CNT and 2*ETH_RX_DESC_CNT
  2.b. Rx Buffers must have the same size: ETH_RX_BUF_SIZE, this value must
       passed to ETH DMA in the init field (heth.Init.RxBuffLen)
  2.c  The RX Ruffers addresses and sizes must be properly defined to be aligned
       to L1-CACHE line size (32 bytes).
*/

/* Data Type Definitions */
typedef enum
{
  RX_ALLOC_OK       = 0x00,
  RX_ALLOC_ERROR    = 0x01
} RxAllocStatusTypeDef;

typedef struct
{
  struct pbuf_custom pbuf_custom;
  uint8_t buff[(ETH_RX_BUF_SIZE + 31) & ~31] __ALIGNED(32);
} RxBuff_t;

/* Memory Pool Declaration */
/* M2: Must be in range [ETH_RX_DESC_CNT, 2×ETH_RX_DESC_CNT].
 * ETH_RX_DESC_CNT defaults to 4, so valid range is [4, 8].
 * 8 gives the maximum pre-allocation without wasting ~14 KB of RAM. */
#define ETH_RX_BUFFER_CNT             8U
LWIP_MEMPOOL_DECLARE(RX_POOL, ETH_RX_BUFFER_CNT, sizeof(RxBuff_t), "Zero-copy RX PBUF pool");

/* Variable Definitions */
static uint8_t RxAllocStatus;

ETH_DMADescTypeDef  DMARxDscrTab[ETH_RX_DESC_CNT]; /* Ethernet Rx DMA Descriptors */
ETH_DMADescTypeDef  DMATxDscrTab[ETH_TX_DESC_CNT]; /* Ethernet Tx DMA Descriptors */

/* USER CODE BEGIN 2 */
/* TX/RX diagnostic counters defined in ethernet_task.c */
extern volatile uint32_t g_eth_tx_count;
extern volatile uint32_t g_eth_rx_count;
/* FBES diagnostic counter defined in ethernet_task.c */
extern volatile uint32_t g_eth_fbes_count;
/* Safe system reset: ETH_PowerDown() + NVIC_SystemReset() — safe from ISR */
extern void System_SafeReset(void);
/* USER CODE END 2 */

osSemaphoreId RxPktSemaphore = NULL;   /* Semaphore to signal incoming packets */
osSemaphoreId TxPktSemaphore = NULL;   /* Semaphore to signal transmit packet complete */

/* Global Ethernet handle */
ETH_HandleTypeDef heth;
ETH_TxPacketConfig TxConfig;

/* Private function prototypes -----------------------------------------------*/
int32_t ETH_PHY_IO_Init(void);
int32_t ETH_PHY_IO_DeInit (void);
int32_t ETH_PHY_IO_ReadReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t *pRegVal);
int32_t ETH_PHY_IO_WriteReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t RegVal);
int32_t ETH_PHY_IO_GetTick(void);

lan8720_Object_t LAN8720;
lan8720_IOCtx_t  LAN8720_IOCtx = {ETH_PHY_IO_Init,
                                  ETH_PHY_IO_DeInit,
                                  ETH_PHY_IO_WriteReg,
                                  ETH_PHY_IO_ReadReg,
                                  ETH_PHY_IO_GetTick};

/* USER CODE BEGIN 3 */

/* USER CODE END 3 */

/* Private functions ---------------------------------------------------------*/
void pbuf_free_custom(struct pbuf *p);

/**
  * @brief  Ethernet Rx Transfer completed callback
  * @param  handlerEth: ETH handler
  * @retval None
  */
void HAL_ETH_RxCpltCallback(ETH_HandleTypeDef *handlerEth)
{
  osSemaphoreRelease(RxPktSemaphore);
}
/**
  * @brief  Ethernet Tx Transfer completed callback
  * @param  handlerEth: ETH handler
  * @retval None
  */
void HAL_ETH_TxCpltCallback(ETH_HandleTypeDef *handlerEth)
{
  osSemaphoreRelease(TxPktSemaphore);
}
/**
  * @brief  Ethernet DMA transfer error callback
  * @param  handlerEth: ETH handler
  * @retval None
  */
void HAL_ETH_ErrorCallback(ETH_HandleTypeDef *handlerEth)
{
  if ((HAL_ETH_GetDMAError(handlerEth) & ETH_DMASR_RBUS) == ETH_DMASR_RBUS)
  {
    osSemaphoreRelease(RxPktSemaphore);
  }
  /* Fatal Bus Error: AHB bus error on DMA — ETH peripheral state is undefined.
   * Increment diagnostic counter then perform a clean system reset:
   *   System_SafeReset() = ETH_PowerDown() [DWT delay, safe from ISR]
   *                      + NVIC_SystemReset()
   * After reset, ETH_PowerUp() re-sequences the LAN8720 PHY from scratch.
   * No conflict with LAN8720 — FBES is an MCU-internal AHB error unrelated
   * to the RMII/MDIO interface. */
  if ((HAL_ETH_GetDMAError(handlerEth) & ETH_DMASR_FBES) == ETH_DMASR_FBES)
  {
    g_eth_fbes_count++;
    System_SafeReset();
  }
}

/* USER CODE BEGIN 4 */

volatile uint32_t g_phy_diag_addr = 0U;
volatile uint32_t g_phy_diag_id1 = 0U;
volatile uint32_t g_phy_diag_id2 = 0U;
volatile uint32_t g_phy_diag_bcr = 0U;
volatile uint32_t g_phy_diag_bsr = 0U;
volatile uint32_t g_phy_diag_physcsr = 0U;
volatile int32_t  g_phy_diag_link_state = 0;
volatile uint32_t g_phy_diag_read_fail = 0U;

static void PHY_UpdateDiag(uint32_t phy_addr)
{
  g_phy_diag_addr = phy_addr;
  g_phy_diag_read_fail = 0U;

  if (HAL_ETH_ReadPHYRegister(&heth, phy_addr, 0x02U, (uint32_t *)&g_phy_diag_id1) != HAL_OK)
  {
    g_phy_diag_read_fail |= (1UL << 0);
  }
  if (HAL_ETH_ReadPHYRegister(&heth, phy_addr, 0x03U, (uint32_t *)&g_phy_diag_id2) != HAL_OK)
  {
    g_phy_diag_read_fail |= (1UL << 1);
  }
  if (HAL_ETH_ReadPHYRegister(&heth, phy_addr, LAN8720_BCR, (uint32_t *)&g_phy_diag_bcr) != HAL_OK)
  {
    g_phy_diag_read_fail |= (1UL << 2);
  }
  if (HAL_ETH_ReadPHYRegister(&heth, phy_addr, LAN8720_BSR, (uint32_t *)&g_phy_diag_bsr) != HAL_OK)
  {
    g_phy_diag_read_fail |= (1UL << 3);
  }
  if (HAL_ETH_ReadPHYRegister(&heth, phy_addr, LAN8720_SCSR, (uint32_t *)&g_phy_diag_physcsr) != HAL_OK)
  {
    g_phy_diag_read_fail |= (1UL << 4);
  }
}

/* USER CODE END 4 */

/*******************************************************************************
                       LL Driver Interface ( LwIP stack --> ETH)
*******************************************************************************/
/**
 * @brief In this function, the hardware should be initialized.
 * Called from ethernetif_init().
 *
 * @param netif the already initialized lwip network interface structure
 *        for this ethernetif
 */
static void low_level_init(struct netif *netif)
{
  HAL_StatusTypeDef hal_eth_init_status = HAL_OK;
/* USER CODE BEGIN OS_THREAD_ATTR_CMSIS_RTOS_V2 */
  osThreadAttr_t attributes;
/* USER CODE END OS_THREAD_ATTR_CMSIS_RTOS_V2 */
  uint32_t duplex = 0U, speed = 0U, linkchanged = 0U;
  int32_t PHYLinkState = 0;
  uint32_t phy_bcr = 0U;
  ETH_MACConfigTypeDef MACConf = {0};
  /* Start ETH HAL Init */

  static uint8_t MACAddr[6];
  heth.Instance = ETH;
  /* Derive a unique MAC from STM32 96-bit UID so two boards on the
   * same LAN never collide.  OUI 00:80:E1 is ST's registered prefix.
   * bit0 of byte[3] is forced to 0 (unicast, non-multicast). */
  {
    uint32_t uid0 = HAL_GetUIDw0();
    uint32_t uid1 = HAL_GetUIDw1();
    uint32_t mix  = uid0 ^ uid1;
    MACAddr[0] = 0x00U;
    MACAddr[1] = 0x80U;
    MACAddr[2] = 0xE1U;
    MACAddr[3] = (uint8_t)( mix        & 0xFEU); /* bit0=0: unicast */
    MACAddr[4] = (uint8_t)((mix >>  8) & 0xFFU);
    MACAddr[5] = (uint8_t)((mix >> 16) & 0xFFU);
  }
  heth.Init.MACAddr = &MACAddr[0];
  heth.Init.MediaInterface = HAL_ETH_RMII_MODE;
  heth.Init.TxDesc = DMATxDscrTab;
  heth.Init.RxDesc = DMARxDscrTab;
  heth.Init.RxBuffLen = 1536;

  /* USER CODE BEGIN MACADDRESS */

  /* USER CODE END MACADDRESS */

  hal_eth_init_status = HAL_ETH_Init(&heth);

  memset(&TxConfig, 0 , sizeof(ETH_TxPacketConfig));
  TxConfig.Attributes = ETH_TX_PACKETS_FEATURES_CSUM | ETH_TX_PACKETS_FEATURES_CRCPAD;
  TxConfig.ChecksumCtrl = ETH_CHECKSUM_IPHDR_PAYLOAD_INSERT_PHDR_CALC;
  TxConfig.CRCPadCtrl = ETH_CRC_PAD_INSERT;

  /* End ETH HAL Init */

  /* Initialize the RX POOL */
  LWIP_MEMPOOL_INIT(RX_POOL);

#if LWIP_ARP || LWIP_ETHERNET

  /* set MAC hardware address length */
  netif->hwaddr_len = ETH_HWADDR_LEN;

  /* set MAC hardware address */
  netif->hwaddr[0] =  heth.Init.MACAddr[0];
  netif->hwaddr[1] =  heth.Init.MACAddr[1];
  netif->hwaddr[2] =  heth.Init.MACAddr[2];
  netif->hwaddr[3] =  heth.Init.MACAddr[3];
  netif->hwaddr[4] =  heth.Init.MACAddr[4];
  netif->hwaddr[5] =  heth.Init.MACAddr[5];

  /* maximum transfer unit */
  netif->mtu = ETH_MAX_PAYLOAD;

  /* Accept broadcast address and ARP traffic */
  /* don't set NETIF_FLAG_ETHARP if this device is not an ethernet one */
  /* NETIF_FLAG_ETHERNET must also be set so LwIP treats this as a real     */
  /* Ethernet interface: gratuitous ARP on link-up, correct DHCP ARP check  */
  /* path, and netif_is_ethernet() returning true throughout the stack.      */
  #if LWIP_ARP
    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
  #else
    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHERNET;
  #endif /* LWIP_ARP */

  /* create a binary semaphore used for informing ethernetif of frame reception */
  RxPktSemaphore = osSemaphoreNew(1, 0, NULL);

  /* create a binary semaphore used for informing ethernetif of frame transmission */
  TxPktSemaphore = osSemaphoreNew(1, 0, NULL);

  /* create the task that handles the ETH_MAC */
/* USER CODE BEGIN OS_THREAD_NEW_CMSIS_RTOS_V2 */
  memset(&attributes, 0x0, sizeof(osThreadAttr_t));
  attributes.name = "EthIf";
  attributes.stack_size = INTERFACE_THREAD_STACK_SIZE;
  attributes.priority = osPriorityHigh;   /* below Realtime; above tcpip_thread(prio 24) */
  osThreadNew(ethernetif_input, netif, &attributes);
/* USER CODE END OS_THREAD_NEW_CMSIS_RTOS_V2 */

/* USER CODE BEGIN PHY_PRE_CONFIG */

/* USER CODE END PHY_PRE_CONFIG */
  /* Set PHY IO functions */
  LAN8720_RegisterBusIO(&LAN8720, &LAN8720_IOCtx);

  /* LAN8720 PHY address = 0x00: PHYAD0 (pin 10, RXER/PHYAD0) pulled to GND
   * via R413 (4.7 kΩ). Manually assign and mark as initialized to skip
   * the MDIO scan (which requires MDIO to be fully settled first). */
  LAN8720.DevAddr = LAN8720_PHY_ADDRESS;
  LAN8720.Is_Initialized = 1U;
  (void)LAN8720_DisablePowerDownMode(&LAN8720);

  /* Soft-reset the LAN8720, clear power-down and isolate bits,
   * then restart auto-negotiation. */
  if (HAL_ETH_ReadPHYRegister(&heth, LAN8720.DevAddr, LAN8720_BCR, &phy_bcr) == HAL_OK)
  {
    phy_bcr &= ~LAN8720_BCR_POWER_DOWN;
    phy_bcr &= ~LAN8720_BCR_ISOLATE;
    phy_bcr |= LAN8720_BCR_AUTONEGO_EN | LAN8720_BCR_RESTART_AUTONEGO | LAN8720_BCR_SOFT_RESET;
    (void)HAL_ETH_WritePHYRegister(&heth, LAN8720.DevAddr, LAN8720_BCR, phy_bcr);

    for (uint32_t retry = 0U; retry < 100U; retry++)
    {
      osDelay(10U);
      if (HAL_ETH_ReadPHYRegister(&heth, LAN8720.DevAddr, LAN8720_BCR, &phy_bcr) != HAL_OK)
      {
        break;
      }
      if ((phy_bcr & LAN8720_BCR_SOFT_RESET) == 0U)
      {
        break;
      }
    }
  }

  /* Mask all LAN8720 interrupts so that pin 14 (INT/REFCLKO, open-drain)
   * never asserts.  Pin 14 shares the same net as PA1 (ETH_REF_CLK) and
   * XTAL1 — an open-drain pull-low on an active 50 MHz line would glitch
   * the reference clock for both the MCU ETH MAC and the PHY itself. */
  (void)HAL_ETH_WritePHYRegister(&heth, LAN8720.DevAddr, LAN8720_IMR, 0x0000U);

  PHY_UpdateDiag(LAN8720.DevAddr);

  if (hal_eth_init_status == HAL_OK)
  {
    PHYLinkState = LAN8720_GetLinkState(&LAN8720);
    g_phy_diag_link_state = PHYLinkState;
    PHY_UpdateDiag(LAN8720.DevAddr);

    /* Get link state */
    /* Treat AUTONEGO_NOTDONE (6) the same as LINK_DOWN: do NOT start the ETH
     * DMA with a guessed speed.  ethernet_link_thread polls every 100 ms and
     * will configure the MAC with the correct speed/duplex once auto-
     * negotiation completes, then call netif_set_link_up().            */
    if(PHYLinkState <= LAN8720_STATUS_LINK_DOWN ||
       PHYLinkState == LAN8720_STATUS_AUTONEGO_NOTDONE)
    {
      netif_set_link_down(netif);
    }
    else
    {
      /* Use the same linkchanged pattern as ethernet_link_thread so that
       * an unrecognised PHY state never starts the DMA with speed=0.
       * If linkchanged stays 0, ethernet_link_thread will pick up the
       * correct speed/duplex on its next 100 ms poll and call
       * netif_set_link_up() from there (delegate approach). */
      switch (PHYLinkState)
      {
      case LAN8720_STATUS_100MBITS_FULLDUPLEX:
        duplex = ETH_FULLDUPLEX_MODE;
        speed  = ETH_SPEED_100M;
        linkchanged = 1U;
        break;
      case LAN8720_STATUS_100MBITS_HALFDUPLEX:
        duplex = ETH_HALFDUPLEX_MODE;
        speed  = ETH_SPEED_100M;
        linkchanged = 1U;
        break;
      case LAN8720_STATUS_10MBITS_FULLDUPLEX:
        duplex = ETH_FULLDUPLEX_MODE;
        speed  = ETH_SPEED_10M;
        linkchanged = 1U;
        break;
      case LAN8720_STATUS_10MBITS_HALFDUPLEX:
        duplex = ETH_HALFDUPLEX_MODE;
        speed  = ETH_SPEED_10M;
        linkchanged = 1U;
        break;
      default:
        /* Unrecognised state — do not start DMA; delegate to
         * ethernet_link_thread which polls every 100 ms. */
        break;
      }

      if (linkchanged)
      {
        /* Autoneg is complete and speed/duplex are known: configure the
         * STM32 MAC to match before enabling the DMA. */
        HAL_ETH_GetMACConfig(&heth, &MACConf);
        MACConf.DuplexMode = duplex;
        MACConf.Speed      = speed;
        HAL_ETH_SetMACConfig(&heth, &MACConf);

        HAL_ETH_Start_IT(&heth);
        /* Disable hardware dropping of frames whose UDP checksum field is
         * zero (valid per RFC 768).  DHCP OFFER packets carry checksum=0;
         * without DTCEFD=1 the DMA silently drops them, keeping DHCP
         * stuck at SELECTING forever. */
        heth.Instance->DMAOMR |= ETH_DMAOMR_DTCEFD;
        netif_set_link_up(netif);
      }
      /* else: linkchanged=0 → netif stays link-down; ethernet_link_thread
       *       will bring it up once autoneg finishes. */
/* USER CODE BEGIN PHY_POST_CONFIG */

/* USER CODE END PHY_POST_CONFIG */
    }

  }
  else
  {
    Error_Handler();
  }
#endif /* LWIP_ARP || LWIP_ETHERNET */

/* USER CODE BEGIN LOW_LEVEL_INIT */

/* USER CODE END LOW_LEVEL_INIT */
}

/**
 * @brief This function should do the actual transmission of the packet. The packet is
 * contained in the pbuf that is passed to the function. This pbuf
 * might be chained.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @param p the MAC packet to send (e.g. IP packet including MAC addresses and type)
 * @return ERR_OK if the packet could be sent
 *         an err_t value if the packet couldn't be sent
 *
 * @note Returning ERR_MEM here if a DMA queue of your MAC is full can lead to
 *       strange results. You might consider waiting for space in the DMA queue
 *       to become available since the stack doesn't retry to send a packet
 *       dropped because of memory failure (except for the TCP timers).
 */

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
  /* Release any TX descriptors that completed since the last call.
   * Without this, the 4-entry TX descriptor ring fills after 4 frames
   * and every subsequent HAL_ETH_Transmit_IT() returns HAL_ETH_ERROR_BUSY,
   * forcing all traffic through the slow BUSY-retry path. */
  HAL_ETH_ReleaseTxPacket(&heth);

  uint32_t i = 0U;
  struct pbuf *q = NULL;
  err_t errval = ERR_OK;
  ETH_BufferTypeDef Txbuffer[ETH_TX_DESC_CNT] = {0};

  memset(Txbuffer, 0 , ETH_TX_DESC_CNT*sizeof(ETH_BufferTypeDef));

  for(q = p; q != NULL; q = q->next)
  {
    if(i >= ETH_TX_DESC_CNT)
      return ERR_IF;

    Txbuffer[i].buffer = q->payload;
    Txbuffer[i].len = q->len;

    if(i>0)
    {
      Txbuffer[i-1].next = &Txbuffer[i];
    }

    if(q->next == NULL)
    {
      Txbuffer[i].next = NULL;
    }

    i++;
  }

  TxConfig.Length = p->tot_len;
  TxConfig.TxBuffer = Txbuffer;
  TxConfig.pData = p;

  pbuf_ref(p);

  do
  {
    if(HAL_ETH_Transmit_IT(&heth, &TxConfig) == HAL_OK)
    {
      g_eth_tx_count++;
      errval = ERR_OK;
    }
    else
    {

      if(HAL_ETH_GetError(&heth) & HAL_ETH_ERROR_BUSY)
      {
        /* Wait for descriptors to become available */
        osSemaphoreAcquire(TxPktSemaphore, ETHIF_TX_TIMEOUT);
        HAL_ETH_ReleaseTxPacket(&heth);
        errval = ERR_BUF;
      }
      else
      {
        /* Other error */
        pbuf_free(p);
        errval =  ERR_IF;
      }
    }
  }while(errval == ERR_BUF);

  return errval;
}

/**
 * @brief Should allocate a pbuf and transfer the bytes of the incoming
 * packet from the interface into the pbuf.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return a pbuf filled with the received packet (including MAC header)
 *         NULL on memory error
   */
static struct pbuf * low_level_input(struct netif *netif)
{
  struct pbuf *p = NULL;

  if(RxAllocStatus == RX_ALLOC_OK)
  {
    HAL_ETH_ReadData(&heth, (void **)&p);
  }

  return p;
}

/**
 * @brief This function should be called when a packet is ready to be read
 * from the interface. It uses the function low_level_input() that
 * should handle the actual reception of bytes from the network
 * interface. Then the type of the received packet is determined and
 * the appropriate input function is called.
 *
 * @param netif the lwip network interface structure for this ethernetif
 */
void ethernetif_input(void* argument)
{
  struct pbuf *p = NULL;
  struct netif *netif = (struct netif *) argument;

  for( ;; )
  {
    if (osSemaphoreAcquire(RxPktSemaphore, TIME_WAITING_FOR_INPUT) == osOK)
    {
      do
      {
        p = low_level_input( netif );
        if (p != NULL)
        {
          g_eth_rx_count++;
          if (netif->input( p, netif) != ERR_OK )
          {
            pbuf_free(p);
          }
        }
      } while(p!=NULL);
    }
  }
}

#if !LWIP_ARP
/**
 * This function has to be completed by user in case of ARP OFF.
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return ERR_OK if ...
 */
static err_t low_level_output_arp_off(struct netif *netif, struct pbuf *q, const ip4_addr_t *ipaddr)
{
  err_t errval;
  errval = ERR_OK;

/* USER CODE BEGIN 5 */

/* USER CODE END 5 */

  return errval;

}
#endif /* LWIP_ARP */

/**
 * @brief Should be called at the beginning of the program to set up the
 * network interface. It calls the function low_level_init() to do the
 * actual setup of the hardware.
 *
 * This function should be passed as a parameter to netif_add().
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return ERR_OK if the loopif is initialized
 *         ERR_MEM if private data couldn't be allocated
 *         any other err_t on error
 */
err_t ethernetif_init(struct netif *netif)
{
  LWIP_ASSERT("netif != NULL", (netif != NULL));

#if LWIP_NETIF_HOSTNAME
  /* Initialize interface hostname */
  netif->hostname = "lwip";
#endif /* LWIP_NETIF_HOSTNAME */

  /*
   * Initialize the snmp variables and counters inside the struct netif.
   * The last argument should be replaced with your link speed, in units
   * of bits per second.
   */
  // MIB2_INIT_NETIF(netif, snmp_ifType_ethernet_csmacd, LINK_SPEED_OF_YOUR_NETIF_IN_BPS);

  netif->name[0] = IFNAME0;
  netif->name[1] = IFNAME1;
  /* We directly use etharp_output() here to save a function call.
   * You can instead declare your own function an call etharp_output()
   * from it if you have to do some checks before sending (e.g. if link
   * is available...) */

#if LWIP_IPV4
#if LWIP_ARP || LWIP_ETHERNET
#if LWIP_ARP
  netif->output = etharp_output;
#else
  /* The user should write its own code in low_level_output_arp_off function */
  netif->output = low_level_output_arp_off;
#endif /* LWIP_ARP */
#endif /* LWIP_ARP || LWIP_ETHERNET */
#endif /* LWIP_IPV4 */

#if LWIP_IPV6
  netif->output_ip6 = ethip6_output;
#endif /* LWIP_IPV6 */

  netif->linkoutput = low_level_output;

  /* initialize the hardware */
  low_level_init(netif);

  return ERR_OK;
}

/**
  * @brief  Custom Rx pbuf free callback
  * @param  pbuf: pbuf to be freed
  * @retval None
  */
void pbuf_free_custom(struct pbuf *p)
{
  struct pbuf_custom* custom_pbuf = (struct pbuf_custom*)p;
  LWIP_MEMPOOL_FREE(RX_POOL, custom_pbuf);

  /* If the Rx Buffer Pool was exhausted, signal the ethernetif_input task to
   * call HAL_ETH_GetRxDataBuffer to rebuild the Rx descriptors. */

  if (RxAllocStatus == RX_ALLOC_ERROR)
  {
    RxAllocStatus = RX_ALLOC_OK;
    osSemaphoreRelease(RxPktSemaphore);
  }
}

/* USER CODE BEGIN 6 */

/**
* @brief  Returns the current time in milliseconds
*         when LWIP_TIMERS == 1 and NO_SYS == 1
* @param  None
* @retval Current Time value
*/
u32_t sys_now(void)
{
  return HAL_GetTick();
}

/* USER CODE END 6 */

/**
  * @brief  Initializes the ETH MSP.
  * @param  ethHandle: ETH handle
  * @retval None
  */

void HAL_ETH_MspInit(ETH_HandleTypeDef* ethHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(ethHandle->Instance==ETH)
  {
  /* USER CODE BEGIN ETH_MspInit 0 */

  /* USER CODE END ETH_MspInit 0 */
    /* Enable Peripheral clock */
    __HAL_RCC_ETH_CLK_ENABLE();

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**ETH GPIO Configuration
    PC1     ------> ETH_MDC
    PA1     ------> ETH_REF_CLK
    PA2     ------> ETH_MDIO
    PA7     ------> ETH_CRS_DV
    PC4     ------> ETH_RXD0
    PC5     ------> ETH_RXD1
    PB11     ------> ETH_TX_EN
    PB12     ------> ETH_TXD0
    PB13     ------> ETH_TXD1
    */
    GPIO_InitStruct.Pin = GPIO_PIN_1|GPIO_PIN_4|GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_7;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_13;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF11_ETH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* Peripheral interrupt init */
    HAL_NVIC_SetPriority(ETH_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(ETH_IRQn);
  /* USER CODE BEGIN ETH_MspInit 1 */

  /* USER CODE END ETH_MspInit 1 */
  }
}

void HAL_ETH_MspDeInit(ETH_HandleTypeDef* ethHandle)
{
  if(ethHandle->Instance==ETH)
  {
  /* USER CODE BEGIN ETH_MspDeInit 0 */

  /* USER CODE END ETH_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_ETH_CLK_DISABLE();

    /**ETH GPIO Configuration
    PC1     ------> ETH_MDC
    PA1     ------> ETH_REF_CLK
    PA2     ------> ETH_MDIO
    PA7     ------> ETH_CRS_DV
    PC4     ------> ETH_RXD0
    PC5     ------> ETH_RXD1
    PB11     ------> ETH_TX_EN
    PB12     ------> ETH_TXD0
    PB13     ------> ETH_TXD1
    */
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_1|GPIO_PIN_4|GPIO_PIN_5);

    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_7);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_11|GPIO_PIN_12|GPIO_PIN_13);

    /* Peripheral interrupt Deinit*/
    HAL_NVIC_DisableIRQ(ETH_IRQn);

  /* USER CODE BEGIN ETH_MspDeInit 1 */

  /* USER CODE END ETH_MspDeInit 1 */
  }
}

/*******************************************************************************
                       PHI IO Functions
*******************************************************************************/
/**
  * @brief  Initializes the MDIO interface GPIO and clocks.
  * @param  None
  * @retval 0 if OK, -1 if ERROR
  */
int32_t ETH_PHY_IO_Init(void)
{
  /* We assume that MDIO GPIO configuration is already done
     in the ETH_MspInit() else it should be done here
  */

  /* Configure the MDIO Clock */
  HAL_ETH_SetMDIOClockRange(&heth);

  return 0;
}

/**
  * @brief  De-Initializes the MDIO interface .
  * @param  None
  * @retval 0 if OK, -1 if ERROR
  */
int32_t ETH_PHY_IO_DeInit (void)
{
  return 0;
}

/**
  * @brief  Read a PHY register through the MDIO interface.
  * @param  DevAddr: PHY port address
  * @param  RegAddr: PHY register address
  * @param  pRegVal: pointer to hold the register value
  * @retval 0 if OK -1 if Error
  */
int32_t ETH_PHY_IO_ReadReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t *pRegVal)
{
  if(HAL_ETH_ReadPHYRegister(&heth, DevAddr, RegAddr, pRegVal) != HAL_OK)
  {
    return -1;
  }

  return 0;
}

/**
  * @brief  Write a value to a PHY register through the MDIO interface.
  * @param  DevAddr: PHY port address
  * @param  RegAddr: PHY register address
  * @param  RegVal: Value to be written
  * @retval 0 if OK -1 if Error
  */
int32_t ETH_PHY_IO_WriteReg(uint32_t DevAddr, uint32_t RegAddr, uint32_t RegVal)
{
  if(HAL_ETH_WritePHYRegister(&heth, DevAddr, RegAddr, RegVal) != HAL_OK)
  {
    return -1;
  }

  return 0;
}

/**
  * @brief  Get the time in millisecons used for internal PHY driver process.
  * @retval Time value
  */
int32_t ETH_PHY_IO_GetTick(void)
{
  return HAL_GetTick();
}

/**
  * @brief  Check the ETH link state then update ETH driver and netif link accordingly.
  * @retval None
  */
void ethernet_link_thread(void* argument)
{
  ETH_MACConfigTypeDef MACConf = {0};
  int32_t PHYLinkState = 0;
  uint32_t linkchanged = 0U, speed = 0U, duplex = 0U;

  struct netif *netif = (struct netif *) argument;
/* USER CODE BEGIN ETH link init */

/* USER CODE END ETH link init */

  for(;;)
  {
  linkchanged = 0U;   /* M4: reset each iteration to prevent repeated Start_IT */
  speed  = 0U;
  duplex = 0U;

  PHYLinkState = LAN8720_GetLinkState(&LAN8720);
  g_phy_diag_link_state = PHYLinkState;
  PHY_UpdateDiag(LAN8720.DevAddr);

  if(netif_is_link_up(netif) && (PHYLinkState <= LAN8720_STATUS_LINK_DOWN))
  {
    HAL_ETH_Stop_IT(&heth);
    netif_set_link_down(netif);
  }
  else if(!netif_is_link_up(netif) && (PHYLinkState > LAN8720_STATUS_LINK_DOWN))
  {
    switch (PHYLinkState)
    {
    case LAN8720_STATUS_100MBITS_FULLDUPLEX:
      duplex = ETH_FULLDUPLEX_MODE;
      speed = ETH_SPEED_100M;
      linkchanged = 1;
      break;
    case LAN8720_STATUS_100MBITS_HALFDUPLEX:
      duplex = ETH_HALFDUPLEX_MODE;
      speed = ETH_SPEED_100M;
      linkchanged = 1;
      break;
    case LAN8720_STATUS_10MBITS_FULLDUPLEX:
      duplex = ETH_FULLDUPLEX_MODE;
      speed = ETH_SPEED_10M;
      linkchanged = 1;
      break;
    case LAN8720_STATUS_10MBITS_HALFDUPLEX:
      duplex = ETH_HALFDUPLEX_MODE;
      speed = ETH_SPEED_10M;
      linkchanged = 1;
      break;
    default:
      break;
    }

    if(linkchanged)
    {
      /* Get MAC Config MAC */
      HAL_ETH_GetMACConfig(&heth, &MACConf);
      MACConf.DuplexMode = duplex;
      MACConf.Speed = speed;
      HAL_ETH_SetMACConfig(&heth, &MACConf);
      HAL_ETH_Start_IT(&heth);
      heth.Instance->DMAOMR |= ETH_DMAOMR_DTCEFD;  /* keep DTCEFD set after each Start */
      netif_set_link_up(netif);
    }
  }

/* USER CODE BEGIN ETH link Thread core code for User BSP */

/* USER CODE END ETH link Thread core code for User BSP */

    osDelay(100);
  }
}

void HAL_ETH_RxAllocateCallback(uint8_t **buff)
{
/* USER CODE BEGIN HAL ETH RxAllocateCallback */
  struct pbuf_custom *p = LWIP_MEMPOOL_ALLOC(RX_POOL);
  if (p)
  {
    /* Get the buff from the struct pbuf address. */
    *buff = (uint8_t *)p + offsetof(RxBuff_t, buff);
    p->custom_free_function = pbuf_free_custom;
    /* Initialize the struct pbuf.
    * This must be performed whenever a buffer's allocated because it may be
    * changed by lwIP or the app, e.g., pbuf_free decrements ref. */
    pbuf_alloced_custom(PBUF_RAW, 0, PBUF_REF, p, *buff, ETH_RX_BUF_SIZE);
  }
  else
  {
    RxAllocStatus = RX_ALLOC_ERROR;
    *buff = NULL;
  }
/* USER CODE END HAL ETH RxAllocateCallback */
}

void HAL_ETH_RxLinkCallback(void **pStart, void **pEnd, uint8_t *buff, uint16_t Length)
{
/* USER CODE BEGIN HAL ETH RxLinkCallback */

  struct pbuf **ppStart = (struct pbuf **)pStart;
  struct pbuf **ppEnd = (struct pbuf **)pEnd;
  struct pbuf *p = NULL;

  /* Get the struct pbuf from the buff address. */
  p = (struct pbuf *)(buff - offsetof(RxBuff_t, buff));
  p->next = NULL;
  p->tot_len = 0;
  p->len = Length;

  /* Chain the buffer. */
  if (!*ppStart)
  {
    /* The first buffer of the packet. */
    *ppStart = p;
  }
  else
  {
    /* Chain the buffer to the end of the packet. */
    (*ppEnd)->next = p;
  }
  *ppEnd  = p;

  /* Update the total length of all the buffers of the chain. Each pbuf in the chain should have its tot_len
   * set to its own length, plus the length of all the following pbufs in the chain. */
  for (p = *ppStart; p != NULL; p = p->next)
  {
    p->tot_len += Length;
  }

/* USER CODE END HAL ETH RxLinkCallback */
}

void HAL_ETH_TxFreeCallback(uint32_t * buff)
{
/* USER CODE BEGIN HAL ETH TxFreeCallback */

  pbuf_free((struct pbuf *)buff);

/* USER CODE END HAL ETH TxFreeCallback */
}

/* USER CODE BEGIN 8 */

/* USER CODE END 8 */
