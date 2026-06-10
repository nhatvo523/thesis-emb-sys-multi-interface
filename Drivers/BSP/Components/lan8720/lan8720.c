/**
  ******************************************************************************
  * @file    lan8720.c
  * @brief   This file provides a set of functions needed to manage the LAN8720
  *          PHY device (SMSC/Microchip 10/100 Ethernet PHY, RMII interface).
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "lan8720.h"

/* Private defines -----------------------------------------------------------*/
#define LAN8720_MAX_DEV_ADDR   ((uint32_t)31U)

/* Private functions ---------------------------------------------------------*/

/**
  * @brief  Register IO functions to component object
  * @param  pObj: device object of lan8720_Object_t.
  * @param  ioctx: holds device IO functions.
  * @retval LAN8720_STATUS_OK  if OK
  *         LAN8720_STATUS_ERROR if missing mandatory function
  */
int32_t LAN8720_RegisterBusIO(lan8720_Object_t *pObj, lan8720_IOCtx_t *ioctx)
{
  if (!pObj || !ioctx->ReadReg || !ioctx->WriteReg || !ioctx->GetTick)
  {
    return LAN8720_STATUS_ERROR;
  }

  pObj->IO.Init     = ioctx->Init;
  pObj->IO.DeInit   = ioctx->DeInit;
  pObj->IO.ReadReg  = ioctx->ReadReg;
  pObj->IO.WriteReg = ioctx->WriteReg;
  pObj->IO.GetTick  = ioctx->GetTick;

  return LAN8720_STATUS_OK;
}

/**
  * @brief  Initialize the LAN8720 and configure the needed hardware resources.
  *         Scans MDIO addresses 0..31 using the Special Modes Register (0x12)
  *         whose bits[4:0] contain the configured PHY address.
  * @param  pObj: device object lan8720_Object_t.
  * @retval LAN8720_STATUS_OK            if found
  *         LAN8720_STATUS_ADDRESS_ERROR if no device found
  *         LAN8720_STATUS_READ_ERROR    if MDIO read fails on all addresses
  */
int32_t LAN8720_Init(lan8720_Object_t *pObj)
{
  uint32_t regvalue = 0;
  uint32_t addr     = 0;
  int32_t  status   = LAN8720_STATUS_OK;

  if (pObj->Is_Initialized == 0)
  {
    if (pObj->IO.Init != 0)
    {
      pObj->IO.Init();
    }

    pObj->DevAddr = LAN8720_MAX_DEV_ADDR + 1U;

    /* Scan all MDIO addresses; LAN8720 SMR bits[4:0] == configured PHYAD */
    for (addr = 0U; addr <= LAN8720_MAX_DEV_ADDR; addr++)
    {
      if (pObj->IO.ReadReg(addr, LAN8720_SMR, &regvalue) < 0)
      {
        status = LAN8720_STATUS_READ_ERROR;
        continue;
      }

      if ((regvalue & LAN8720_SMR_PHY_ADDR) == addr)
      {
        pObj->DevAddr = addr;
        status = LAN8720_STATUS_OK;
        break;
      }
    }

    if (pObj->DevAddr > LAN8720_MAX_DEV_ADDR)
    {
      /* Fallback: if SMR scan failed, probe LAN8720_PHY_ADDRESS directly.
       * LAN8720AI default strap has PHYAD0 pulled high → address 1. */
      if (pObj->IO.ReadReg(LAN8720_PHY_ADDRESS, LAN8720_BSR, &regvalue) >= 0)
      {
        pObj->DevAddr = LAN8720_PHY_ADDRESS;
        status = LAN8720_STATUS_OK;
      }
      else
      {
        status = LAN8720_STATUS_ADDRESS_ERROR;
      }
    }

    if (status == LAN8720_STATUS_OK)
    {
      pObj->Is_Initialized = 1U;
    }
  }

  return status;
}

/**
  * @brief  De-Initialize the LAN8720 and its hardware resources.
  * @param  pObj: device object lan8720_Object_t.
  * @retval LAN8720_STATUS_OK or LAN8720_STATUS_ERROR
  */
int32_t LAN8720_DeInit(lan8720_Object_t *pObj)
{
  if (pObj->Is_Initialized)
  {
    if (pObj->IO.DeInit != 0)
    {
      if (pObj->IO.DeInit() < 0)
      {
        /* Clear flag so a subsequent Init() can retry cleanly. */
        pObj->Is_Initialized = 0U;
        return LAN8720_STATUS_ERROR;
      }
    }
    pObj->Is_Initialized = 0U;
  }
  return LAN8720_STATUS_OK;
}

/**
  * @brief  Disable the LAN8720 power-down mode.
  * @param  pObj: device object lan8720_Object_t.
  * @retval LAN8720_STATUS_OK / LAN8720_STATUS_READ_ERROR / LAN8720_STATUS_WRITE_ERROR
  */
int32_t LAN8720_DisablePowerDownMode(lan8720_Object_t *pObj)
{
  uint32_t readval = 0;
  int32_t  status  = LAN8720_STATUS_OK;

  if (pObj->IO.ReadReg(pObj->DevAddr, LAN8720_BCR, &readval) >= 0)
  {
    readval &= ~LAN8720_BCR_POWER_DOWN;
    if (pObj->IO.WriteReg(pObj->DevAddr, LAN8720_BCR, readval) < 0)
    {
      status = LAN8720_STATUS_WRITE_ERROR;
    }
  }
  else
  {
    status = LAN8720_STATUS_READ_ERROR;
  }
  return status;
}

/**
  * @brief  Enable the LAN8720 power-down mode.
  * @param  pObj: device object lan8720_Object_t.
  * @retval LAN8720_STATUS_OK / LAN8720_STATUS_READ_ERROR / LAN8720_STATUS_WRITE_ERROR
  */
int32_t LAN8720_EnablePowerDownMode(lan8720_Object_t *pObj)
{
  uint32_t readval = 0;
  int32_t  status  = LAN8720_STATUS_OK;

  if (pObj->IO.ReadReg(pObj->DevAddr, LAN8720_BCR, &readval) >= 0)
  {
    readval |= LAN8720_BCR_POWER_DOWN;
    if (pObj->IO.WriteReg(pObj->DevAddr, LAN8720_BCR, readval) < 0)
    {
      status = LAN8720_STATUS_WRITE_ERROR;
    }
  }
  else
  {
    status = LAN8720_STATUS_READ_ERROR;
  }
  return status;
}

/**
  * @brief  Start the auto-negotiation process.
  * @param  pObj: device object lan8720_Object_t.
  * @retval LAN8720_STATUS_OK / LAN8720_STATUS_READ_ERROR / LAN8720_STATUS_WRITE_ERROR
  */
int32_t LAN8720_StartAutoNego(lan8720_Object_t *pObj)
{
  uint32_t readval = 0;
  int32_t  status  = LAN8720_STATUS_OK;

  if (pObj->IO.ReadReg(pObj->DevAddr, LAN8720_BCR, &readval) >= 0)
  {
    readval |= LAN8720_BCR_AUTONEGO_EN;
    if (pObj->IO.WriteReg(pObj->DevAddr, LAN8720_BCR, readval) < 0)
    {
      status = LAN8720_STATUS_WRITE_ERROR;
    }
  }
  else
  {
    status = LAN8720_STATUS_READ_ERROR;
  }
  return status;
}

/**
  * @brief  Get the link state of the LAN8720 device.
  *         Uses SCSR register (0x1F) speed/duplex indication bits.
  * @param  pObj: Pointer to device object.
  * @retval LAN8720_STATUS_LINK_DOWN            - no link
  *         LAN8720_STATUS_AUTONEGO_NOTDONE     - auto-nego in progress
  *         LAN8720_STATUS_100MBITS_FULLDUPLEX  - 100 Mb/s FD
  *         LAN8720_STATUS_100MBITS_HALFDUPLEX  - 100 Mb/s HD
  *         LAN8720_STATUS_10MBITS_FULLDUPLEX   - 10 Mb/s FD
  *         LAN8720_STATUS_10MBITS_HALFDUPLEX   - 10 Mb/s HD
  *         LAN8720_STATUS_READ_ERROR           - MDIO read failure
  */
int32_t LAN8720_GetLinkState(lan8720_Object_t *pObj)
{
  uint32_t readval = 0;

  /* Read BSR twice (first read may be a latched value) */
  if (pObj->IO.ReadReg(pObj->DevAddr, LAN8720_BSR, &readval) < 0)
  {
    return LAN8720_STATUS_READ_ERROR;
  }
  if (pObj->IO.ReadReg(pObj->DevAddr, LAN8720_BSR, &readval) < 0)
  {
    return LAN8720_STATUS_READ_ERROR;
  }

  if ((readval & LAN8720_BSR_LINK_STATUS) == 0U)
  {
    return LAN8720_STATUS_LINK_DOWN;
  }

  /* Check BCR to see if auto-nego is enabled */
  if (pObj->IO.ReadReg(pObj->DevAddr, LAN8720_BCR, &readval) < 0)
  {
    return LAN8720_STATUS_READ_ERROR;
  }

  if ((readval & LAN8720_BCR_AUTONEGO_EN) != LAN8720_BCR_AUTONEGO_EN)
  {
    /* Manual mode: read speed/duplex from BCR */
    if (((readval & LAN8720_BCR_SPEED_SELECT) != 0U) &&
        ((readval & LAN8720_BCR_DUPLEX_MODE)  != 0U))
    {
      return LAN8720_STATUS_100MBITS_FULLDUPLEX;
    }
    else if ((readval & LAN8720_BCR_SPEED_SELECT) != 0U)
    {
      return LAN8720_STATUS_100MBITS_HALFDUPLEX;
    }
    else if ((readval & LAN8720_BCR_DUPLEX_MODE) != 0U)
    {
      return LAN8720_STATUS_10MBITS_FULLDUPLEX;
    }
    else
    {
      return LAN8720_STATUS_10MBITS_HALFDUPLEX;
    }
  }

  /* Auto-nego enabled: read result from SCSR (register 0x1F) */
  if (pObj->IO.ReadReg(pObj->DevAddr, LAN8720_SCSR, &readval) < 0)
  {
    return LAN8720_STATUS_READ_ERROR;
  }

  if ((readval & LAN8720_SCSR_AUTONEGO_DONE) == 0U)
  {
    return LAN8720_STATUS_AUTONEGO_NOTDONE;
  }

  switch (readval & LAN8720_SCSR_HCDSPEEDMASK)
  {
    case LAN8720_SCSR_100BTX_FD:
      return LAN8720_STATUS_100MBITS_FULLDUPLEX;

    case LAN8720_SCSR_100BTX_HD:
      return LAN8720_STATUS_100MBITS_HALFDUPLEX;

    case LAN8720_SCSR_10BT_FD:
      return LAN8720_STATUS_10MBITS_FULLDUPLEX;

    default:
      return LAN8720_STATUS_10MBITS_HALFDUPLEX;
  }
}

/**
  * @brief  Manually set the link state (disable auto-nego).
  * @param  pObj: Pointer to device object.
  * @param  LinkState: one of LAN8720_STATUS_100MBITS_FULLDUPLEX, etc.
  * @retval LAN8720_STATUS_OK / LAN8720_STATUS_ERROR /
  *         LAN8720_STATUS_READ_ERROR / LAN8720_STATUS_WRITE_ERROR
  */
int32_t LAN8720_SetLinkState(lan8720_Object_t *pObj, uint32_t LinkState)
{
  uint32_t bcrvalue = 0;

  if (pObj->IO.ReadReg(pObj->DevAddr, LAN8720_BCR, &bcrvalue) < 0)
  {
    return LAN8720_STATUS_READ_ERROR;
  }

  bcrvalue &= ~(LAN8720_BCR_AUTONEGO_EN | LAN8720_BCR_SPEED_SELECT | LAN8720_BCR_DUPLEX_MODE);

  switch (LinkState)
  {
    case LAN8720_STATUS_100MBITS_FULLDUPLEX:
      bcrvalue |= (LAN8720_BCR_SPEED_SELECT | LAN8720_BCR_DUPLEX_MODE);
      break;
    case LAN8720_STATUS_100MBITS_HALFDUPLEX:
      bcrvalue |= LAN8720_BCR_SPEED_SELECT;
      break;
    case LAN8720_STATUS_10MBITS_FULLDUPLEX:
      bcrvalue |= LAN8720_BCR_DUPLEX_MODE;
      break;
    case LAN8720_STATUS_10MBITS_HALFDUPLEX:
      /* No speed/duplex bits set → 10HD */
      break;
    default:
      return LAN8720_STATUS_ERROR;
  }

  /* Only reached for valid LinkState values */
  if (pObj->IO.WriteReg(pObj->DevAddr, LAN8720_BCR, bcrvalue) < 0)
  {
    return LAN8720_STATUS_WRITE_ERROR;
  }
  return LAN8720_STATUS_OK;
}

/**
  * @brief  Enable loopback mode.
  * @param  pObj: Pointer to device object.
  * @retval LAN8720_STATUS_OK / LAN8720_STATUS_READ_ERROR / LAN8720_STATUS_WRITE_ERROR
  */
int32_t LAN8720_EnableLoopbackMode(lan8720_Object_t *pObj)
{
  uint32_t readval = 0;
  int32_t  status  = LAN8720_STATUS_OK;

  if (pObj->IO.ReadReg(pObj->DevAddr, LAN8720_BCR, &readval) >= 0)
  {
    readval |= LAN8720_BCR_LOOPBACK;
    if (pObj->IO.WriteReg(pObj->DevAddr, LAN8720_BCR, readval) < 0)
    {
      status = LAN8720_STATUS_WRITE_ERROR;
    }
  }
  else
  {
    status = LAN8720_STATUS_READ_ERROR;
  }
  return status;
}

/**
  * @brief  Disable loopback mode.
  * @param  pObj: Pointer to device object.
  * @retval LAN8720_STATUS_OK / LAN8720_STATUS_READ_ERROR / LAN8720_STATUS_WRITE_ERROR
  */
int32_t LAN8720_DisableLoopbackMode(lan8720_Object_t *pObj)
{
  uint32_t readval = 0;
  int32_t  status  = LAN8720_STATUS_OK;

  if (pObj->IO.ReadReg(pObj->DevAddr, LAN8720_BCR, &readval) >= 0)
  {
    readval &= ~LAN8720_BCR_LOOPBACK;
    if (pObj->IO.WriteReg(pObj->DevAddr, LAN8720_BCR, readval) < 0)
    {
      status = LAN8720_STATUS_WRITE_ERROR;
    }
  }
  else
  {
    status = LAN8720_STATUS_READ_ERROR;
  }
  return status;
}

/**
  * @brief  Enable interrupt source(s).
  * @param  pObj: Pointer to device object.
  * @param  Interrupt: OR-mask of LAN8720_INT_* bits.
  * @retval LAN8720_STATUS_OK / LAN8720_STATUS_READ_ERROR / LAN8720_STATUS_WRITE_ERROR
  */
int32_t LAN8720_EnableIT(lan8720_Object_t *pObj, uint32_t Interrupt)
{
  uint32_t readval = 0;
  int32_t  status  = LAN8720_STATUS_OK;

  if (pObj->IO.ReadReg(pObj->DevAddr, LAN8720_IMR, &readval) >= 0)
  {
    readval |= Interrupt;
    if (pObj->IO.WriteReg(pObj->DevAddr, LAN8720_IMR, readval) < 0)
    {
      status = LAN8720_STATUS_WRITE_ERROR;
    }
  }
  else
  {
    status = LAN8720_STATUS_READ_ERROR;
  }
  return status;
}

/**
  * @brief  Disable interrupt source(s).
  * @param  pObj: Pointer to device object.
  * @param  Interrupt: OR-mask of LAN8720_INT_* bits.
  * @retval LAN8720_STATUS_OK / LAN8720_STATUS_READ_ERROR / LAN8720_STATUS_WRITE_ERROR
  */
int32_t LAN8720_DisableIT(lan8720_Object_t *pObj, uint32_t Interrupt)
{
  uint32_t readval = 0;
  int32_t  status  = LAN8720_STATUS_OK;

  if (pObj->IO.ReadReg(pObj->DevAddr, LAN8720_IMR, &readval) >= 0)
  {
    readval &= ~Interrupt;
    if (pObj->IO.WriteReg(pObj->DevAddr, LAN8720_IMR, readval) < 0)
    {
      status = LAN8720_STATUS_WRITE_ERROR;
    }
  }
  else
  {
    status = LAN8720_STATUS_READ_ERROR;
  }
  return status;
}

/**
  * @brief  Clear interrupt flags by reading the ISR (read-to-clear).
  * @param  pObj: Pointer to device object.
  * @param  Interrupt: unused – ISR is cleared by reading.
  * @retval LAN8720_STATUS_OK / LAN8720_STATUS_READ_ERROR
  */
int32_t LAN8720_ClearIT(lan8720_Object_t *pObj, uint32_t Interrupt)
{
  uint32_t readval = 0;
  (void)Interrupt;

  if (pObj->IO.ReadReg(pObj->DevAddr, LAN8720_ISR, &readval) < 0)
  {
    return LAN8720_STATUS_READ_ERROR;
  }
  return LAN8720_STATUS_OK;
}

/**
  * @brief  Get interrupt status flags.
  * @param  pObj: Pointer to device object.
  * @param  Interrupt: OR-mask of LAN8720_INT_* bits to check.
  * @retval 0 if none of the bits are set, non-zero otherwise.
  *         LAN8720_STATUS_READ_ERROR on MDIO failure.
  */
int32_t LAN8720_GetITStatus(lan8720_Object_t *pObj, uint32_t Interrupt)
{
  uint32_t readval = 0;

  if (pObj->IO.ReadReg(pObj->DevAddr, LAN8720_ISR, &readval) < 0)
  {
    return LAN8720_STATUS_READ_ERROR;
  }
  return (int32_t)(readval & Interrupt);
}
