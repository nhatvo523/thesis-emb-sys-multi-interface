/**
 * @file    ethernet_task.c
 * @brief   Ethernet task — LwIP initialisation, DHCP monitoring,
 *          HTTP web server, and physical link state management.
 *
 * Stack   : 4 kB  (Task_Ethernet_attributes.stack_size = 1024 * 4)
 * Priority: osPriorityNormal
 *
 * Architecture
 * ------------
 * MX_LWIP_Init() spawns two LwIP-internal threads:
 *   tcpip_thread         — runs all LwIP protocol callbacks and timers.
 *   ethernet_link_thread — polls the LAN8720 PHY and calls
 *                          netif_set_link_up() / netif_set_link_down().
 *
 * This task's 500 ms polling loop does three things:
 *   a) Mirrors PHY link state → netif_set_up() / netif_set_down() so LwIP
 *      does not route packets over a dead link.
 *   b) Detects when DHCP assigns the first IP address, writes ip_addr /
 *      ip_mask / ip_gw into g_appData, and fires EVT_IP_ACQUIRED once.
 *   c) Clears IP fields and resets the "reported" flag on link down so
 *      EVT_IP_ACQUIRED fires again if the cable is re-plugged.
 *
 * All writes to g_appData are protected by g_dataMutex.
 * EVT_IP_ACQUIRED is set AFTER the mutex is released.
 */

#include "ethernet_task.h"
#include "main.h"               /* ETH_PWR_CTRL / ETH_CR_EN / ETH_RST macros */
#include "app_data.h"           /* g_appData, g_dataMutex, g_systemEvents    */
#include "lwip.h"               /* MX_LWIP_Init(), gnetif                    */
#include "lwip/netif.h"         /* netif_is_link_up, netif_set_up/down       */
#include "lwip/dhcp.h"          /* dhcp_start(), dhcp_stop(), netif_dhcp_data() */
#include "lwip/ip_addr.h"       /* ip_addr_t, IP4_ADDR, netif_set_addr       */
#include "lwip/tcpip.h"         /* tcpip_callback() — thread-safe LwIP calls */

/* Wrappers so dhcp_start/stop/addr run inside tcpip_thread (thread-safe). */
static void netif_set_up_cb(void *arg)   { netif_set_up((struct netif *)arg);   }
static void netif_set_down_cb(void *arg) { netif_set_down((struct netif *)arg); }
static void dhcp_start_cb(void *arg) { dhcp_start((struct netif *)arg); }
static void dhcp_stop_cb(void *arg)  { dhcp_stop((struct netif *)arg);  }

typedef struct { struct netif *netif; ip_addr_t ip, nm, gw; } StaticIPArgs;
static StaticIPArgs s_static_args;
static void set_static_ip_cb(void *arg)
{
    StaticIPArgs *a = (StaticIPArgs *)arg;
    dhcp_stop(a->netif);
    netif_set_addr(a->netif, &a->ip, &a->nm, &a->gw);
}

/* -----------------------------------------------------------------------
 * Uncomment to test with static IP instead of DHCP.
 * After confirming connectivity with ping, comment out and rebuild.
 * ----------------------------------------------------------------------- */
 //#define ETH_STATIC_IP_TEST

#include "lan8720.h"            /* lan8720_Object_t — PHY driver               */
#include "web_server.h"         /* WebServer_Init()                          */
#include "sse_server.h"         /* SSE_Server_Init()                         */
#include "cmsis_os.h"           /* osDelay(), osMutexAcquire/Release         */
#include "usbd_cdc_if.h"        /* CDC_Transmit_FS() — debug logging         */
#include <string.h>             /* strlen()                                  */
#include <stdio.h>              /* snprintf()                                */

/* gnetif is defined in LWIP/App/lwip.c as the default network interface. */
extern struct netif gnetif;

volatile uint32_t g_eth_tx_count = 0;
volatile uint32_t g_eth_rx_count = 0;
volatile uint32_t g_eth_fbes_count = 0;  /* incremented on each FBES (Fatal Bus Error) */

/* ===========================================================================
 * Private constants
 * ========================================================================= */

/** Task poll interval — governs both link-state check and DHCP detection. */
#define LINK_POLL_MS    500U

/* ===========================================================================
 * ETH PHY power control — LAN8720AI-CP-TR on RMII
 * ========================================================================= */

/**
 * @brief  Busy-wait delay using the DWT cycle counter.
 *
 * This helper is intentionally NOT using HAL_Delay() or osDelay() because
 * ETH_PowerDown() must work in every possible execution context:
 *   - Bare-metal before osKernelStart()  → osDelay() not available
 *   - FreeRTOS task context             → either works, but DWT is simplest
 *   - HardFault / BusFault / MemManage  → SysTick cannot preempt (fixed
 *                                         priority -1), HAL_Delay() deadlocks;
 *                                         osDelay() would crash the kernel
 *   - Any IRQ handler                   → osDelay() forbidden from ISR
 *
 * DWT->CYCCNT increments at HCLK (168 MHz) with no dependency on any
 * interrupt source.  It is always accessible from privileged code.
 *
 * @param  ms  Delay in whole milliseconds (max ~25 s before 32-bit wrap).
 */
static void ETH_BusyDelay_ms(uint32_t ms)
{
    /* Enable the DWT unit and its cycle counter if not already running.
     * These registers are in the CoreDebug/DWT address space and are always
     * writable from privileged mode, regardless of debug connection state. */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL        |= DWT_CTRL_CYCCNTENA_Msk;

    uint32_t ticks = ms * (SystemCoreClock / 1000U);
    uint32_t start = DWT->CYCCNT;
    /* Subtraction handles the single 32-bit wrap that can occur for delays
     * up to ~25 s at 168 MHz.  For the 1–10 ms delays used here, wrap is
     * impossible, but the form is correct regardless. */
    while ((DWT->CYCCNT - start) < ticks)
    {
        __NOP();
    }
}

void ETH_PowerUp(void)
{
    /* Force a full hardware power cycle first so the PHY cannot remain in a
     * stale state across reflashes or partial resets. */
    HAL_GPIO_WritePin(ETH_RST_PORT, ETH_RST_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ETH_CR_EN_PORT, ETH_CR_EN_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(ETH_PWR_CTRL_PORT, ETH_PWR_CTRL_PIN, GPIO_PIN_RESET);
    HAL_Delay(20U);

    /* Step 1: Enable ETH_VDD rail.
     * PA8 HIGH → Q401 (N-ch) ON → Q400 (P-ch) ON → ETH_VDD powered.
     * Logic is inverted: HIGH = rail ON, LOW = rail OFF. */
    HAL_GPIO_WritePin(ETH_PWR_CTRL_PORT, ETH_PWR_CTRL_PIN, GPIO_PIN_SET);

    /* Step 2: Wait for ETH_VDD bulk capacitors and PHY core rail to settle. */
    HAL_Delay(50U);

    /* Step 3: Start 50 MHz CMOS oscillator (Y400).
     * PB14 HIGH → OE pin asserted → Y400 output active → XTAL1 of LAN8720
     * receives 50 MHz clock via R420 (33 Ω series termination). */
    HAL_GPIO_WritePin(ETH_CR_EN_PORT, ETH_CR_EN_PIN, GPIO_PIN_SET);

    /* Step 4: Give the oscillator enough time to start and stabilize. */
    HAL_Delay(20U);

    /* Step 5: Assert LAN8720 reset (active-low).
     * PB15 LOW → RST pin 15 of LAN8720 pulled low.
     * R403 (4.7 kΩ) pull-up is to ETH_VDD, so it is now active. */
    HAL_GPIO_WritePin(ETH_RST_PORT, ETH_RST_PIN, GPIO_PIN_RESET);

    /* Step 6: Hold reset long enough to guarantee a clean strap latch. */
    HAL_Delay(20U);

    /* Step 7: Release LAN8720 reset.
     * PB15 HIGH → RST pin deasserted → PHY begins internal boot sequence. */
    HAL_GPIO_WritePin(ETH_RST_PORT, ETH_RST_PIN, GPIO_PIN_SET);

    /* Step 8: Wait for LAN8720 internal boot and analog front-end startup.
     * After this delay, MDIO/SMI register access is guaranteed ready. */
    HAL_Delay(200U);

    /* Step 9: (automatic) Ethernet_Task_Run() calls MX_LWIP_Init() after the
     * RTOS scheduler starts.  MX_LWIP_Init() → ethernetif_init() →
     * low_level_init() → HAL_ETH_Init() + HAL_ETH_Start().
     * Do NOT call HAL_ETH_Init() here to avoid double-initialisation. */
}

void ETH_PowerDown(void)
{
    /* All delays use ETH_BusyDelay_ms() (DWT cycle counter) instead of
     * HAL_Delay() or osDelay().  This function is called from fault handlers
     * (HardFault, BusFault, MemManage, UsageFault) where SysTick cannot
     * preempt and the RTOS kernel must not be touched. See ETH_BusyDelay_ms()
     * above for full rationale. */

    /* Step 1: Assert PHY reset before cutting supply.
     * This ensures a clean reset state when power is restored.
     * R403 (4.7 kΩ) pull-up to ETH_VDD keeps RST defined until VDD drops. */
    HAL_GPIO_WritePin(ETH_RST_PORT, ETH_RST_PIN, GPIO_PIN_RESET);

    /* Step 2: 1 ms — allows PHY to register the reset assertion. */
    ETH_BusyDelay_ms(1U);

    /* Step 3: Stop the 50 MHz oscillator.
     * PB14 LOW → OE of Y400 deasserted → output goes Hi-Z → no clock to PHY.
     * This eliminates the reverse-current path into XTAL1 before VDD drops. */
    HAL_GPIO_WritePin(ETH_CR_EN_PORT, ETH_CR_EN_PIN, GPIO_PIN_RESET);

    /* Step 4: 1 ms before cutting power. */
    ETH_BusyDelay_ms(1U);

    /* Step 5: Cut ETH_VDD rail.
     * PA8 LOW → Q401 OFF → Q400 OFF → ETH_VDD collapses.
     * PHY is in reset, oscillator is stopped — no reverse current via XTAL1,
     * no I/O pins driven above Vcc during discharge. */
    HAL_GPIO_WritePin(ETH_PWR_CTRL_PORT, ETH_PWR_CTRL_PIN, GPIO_PIN_RESET);
}

void System_SafeReset(void)
{
    ETH_PowerDown();      /* ~2 ms: RST → osc off → VDD off via DWT delay */
    NVIC_SystemReset();   /* triggers Cortex-M4 system reset               */
}

/* ===========================================================================
 * Private helpers
 * ========================================================================= */

/**
 * @brief  Thread-safe CDC_Transmit_FS() wrapper for ethernet_task.
 *
 * Acquires g_cdcMutex (shared with usb_task::log_usb()) so that concurrent
 * CDC_Transmit_FS() calls from different tasks never overlap.  USB CDC is
 * not thread-safe internally: two concurrent callers corrupt the internal
 * TxBuffer/TxLength fields and produce garbled or missing log output.
 *
 * Timeout 50 ms >> worst-case USB bulk transfer time (~1 ms at FS 12 Mbps)
 * but short enough to never stall the DHCP/link-state machine.
 * On timeout the log line is silently dropped — debug output is non-critical.
 *
 * @param  buf  Data to transmit.
 * @param  len  Number of bytes.
 */
static void eth_cdc_log(const uint8_t *buf, uint16_t len)
{
    if (len == 0U) { return; }
    if (osMutexAcquire(g_cdcMutex, 50U) == osOK)
    {
        CDC_Transmit_FS((uint8_t *)buf, len);
        osMutexRelease(g_cdcMutex);
    }
}

/* ===========================================================================
 * Public API
 * ========================================================================= */

void Ethernet_Task_Init(void)
{
    /* No pre-scheduler resources needed; LwIP init happens inside the task. */
}

void Ethernet_Task_Run(void *argument)
{
    (void)argument;

    /* -----------------------------------------------------------------------
     * 1. Initialise LwIP stack.
     *    After return: tcpip_thread and ethernet_link_thread are running,
     *    gnetif is registered, and DHCP negotiation has started.
     * ---------------------------------------------------------------------- */
    MX_LWIP_Init();

    /* DTCEFD (DMAOMR bit 26) is already set inside low_level_init() and
     * ethernet_link_thread() immediately after every HAL_ETH_Start_IT() call.
     * The redundant write below is intentionally kept as a belt-and-suspenders
     * safety net: if a HAL_ETH_Stop_IT/Start_IT sequence occurs between
     * low_level_init() and this line it ensures the bit is set before the
     * first DHCP OFFER can arrive.
     * Access is safe: heth.Instance is a hardware register (atomic 32-bit
     * read-modify-write on Cortex-M4 is not atomic, but DTCEFD is set-only
     * here and cleared nowhere — a concurrent set from ethernet_link_thread
     * is harmless). */
    {
        extern ETH_HandleTypeDef heth;
        heth.Instance->DMAOMR |= ETH_DMAOMR_DTCEFD;
    }

    /* LAN8720 uses standard BCR/BSR registers only; no vendor-specific
     * post-init register writes are required here. */

    /* -----------------------------------------------------------------------
     * 2. Start HTTP web server on port 80.
     * ---------------------------------------------------------------------- */
    WebServer_Init();

    /* -----------------------------------------------------------------------
     * 3. Start SSE server on port 8080 (real-time push to browsers).
     *    schedules tcp_new/bind/listen inside tcpip_thread via tcpip_callback.
     * ---------------------------------------------------------------------- */
    SSE_Server_Init();

    /* -----------------------------------------------------------------------
     * 3. Link-state / DHCP monitoring loop.
     * ---------------------------------------------------------------------- */
    uint8_t was_link_up  = 0U;   /* previous physical link state             */
    uint8_t ip_reported  = 0U;   /* 1 after EVT_IP_ACQUIRED has been fired   */
    uint32_t poll_count  = 0U;   /* debug counter                            */
    uint32_t dhcp_stuck  = 0U;   /* polls in non-BOUND state while link up   */
    uint32_t dhcp_tries  = 0U;   /* total DHCP restart attempts              */
    uint8_t  last_dhcp_state = 0xFFU; /* track state changes for debug log   */

    for (;;)
    {
        osDelay(LINK_POLL_MS);
        poll_count++;

        if (netif_is_link_up(&gnetif))
        {
            /* ---- Link came up ------------------------------------------ */
            if (!was_link_up)
            {
#ifdef ETH_STATIC_IP_TEST
                /* --- Static IP test: 192.168.1.100 / 255.255.255.0 --- */
                {
                    IP4_ADDR(&s_static_args.ip, 192, 168,   1, 100);
                    IP4_ADDR(&s_static_args.nm, 255, 255, 255,   0);
                    IP4_ADDR(&s_static_args.gw, 192, 168,   1,   1);
                    s_static_args.netif = &gnetif;
                    tcpip_callback(set_static_ip_cb, &s_static_args);
                    eth_cdc_log((uint8_t *)"[ETH] STATIC IP 192.168.1.100\r\n", sizeof("[ETH] STATIC IP 192.168.1.100\r\n") - 1U);
                }
#else
                eth_cdc_log((uint8_t *)"[ETH] LINK UP - starting DHCP\r\n", sizeof("[ETH] LINK UP - starting DHCP\r\n") - 1U);
#endif
                osDelay(10);
                tcpip_callback(netif_set_up_cb, &gnetif);  /* enable L3 routing */
                was_link_up = 1U;
#ifndef ETH_STATIC_IP_TEST
                tcpip_callback(dhcp_stop_cb,  &gnetif);
                tcpip_callback(dhcp_start_cb, &gnetif);
#endif
            }

            /* ---- DHCP watchdog: restart if stuck for >10 s, fallback to static after 3 tries ---- */
            if (gnetif.ip_addr.addr == 0U)
            {
                dhcp_stuck++;
                if (dhcp_stuck >= 20U)  /* 20 * 500 ms = 10 s */
                {
                    dhcp_stuck = 0U;
                    dhcp_tries++;
                    if (dhcp_tries >= 3U)
                    {
                        dhcp_tries = 0U;
                        eth_cdc_log((uint8_t *)"[ETH] DHCP failed - using static 192.168.1.100\r\n", sizeof("[ETH] DHCP failed - using static 192.168.1.100\r\n") - 1U);
                        osDelay(10);
                        IP4_ADDR(&s_static_args.ip, 192, 168,   1, 100);
                        IP4_ADDR(&s_static_args.nm, 255, 255, 255,   0);
                        IP4_ADDR(&s_static_args.gw, 192, 168,   1,   1);
                        s_static_args.netif = &gnetif;
                        tcpip_callback(set_static_ip_cb, &s_static_args);
                    }
                    else
                    {
                        eth_cdc_log((uint8_t *)"[ETH] DHCP timeout - restarting\r\n", sizeof("[ETH] DHCP timeout - restarting\r\n") - 1U);
                        osDelay(10);
                        tcpip_callback(dhcp_stop_cb,  &gnetif);
                        tcpip_callback(dhcp_start_cb, &gnetif);
                    }
                }
            }
            else
            {
                dhcp_stuck = 0U;
            }

            /* ---- DHCP state-change log (fires immediately on each transition) ---- */
            {
                struct dhcp *dhcp_data = netif_dhcp_data(&gnetif);
                uint8_t cur_state = dhcp_data ? dhcp_data->state : 0xFFU;
                if (cur_state != last_dhcp_state)
                {
                    static const char * const dhcp_state_name[] = {
                        "OFF","REQUESTING","INIT","REBOOTING",
                        "REBINDING","RENEWING","SELECTING","INFORMING",
                        "CHECKING","PERMANENT","BOUND","RELEASING","BACKING_OFF"
                    };
                    const char *sname = (cur_state < 13U) ? dhcp_state_name[cur_state] : "?";
                    char sbuf[64];
                    int slen = snprintf(sbuf, sizeof(sbuf),
                        "[DHCP] state %u->%u (%s)\r\n",
                        (unsigned)last_dhcp_state, (unsigned)cur_state, sname);
                    if (slen > 0) { eth_cdc_log((uint8_t *)sbuf, (uint16_t)slen); }
                    last_dhcp_state = cur_state;
                    osDelay(10);
                }
            }

            /* ---- Periodic debug log every 5 s (10 polls × 500 ms) ------- */
            if ((poll_count % 10U) == 0U)
            {
                extern volatile uint32_t g_eth_rx_count;
                extern volatile uint32_t g_eth_tx_count;
                extern ETH_HandleTypeDef heth;
                extern lan8720_Object_t LAN8720;
                char dbg[96];

                /* --- PHY + SW counters --- */
                uint32_t bmsr = 0, physcsr = 0;
                HAL_ETH_ReadPHYRegister(&heth, LAN8720.DevAddr, 0x01U, &bmsr);
                HAL_ETH_ReadPHYRegister(&heth, LAN8720.DevAddr, 0x1FU, &physcsr);
                uint32_t crc_err = heth.Instance->MMCRFCECR;
                /* MMCRFAECR = RX alignment error counter (proxy for noise) */
                uint32_t align_err = heth.Instance->MMCRFAECR;
                uint32_t my_ip  = gnetif.ip_addr.addr;
                struct dhcp *dhcp_data = netif_dhcp_data(&gnetif);
                uint8_t dhcp_state = dhcp_data ? dhcp_data->state : 0xFFU;
                uint32_t dhcp_xid  = dhcp_data ? dhcp_data->xid  : 0U;
                uint32_t offer_ip  = dhcp_data ? dhcp_data->offered_ip_addr.addr : 0U;
                /* DHCP states: 0=OFF 2=INIT 6=SELECT 1=REQUEST 10=BOUND */
                int len = snprintf(dbg, sizeof(dbg),
                    "[ETH1] TX=%lu RX=%lu AE=%lu IP=%lu.%lu.%lu.%lu DHCP=%u CRC=%lu\r\n",
                    (unsigned long)g_eth_tx_count,
                    (unsigned long)g_eth_rx_count,
                    (unsigned long)align_err,
                    (unsigned long)((my_ip >>  0) & 0xFFU),
                    (unsigned long)((my_ip >>  8) & 0xFFU),
                    (unsigned long)((my_ip >> 16) & 0xFFU),
                    (unsigned long)((my_ip >> 24) & 0xFFU),
                    (unsigned)dhcp_state,
                    (unsigned long)crc_err);
                if (len > 0) { eth_cdc_log((uint8_t *)dbg, (uint16_t)len); }
                osDelay(10);

                /* --- DHCP detail: xid + offered IP ------------------------ */
                char dbg3[80];
                int len3 = snprintf(dbg3, sizeof(dbg3),
                    "[DHCP] xid=%08lX offer=%lu.%lu.%lu.%lu PHYSCSR=%04lX\r\n",
                    (unsigned long)dhcp_xid,
                    (unsigned long)((offer_ip >>  0) & 0xFFU),
                    (unsigned long)((offer_ip >>  8) & 0xFFU),
                    (unsigned long)((offer_ip >> 16) & 0xFFU),
                    (unsigned long)((offer_ip >> 24) & 0xFFU),
                    (unsigned long)physcsr);
                if (len3 > 0) { eth_cdc_log((uint8_t *)dbg3, (uint16_t)len3); }
                osDelay(10);

                /* --- MAC/DMA hardware registers ---
                 * MACCR  bit9=DM (duplex), bit14=FES (speed)
                 * DMAOMR bit26=DTCEFD
                 * MMCRGUFCR = good unicast RX frames
                 * MMCRGUFCR stays at 0 while pinging → PC not on same L2 segment
                 */
                uint32_t maccr  = heth.Instance->MACCR;
                uint32_t dmaomr = heth.Instance->DMAOMR;
                uint32_t dmasr  = heth.Instance->DMASR;
                uint32_t guf    = heth.Instance->MMCRGUFCR;
                uint32_t macffr = heth.Instance->MACFFR;
                char dbg2[128];
                int len2 = snprintf(dbg2, sizeof(dbg2),
                    "[ETH2] MACCR=%08lX DMAOMR=%08lX DMASR=%08lX GUF=%lu MACFFR=%04lX\r\n",
                    (unsigned long)maccr,
                    (unsigned long)dmaomr,
                    (unsigned long)dmasr,
                    (unsigned long)guf,
                    (unsigned long)macffr);
                if (len2 > 0) { eth_cdc_log((uint8_t *)dbg2, (uint16_t)len2); }
                osDelay(10);
            }

            /* ---- DHCP IP detection (fires only once per link session) --- */
            if (!ip_reported && (gnetif.ip_addr.addr != 0U))
            {
                /* Snapshot the three address fields under mutex. */
                if (osMutexAcquire(g_dataMutex, MUTEX_TIMEOUT_MS) == osOK)
                {
                    g_appData.ip_addr = gnetif.ip_addr.addr;
                    g_appData.ip_mask = gnetif.netmask.addr;
                    g_appData.ip_gw   = gnetif.gw.addr;
                    osMutexRelease(g_dataMutex);
                }

                /* Log acquired IP/mask/GW via CDC. */
                {
                    char iplog[80];
                    int ilen = snprintf(iplog, sizeof(iplog),
                        "[ETH] IP=%lu.%lu.%lu.%lu  GW=%lu.%lu.%lu.%lu\r\n",
                        (unsigned long)((gnetif.ip_addr.addr >>  0) & 0xFFU),
                        (unsigned long)((gnetif.ip_addr.addr >>  8) & 0xFFU),
                        (unsigned long)((gnetif.ip_addr.addr >> 16) & 0xFFU),
                        (unsigned long)((gnetif.ip_addr.addr >> 24) & 0xFFU),
                        (unsigned long)((gnetif.gw.addr >>  0) & 0xFFU),
                        (unsigned long)((gnetif.gw.addr >>  8) & 0xFFU),
                        (unsigned long)((gnetif.gw.addr >> 16) & 0xFFU),
                        (unsigned long)((gnetif.gw.addr >> 24) & 0xFFU));
                    if (ilen > 0) { eth_cdc_log((uint8_t *)iplog, (uint16_t)ilen); }
                    osDelay(10);
                }

                /* Signal all waiting tasks — set bits AFTER mutex release. */
                xEventGroupSetBits(g_systemEvents, EVT_IP_ACQUIRED);
                ip_reported = 1U;
            }
        }
        else
        {
            /* ---- Link went down ---------------------------------------- */
            if (was_link_up)
            {
                eth_cdc_log((uint8_t *)"[ETH] LINK DOWN\r\n", sizeof("[ETH] LINK DOWN\r\n") - 1U);
                osDelay(10);
                tcpip_callback(dhcp_stop_cb, &gnetif);       /* release DHCP lease / stop timers */
                tcpip_callback(netif_set_down_cb, &gnetif);  /* suspend L3 routing */
                was_link_up  = 0U;
                dhcp_stuck   = 0U;
                dhcp_tries   = 0U;

                /* Clear IP fields so stale data is not shown after reconnect. */
                if (osMutexAcquire(g_dataMutex, MUTEX_TIMEOUT_MS) == osOK)
                {
                    g_appData.ip_addr = 0U;
                    g_appData.ip_mask = 0U;
                    g_appData.ip_gw   = 0U;
                    osMutexRelease(g_dataMutex);
                }

                /* Reset flag so EVT_IP_ACQUIRED fires again after reconnect. */
                ip_reported = 0U;
            }
        }
    }
}
