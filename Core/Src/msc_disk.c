/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "bsp/board_api.h"
#include "sdmmc.h"
#include "stm32l4p5xx.h"
#include "tusb.h"
extern SD_HandleTypeDef hsd2;


// Flags to track DMA completion
volatile bool dma_rx_done = false;
volatile bool dma_tx_done = false;
// Track if the hardware is currently busy
static bool is_reading = false;
static bool is_writing = false;


void HAL_SD_RxCpltCallback(SD_HandleTypeDef *hsd2) {
    if (hsd2->Instance == SDMMC2) { // Or whichever SDMMC you are using
        dma_rx_done = true;
    }
}

void HAL_SD_TxCpltCallback(SD_HandleTypeDef *hsd2) {
    if (hsd2->Instance == SDMMC2) {
        dma_tx_done = true;
    }
}



#if CFG_TUD_MSC

// whether host does safe-eject
static bool ejected = false;

// Some MCU doesn't have enough 8KB SRAM to store the whole disk
// We will use Flash as read-only disk with board that has
// CFG_EXAMPLE_MSC_READONLY defined

#define README_CONTENTS \
"This is tinyusb's MassStorage Class demo.\r\n\r\n\
If you find any bugs or get any questions, feel free to file an\r\n\
issue at github.com/hathach/tinyusb"

enum {
  DISK_BLOCK_NUM = 16,// 8KB is the smallest size that windows allow to mount
  DISK_BLOCK_SIZE = 512
};



// Invoked when received SCSI_CMD_INQUIRY, v2 with full inquiry response
// Some inquiry_resp's fields are already filled with default values, application can update them
// Return length of inquiry response, typically sizeof(scsi_inquiry_resp_t) (36 bytes), can be longer if included vendor data.
uint32_t tud_msc_inquiry2_cb(uint8_t lun, scsi_inquiry_resp_t *inquiry_resp, uint32_t bufsize) {
  (void) lun;
  (void) bufsize;
  const char vid[] = "TinyUSB";
  const char pid[] = "Mass Storage";
  const char rev[] = "1.0";

  (void) strncpy((char*) inquiry_resp->vendor_id, vid, 8);
  (void) strncpy((char*) inquiry_resp->product_id, pid, 16);
  (void) strncpy((char*) inquiry_resp->product_rev, rev, 4);

  return sizeof(scsi_inquiry_resp_t); // 36 bytes
}

// Invoked when received Test Unit Ready command.
// return true allowing host to read/write this LUN e.g SD card inserted
bool tud_msc_test_unit_ready_cb(uint8_t lun) {
  (void) lun;

  // RAM disk is ready until ejected
  if (ejected) {
    // Additional Sense 3A-00 is NOT_FOUND
    return tud_msc_set_sense(lun, SCSI_SENSE_NOT_READY, 0x3a, 0x00);
  }

  return true;
}

// Invoked when received SCSI_CMD_READ_CAPACITY_10 and SCSI_CMD_READ_FORMAT_CAPACITY to determine the disk size
// Application update block count and block size
void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count, uint16_t *block_size) {
  (void) lun;
    HAL_SD_CardInfoTypeDef CardInfo;

    // Ask the SD card for its size and block info
    if (HAL_SD_GetCardInfo(&hsd2, &CardInfo) == HAL_OK) {
        *block_count = CardInfo.BlockNbr;
        *block_size  = CardInfo.BlockSize; // Almost always 512
    } else {
        // Fallback if the card isn't reading properly
        *block_count = 0;
        *block_size  = 512;
    }
}

// Invoked when received Start Stop Unit command
// - Start = 0 : stopped power mode, if load_eject = 1 : unload disk storage
// - Start = 1 : active mode, if load_eject = 1 : load disk storage
bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
  (void) lun;
  (void) power_condition;

  if (load_eject) {
    if (start) {
      // load disk storage
    } else {
      // unload disk storage
      ejected = true;
    }
  }

  return true;
}

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and return number of copied bytes.
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
(void) lun;
    (void) offset; 

    // If we haven't started reading yet, trigger the DMA
    if (!is_reading) {
        uint32_t block_count = bufsize / 512;
        
        dma_rx_done = false;
        is_reading = true; // Lock the state

        if (HAL_SD_ReadBlocks_DMA(&hsd2, (uint8_t*)buffer, lba, block_count) != HAL_OK) {
            is_reading = false; // Unlock on error
            return -1; 
        }

        // Return 0 tells TinyUSB: "I am busy, ask me again next loop"
        return 0;
    }

    // If we are already reading, check if the DMA interrupt flipped the flag
    if (dma_rx_done) {
        is_reading = false; // Reset state for the next read
        return (int32_t) bufsize; // Return full size to tell TinyUSB we are done!
    }

    // Still reading... keep returning 0 to let the CPU do other things
    return 0;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and return number of written bytes
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t bufsize) {
    (void) lun;
    (void) offset;

    // If we haven't started writing yet, trigger the DMA
    if (!is_writing) {
        uint32_t block_count = bufsize / 512;

        dma_tx_done = false;
        is_writing = true; // Lock the state

        if (HAL_SD_WriteBlocks_DMA(&hsd2, buffer, lba, block_count) != HAL_OK) {
            is_writing = false; // Unlock on error
            return -1; 
        }

        // Return 0 tells TinyUSB to go do other things
        return 0;
    }

    // If we are already writing, check if the DMA interrupt finished
    if (dma_tx_done) {
        is_writing = false; // Reset state
        return (int32_t) bufsize; // Done!
    }

    // Still writing...
    return 0;
}

// Callback invoked when received an SCSI command not in built-in list below
// - READ_CAPACITY10, READ_FORMAT_CAPACITY, INQUIRY, MODE_SENSE6, REQUEST_SENSE
// - READ10 and WRITE10 has their own callbacks
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
  (void) lun;
  (void) scsi_cmd;
  (void) buffer;
  (void) bufsize;

  // currently no other commands are supported

  // Set Sense = Invalid Command Operation
  (void) tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);

  return -1; // stall/failed command request;
}

#endif
