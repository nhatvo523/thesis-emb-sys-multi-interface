/**
  ******************************************************************************
  * @file    lan8720.h
  * @brief   This file contains all the functions prototypes for the
  *          lan8720.c PHY driver.
  ******************************************************************************
  * LAN8720A/LAN8720AI - SMSC/Microchip 10/100 Ethernet PHY (RMII)
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef LAN8720_H
#define LAN8720_H

#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/** @defgroup LAN8720_Registers LAN8720 Register Map
  * @{
  */

/* Standard IEEE 802.3 registers */
#define LAN8720_BCR        ((uint16_t)0x0000U)  /*!< Basic Control Register */
#define LAN8720_BSR        ((uint16_t)0x0001U)  /*!< Basic Status Register */
#define LAN8720_PHYI1R     ((uint16_t)0x0002U)  /*!< PHY Identifier 1 */
#define LAN8720_PHYI2R     ((uint16_t)0x0003U)  /*!< PHY Identifier 2 */
#define LAN8720_ANAR       ((uint16_t)0x0004U)  /*!< Auto-Neg Advertisement */
#define LAN8720_ANLPAR     ((uint16_t)0x0005U)  /*!< Auto-Neg Link Partner */
#define LAN8720_ANER       ((uint16_t)0x0006U)  /*!< Auto-Neg Expansion */

/* LAN8720-specific registers */
#define LAN8720_MCSR       ((uint16_t)0x0011U)  /*!< Mode Control/Status Register */
#define LAN8720_SMR        ((uint16_t)0x0012U)  /*!< Special Modes Register */
#define LAN8720_ISR        ((uint16_t)0x001DU)  /*!< Interrupt Source Register */
#define LAN8720_IMR        ((uint16_t)0x001EU)  /*!< Interrupt Mask Register */
#define LAN8720_SCSR       ((uint16_t)0x001FU)  /*!< Special Control/Status Indication Register */

/**
  * @}
  */

/** @defgroup LAN8720_BCR_Bits Basic Control Register bits
  * @{
  */
#define LAN8720_BCR_SOFT_RESET         ((uint16_t)0x8000U)
#define LAN8720_BCR_LOOPBACK           ((uint16_t)0x4000U)
#define LAN8720_BCR_SPEED_SELECT       ((uint16_t)0x2000U)
#define LAN8720_BCR_AUTONEGO_EN        ((uint16_t)0x1000U)
#define LAN8720_BCR_POWER_DOWN         ((uint16_t)0x0800U)
#define LAN8720_BCR_ISOLATE            ((uint16_t)0x0400U)
#define LAN8720_BCR_RESTART_AUTONEGO   ((uint16_t)0x0200U)
#define LAN8720_BCR_DUPLEX_MODE        ((uint16_t)0x0100U)
/**
  * @}
  */

/** @defgroup LAN8720_BSR_Bits Basic Status Register bits
  * @{
  */
#define LAN8720_BSR_100BASE_TX_FD    ((uint16_t)0x4000U)
#define LAN8720_BSR_100BASE_TX_HD    ((uint16_t)0x2000U)
#define LAN8720_BSR_10BASE_T_FD      ((uint16_t)0x1000U)
#define LAN8720_BSR_10BASE_T_HD      ((uint16_t)0x0800U)
#define LAN8720_BSR_AUTONEGO_CPLT    ((uint16_t)0x0020U)
#define LAN8720_BSR_REMOTE_FAULT     ((uint16_t)0x0010U)
#define LAN8720_BSR_AUTONEGO_ABILITY ((uint16_t)0x0008U)
#define LAN8720_BSR_LINK_STATUS      ((uint16_t)0x0004U)
#define LAN8720_BSR_EXTENDED_CAP     ((uint16_t)0x0001U)
/**
  * @}
  */

/** @defgroup LAN8720_PHYI1R_Bits PHY Identifier 1 (OUI bits [3:18])
  * @{
  */
#define LAN8720_PHYI1R_OUI_3_18   ((uint16_t)0xFFFFU)   /*!< OUI[3:18] = 0x0007 */
/**
  * @}
  */

/** @defgroup LAN8720_PHYI2R_Bits PHY Identifier 2
  * @{
  */
#define LAN8720_PHYI2R_OUI_19_24  ((uint16_t)0xFC00U)   /*!< OUI[19:24] = 0x30 */
#define LAN8720_PHYI2R_MODEL_NBR  ((uint16_t)0x03F0U)   /*!< Model = 0x0F */
#define LAN8720_PHYI2R_REVISION   ((uint16_t)0x000FU)
/**
  * @}
  */

/** @defgroup LAN8720_ANAR_Bits Auto-Neg Advertisement bits
  * @{
  */
#define LAN8720_ANAR_100BASE_TX_FD    ((uint16_t)0x0100U)
#define LAN8720_ANAR_100BASE_TX       ((uint16_t)0x0080U)
#define LAN8720_ANAR_10BASE_T_FD      ((uint16_t)0x0040U)
#define LAN8720_ANAR_10BASE_T         ((uint16_t)0x0020U)
#define LAN8720_ANAR_SELECTOR_FIELD   ((uint16_t)0x001FU)
/**
  * @}
  */

/** @defgroup LAN8720_SMR_Bits Special Modes Register bits
  * @{
  */
#define LAN8720_SMR_MODE       ((uint16_t)0x00E0U)   /*!< PHY mode */
#define LAN8720_SMR_PHY_ADDR   ((uint16_t)0x001FU)   /*!< PHY address [4:0] */
/**
  * @}
  */

/** @defgroup LAN8720_SCSR_Bits Special Control/Status Indication Register bits
  * @{
  */
#define LAN8720_SCSR_AUTONEGO_DONE   ((uint16_t)0x1000U)  /*!< Auto-Nego complete */
#define LAN8720_SCSR_HCDSPEEDMASK    ((uint16_t)0x001CU)  /*!< Speed indicator mask [4:2] */
#define LAN8720_SCSR_10BT_HD         ((uint16_t)0x0004U)  /*!< 10Base-T Half Duplex */
#define LAN8720_SCSR_10BT_FD         ((uint16_t)0x0014U)  /*!< 10Base-T Full Duplex */
#define LAN8720_SCSR_100BTX_HD       ((uint16_t)0x0008U)  /*!< 100Base-TX Half Duplex */
#define LAN8720_SCSR_100BTX_FD       ((uint16_t)0x0018U)  /*!< 100Base-TX Full Duplex */
/**
  * @}
  */

/** @defgroup LAN8720_IMR_ISR_Bits Interrupt Mask / Source Register bits
  * @{
  */
#define LAN8720_INT_ENERGYON          ((uint16_t)0x0080U)
#define LAN8720_INT_AUTONEGO_COMPLETE ((uint16_t)0x0040U)
#define LAN8720_INT_REMOTE_FAULT      ((uint16_t)0x0020U)
#define LAN8720_INT_LINK_DOWN         ((uint16_t)0x0010U)
#define LAN8720_INT_AUTONEGO_LP_ACK   ((uint16_t)0x0008U)
#define LAN8720_INT_PARALLEL_DETECT   ((uint16_t)0x0004U)
#define LAN8720_INT_AUTONEGO_PAGE_RX  ((uint16_t)0x0002U)
/**
  * @}
  */

/** @defgroup LAN8720_Status Return codes
  * @{
  */
#define LAN8720_STATUS_READ_ERROR          ((int32_t)-5)
#define LAN8720_STATUS_WRITE_ERROR         ((int32_t)-4)
#define LAN8720_STATUS_ADDRESS_ERROR       ((int32_t)-3)
#define LAN8720_STATUS_RESET_TIMEOUT       ((int32_t)-2)
#define LAN8720_STATUS_ERROR               ((int32_t)-1)
#define LAN8720_STATUS_OK                  ((int32_t) 0)
#define LAN8720_STATUS_LINK_DOWN           ((int32_t) 1)
#define LAN8720_STATUS_100MBITS_FULLDUPLEX ((int32_t) 2)
#define LAN8720_STATUS_100MBITS_HALFDUPLEX ((int32_t) 3)
#define LAN8720_STATUS_10MBITS_FULLDUPLEX  ((int32_t) 4)
#define LAN8720_STATUS_10MBITS_HALFDUPLEX  ((int32_t) 5)
#define LAN8720_STATUS_AUTONEGO_NOTDONE    ((int32_t) 6)
/**
  * @}
  */

/** @defgroup LAN8720_PHY_Address Default PHY address
  * @{
  */
/* PHY address is determined by PHYAD0 strap (pin 10, RXER/PHYAD0).
 * In this design PHYAD0 is pulled to GND via R413 (4.7 kΩ) → address 0x00.
 * Override this define in your board configuration if needed. */
#ifndef LAN8720_PHY_ADDRESS
  #define LAN8720_PHY_ADDRESS   0x00U
#endif
/**
  * @}
  */

/* Exported types ------------------------------------------------------------*/
typedef int32_t (*lan8720_Init_Func)     (void);
typedef int32_t (*lan8720_DeInit_Func)   (void);
typedef int32_t (*lan8720_ReadReg_Func)  (uint32_t, uint32_t, uint32_t *);
typedef int32_t (*lan8720_WriteReg_Func) (uint32_t, uint32_t, uint32_t);
typedef int32_t (*lan8720_GetTick_Func)  (void);

typedef struct
{
  lan8720_Init_Func      Init;
  lan8720_DeInit_Func    DeInit;
  lan8720_WriteReg_Func  WriteReg;
  lan8720_ReadReg_Func   ReadReg;
  lan8720_GetTick_Func   GetTick;
} lan8720_IOCtx_t;

typedef struct
{
  uint32_t         DevAddr;
  uint32_t         Is_Initialized;
  lan8720_IOCtx_t  IO;
  void            *pData;
} lan8720_Object_t;

/* Exported functions --------------------------------------------------------*/
int32_t LAN8720_RegisterBusIO(lan8720_Object_t *pObj, lan8720_IOCtx_t *ioctx);
int32_t LAN8720_Init(lan8720_Object_t *pObj);
int32_t LAN8720_DeInit(lan8720_Object_t *pObj);
int32_t LAN8720_DisablePowerDownMode(lan8720_Object_t *pObj);
int32_t LAN8720_EnablePowerDownMode(lan8720_Object_t *pObj);
int32_t LAN8720_StartAutoNego(lan8720_Object_t *pObj);
int32_t LAN8720_GetLinkState(lan8720_Object_t *pObj);
int32_t LAN8720_SetLinkState(lan8720_Object_t *pObj, uint32_t LinkState);
int32_t LAN8720_EnableLoopbackMode(lan8720_Object_t *pObj);
int32_t LAN8720_DisableLoopbackMode(lan8720_Object_t *pObj);
int32_t LAN8720_EnableIT(lan8720_Object_t *pObj, uint32_t Interrupt);
int32_t LAN8720_DisableIT(lan8720_Object_t *pObj, uint32_t Interrupt);
int32_t LAN8720_ClearIT(lan8720_Object_t *pObj, uint32_t Interrupt);
int32_t LAN8720_GetITStatus(lan8720_Object_t *pObj, uint32_t Interrupt);

#ifdef __cplusplus
}
#endif
#endif /* LAN8720_H */
