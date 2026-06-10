/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : Target/lwipopts.h
  * Description        : This file overrides LwIP stack default configuration
  *                      done in opt.h file.
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

/* Define to prevent recursive inclusion --------------------------------------*/
#ifndef __LWIPOPTS__H__
#define __LWIPOPTS__H__

#include "main.h"

/*-----------------------------------------------------------------------------*/
/* Current version of LwIP supported by CubeMx: 2.1.2 -*/
/*-----------------------------------------------------------------------------*/

/* Within 'USER CODE' section, code will be kept by default at each generation */
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

#ifdef __cplusplus
 extern "C" {
#endif

/* STM32CubeMX Specific Parameters (not defined in opt.h) ---------------------*/
/* Parameters set in STM32CubeMX LwIP Configuration GUI -*/
/*----- WITH_RTOS enabled (Since FREERTOS is set) -----*/
#define WITH_RTOS 1
/* Temporary workaround to avoid conflict on errno defined in STM32CubeIDE and lwip sys_arch.c errno */
#undef LWIP_PROVIDE_ERRNO
/*----- CHECKSUM_BY_HARDWARE enabled -----*/
#define CHECKSUM_BY_HARDWARE 1
/*-----------------------------------------------------------------------------*/

/* LwIP Stack Parameters (modified compared to initialization value in opt.h) -*/
/* Parameters set in STM32CubeMX LwIP Configuration GUI -*/
/*----- Value in opt.h for LWIP_DHCP: 0 -----*/
#define LWIP_DHCP 1
/*----- Value in opt.h for MEM_ALIGNMENT: 1 -----*/
#define MEM_ALIGNMENT 4
/*----- Default Value for MEM_SIZE: 1600 ---*/
#define MEM_SIZE 10240
/*----- Default Value for MEMP_NUM_RAW_PCB: 4 ---*/
#define MEMP_NUM_RAW_PCB 5
/*----- Default Value for MEMP_NUM_TCP_SEG: 16 ---*/
#define MEMP_NUM_TCP_SEG 21
/*----- Value in opt.h for MEMP_NUM_SYS_TIMEOUT: (LWIP_TCP + IP_REASSEMBLY + LWIP_ARP + (2*LWIP_DHCP) + LWIP_AUTOIP + LWIP_IGMP + LWIP_DNS + (PPP_SUPPORT*6*MEMP_NUM_PPP_PCB) + (LWIP_IPV6 ? (1 + LWIP_IPV6_REASS + LWIP_IPV6_MLD) : 0)) -*/
/* H1: Formula = TCP(1)+REASS(1)+ARP(1)+DHCP×2(2)+AUTOIP(1)+HTTPD_keepalive(4) = 10 minimum.
 * Set to 12 for headroom; pool exhaustion silently prevents DHCP retry timers. */
#define MEMP_NUM_SYS_TIMEOUT 12
/*----- Value in opt.h for LWIP_ETHERNET: LWIP_ARP || PPPOE_SUPPORT -*/
#define LWIP_ETHERNET 1
/*----- Value in opt.h for LWIP_DNS_SECURE: (LWIP_DNS_SECURE_RAND_XID | LWIP_DNS_SECURE_NO_MULTIPLE_OUTSTANDING | LWIP_DNS_SECURE_RAND_SRC_PORT) -*/
#define LWIP_DNS_SECURE 7
/*----- Default Value for TCP_WND: 2144 ---*/
#define TCP_WND 5840
/*----- Default Value for TCP_SND_BUF: 1072 ---*/
#define TCP_SND_BUF 2684
/*----- Default Value for LWIP_NETIF_STATUS_CALLBACK: 0 ---*/
#define LWIP_NETIF_STATUS_CALLBACK 1
/*----- Value in opt.h for LWIP_NETIF_LINK_CALLBACK: 0 -----*/
#define LWIP_NETIF_LINK_CALLBACK 1
/*----- Value in opt.h for TCPIP_THREAD_STACKSIZE: 0 -----*/
/* H2: tcpip_thread handles DHCP state machine + ARP + HTTP + SSE callbacks.
 * 1024 words (4 KB) risks stack overflow; 2048 words (8 KB) provides safe margin. */
#define TCPIP_THREAD_STACKSIZE 2048
/*----- Value in opt.h for TCPIP_THREAD_PRIO: 1 -----*/
#define TCPIP_THREAD_PRIO 24
/*----- Value in opt.h for TCPIP_MBOX_SIZE: 0 -----*/
/* M1: HTTP keepalive + SSE push + DHCP retries post to tcpip_mbox concurrently.
 * Size 6 causes silent packet drops under load; 16 gives adequate headroom. */
#define TCPIP_MBOX_SIZE 16
/*----- Value in opt.h for SLIPIF_THREAD_STACKSIZE: 0 -----*/
#define SLIPIF_THREAD_STACKSIZE 1024
/*----- Value in opt.h for SLIPIF_THREAD_PRIO: 1 -----*/
#define SLIPIF_THREAD_PRIO 3
/*----- Value in opt.h for DEFAULT_THREAD_STACKSIZE: 0 -----*/
#define DEFAULT_THREAD_STACKSIZE 1024
/*----- Value in opt.h for DEFAULT_THREAD_PRIO: 1 -----*/
#define DEFAULT_THREAD_PRIO 3
/*----- Value in opt.h for DEFAULT_UDP_RECVMBOX_SIZE: 0 -----*/
#define DEFAULT_UDP_RECVMBOX_SIZE 6
/*----- Value in opt.h for DEFAULT_TCP_RECVMBOX_SIZE: 0 -----*/
#define DEFAULT_TCP_RECVMBOX_SIZE 6
/*----- Value in opt.h for DEFAULT_ACCEPTMBOX_SIZE: 0 -----*/
#define DEFAULT_ACCEPTMBOX_SIZE 6
/*----- Value in opt.h for RECV_BUFSIZE_DEFAULT: INT_MAX -----*/
#define RECV_BUFSIZE_DEFAULT 2000000000
/*----- Default Value for LWIP_HTTPD: 0 ---*/
#define LWIP_HTTPD 1
/*----- Default Value for LWIP_HTTPD_CGI: 0 ---*/
#define LWIP_HTTPD_CGI 1
/*----- Default Value for LWIP_HTTPD_SSI: 0 ---*/
#define LWIP_HTTPD_SSI 1
/*----- Default Value for LWIP_HTTPD_SUPPORT_POST: 0 ---*/
#define LWIP_HTTPD_SUPPORT_POST 1
/*----- Default Value for LWIP_HTTPD_DYNAMIC_HEADERS: 0 ---*/
#define LWIP_HTTPD_DYNAMIC_HEADERS 1
/*----- Default Value for LWIP_HTTPD_SUPPORT_11_KEEPALIVE: 0 ---*/
#define LWIP_HTTPD_SUPPORT_11_KEEPALIVE 1
/*----- Default Value for LWIP_HTTPD_SSI_INCLUDE_TAG: 1 ---*/
#define LWIP_HTTPD_SSI_INCLUDE_TAG 0
/*----- Default Value for LWIP_HTTPD_KILL_OLD_ON_CONNECTIONS_EXCEEDED: 0 ---*/
#define LWIP_HTTPD_KILL_OLD_ON_CONNECTIONS_EXCEEDED 1
/*----- Value in opt.h for LWIP_STATS: 1 -----*/
#define LWIP_STATS 0
/*----- Value in opt.h for CHECKSUM_GEN_IP: 1 -----*/
#define CHECKSUM_GEN_IP 0
/*----- Value in opt.h for CHECKSUM_GEN_UDP: 1 -----*/
#define CHECKSUM_GEN_UDP 0
/*----- Value in opt.h for CHECKSUM_GEN_TCP: 1 -----*/
#define CHECKSUM_GEN_TCP 0
/*----- Value in opt.h for CHECKSUM_GEN_ICMP: 1 -----*/
#define CHECKSUM_GEN_ICMP 0
/*----- Value in opt.h for CHECKSUM_GEN_ICMP6: 1 -----*/
#define CHECKSUM_GEN_ICMP6 0
/*----- Value in opt.h for CHECKSUM_CHECK_IP: 1 -----*/
#define CHECKSUM_CHECK_IP 0
/*----- Value in opt.h for CHECKSUM_CHECK_UDP: 1 -----*/
#define CHECKSUM_CHECK_UDP 0
/*----- Value in opt.h for CHECKSUM_CHECK_TCP: 1 -----*/
#define CHECKSUM_CHECK_TCP 0
/*----- Value in opt.h for CHECKSUM_CHECK_ICMP: 1 -----*/
#define CHECKSUM_CHECK_ICMP 0
/*----- Value in opt.h for CHECKSUM_CHECK_ICMP6: 1 -----*/
#define CHECKSUM_CHECK_ICMP6 0
/*-----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* Enable the custom filesystem hook (fs_open_custom / fs_close_custom).
 * Required by web_server.c to serve /status.json dynamically from RAM
 * without a file on the ROM filesystem (fsdata.c).                         */
#define LWIP_HTTPD_CUSTOM_FILES       1

/* Increase pbuf pool from default 4 to 12.
 * LwIP sanity check requires: PBUF_POOL_SIZE * (PBUF_POOL_BUFSIZE - 54) >= TCP_WND
 * With TCP_WND=5840 and PBUF_POOL_BUFSIZE=592: need >= ceil(5840/538) = 11.
 * Set to 12 for headroom. Also prevents silent drops of ARP/DHCP OFFER frames
 * when HTTP + SSE + DHCP run concurrently. */
#define PBUF_POOL_SIZE                12

/* USER CODE END 1 */

#ifdef __cplusplus
}
#endif
#endif /*__LWIPOPTS__H__ */
