/**
 * @file sd_glue.c
 * FatFs block access to the XTSD flash using SDMMC DMA (IDMA).
 *
 * The CubeMX FatFs glue (bsp_driver_sd.c) uses *polling* reads/writes by
 * default, but USB mass storage on this board uses DMA, which is known to
 * work. These strong definitions replace the __weak polling versions so
 * FatFs uses the same DMA path. This file isn't generated, so CubeMX won't
 * overwrite it.
 *
 * SDMMC IDMA needs 4-byte aligned buffers; unaligned FatFs buffers go
 * through a scratch sector one block at a time.
 */
#include "bsp_driver_sd.h"
#include <string.h>

extern SD_HandleTypeDef hsd2;

#define SD_IO_TIMEOUT_MS   3000U

static uint32_t s_scratch[512 / 4];
static uint32_t s_last_error;

uint32_t SD_Glue_LastError(void) { return s_last_error; }

static uint8_t wait_done(void)
{
    uint32_t t0 = HAL_GetTick();

    /* Transfer: HAL sets state back to READY in the SDMMC interrupt */
    while (HAL_SD_GetState(&hsd2) == HAL_SD_STATE_BUSY) {
        if (HAL_GetTick() - t0 > SD_IO_TIMEOUT_MS) {
            HAL_SD_Abort(&hsd2);
            s_last_error = 0xFFFFFFFFU;          /* timeout */
            return MSD_ERROR;
        }
    }
    if (hsd2.ErrorCode != HAL_SD_ERROR_NONE) {
        s_last_error = hsd2.ErrorCode;
        return MSD_ERROR;
    }

    /* Card finishes programming / goes back to TRANSFER state */
    t0 = HAL_GetTick();
    while (HAL_SD_GetCardState(&hsd2) != HAL_SD_CARD_TRANSFER) {
        if (HAL_GetTick() - t0 > SD_IO_TIMEOUT_MS) {
            s_last_error = 0xFFFFFFFEU;          /* card stuck busy */
            return MSD_ERROR;
        }
    }
    return MSD_OK;
}

static uint8_t read_dma(uint8_t *dst, uint32_t addr, uint32_t n)
{
    if (HAL_SD_ReadBlocks_DMA(&hsd2, dst, addr, n) != HAL_OK) {
        s_last_error = hsd2.ErrorCode ? hsd2.ErrorCode : 0xFFFFFFFDU;
        return MSD_ERROR;
    }
    return wait_done();
}

static uint8_t write_dma(const uint8_t *src, uint32_t addr, uint32_t n)
{
    if (HAL_SD_WriteBlocks_DMA(&hsd2, (uint8_t *)src, addr, n) != HAL_OK) {
        s_last_error = hsd2.ErrorCode ? hsd2.ErrorCode : 0xFFFFFFFDU;
        return MSD_ERROR;
    }
    return wait_done();
}

uint8_t BSP_SD_ReadBlocks(uint32_t *pData, uint32_t ReadAddr, uint32_t NumOfBlocks, uint32_t Timeout)
{
    (void)Timeout;
    uint8_t *p = (uint8_t *)pData;

    if (((uint32_t)p & 3U) == 0U) return read_dma(p, ReadAddr, NumOfBlocks);

    for (uint32_t i = 0; i < NumOfBlocks; i++) {
        if (read_dma((uint8_t *)s_scratch, ReadAddr + i, 1) != MSD_OK) return MSD_ERROR;
        memcpy(p + i * 512U, s_scratch, 512);
    }
    return MSD_OK;
}

uint8_t BSP_SD_WriteBlocks(uint32_t *pData, uint32_t WriteAddr, uint32_t NumOfBlocks, uint32_t Timeout)
{
    (void)Timeout;
    const uint8_t *p = (const uint8_t *)pData;

    if (((uint32_t)p & 3U) == 0U) return write_dma(p, WriteAddr, NumOfBlocks);

    for (uint32_t i = 0; i < NumOfBlocks; i++) {
        memcpy(s_scratch, p + i * 512U, 512);
        if (write_dma((const uint8_t *)s_scratch, WriteAddr + i, 1) != MSD_OK) return MSD_ERROR;
    }
    return MSD_OK;
}

/* Raw check used by the "-sd" serial command: reads sector 0 and reports
 * what FatFs will see. */
uint8_t SD_Glue_ReadSector0(uint8_t out[512])
{
    return BSP_SD_ReadBlocks((uint32_t *)s_scratch, 0, 1, 0) == MSD_OK
           ? (memcpy(out, s_scratch, 512), MSD_OK) : MSD_ERROR;
}
