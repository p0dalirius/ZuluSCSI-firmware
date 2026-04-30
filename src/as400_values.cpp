/**
* ZuluSCSI(TM) - Copyright (c) 2025 Rabbit Hole Computing(TM)
* Copyright (c) 2025 Kevin Moonlight <me@yyzkevin.com>
* ZuluSCSI(TM) firmware is licensed under the GPL version 3 or any later version.
*
* https://www.gnu.org/licenses/gpl-3.0.html
* ----
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/

#include <ZuluSCSI_platform_config.h>
#ifdef PLATFORM_AS400
#include <ZuluSCSI_platform.h>

#include "as400_values.h"

#include <SdFat.h>
extern SdFs SD;

// Log sense envelope constants. The actual page data is computed at runtime
// elsewhere; these describe the wire-format lengths advertised in page 0x00.
const uint8_t as400_log_sense_page_00[] =
{
    0x00, 0x00, 0x00, 0x03, 0x00, 0x30, 0x31
};

const size_t   as400_log_sense_page_00_len             = sizeof(as400_log_sense_page_00);
const uint16_t as400_log_sense_page_30_page_length     = 80;
const uint8_t  as400_log_sense_page_30_page_list_length = 76;
const uint16_t as400_log_sense_page_31_page_length     = 192;

uint32_t as400_read_ops  = 0;
uint32_t as400_write_ops = 0;

extern "C" void as400_get_serial_8(uint8_t scsi_id, uint8_t* serial_buf)
{
    cid_t sd_cid;
    uint32_t sd_sn = 0;
    if (SD.card()->readCID(&sd_cid))
    {
        sd_sn = sd_cid.psn();
    }
    const char hex[] = "0123456789ABCDEF";
    if (sd_sn == 0)
    {
        const uint8_t *board_id = platform_get_8byte_mcu_id();
        serial_buf[0] = hex[board_id[5] & 0xF];
        serial_buf[1] = hex[board_id[4] & 0xF];
        serial_buf[2] = hex[board_id[3] & 0xF];
        serial_buf[3] = hex[board_id[2] & 0xF];
        serial_buf[4] = hex[board_id[1] & 0xF];
        serial_buf[5] = hex[(board_id[0] ^ scsi_id) & 0xF];
    }
    else
    {
        serial_buf[0] = hex[(sd_sn >> 28) & 0xF];
        serial_buf[1] = hex[(sd_sn >> 24) & 0xF];
        serial_buf[2] = hex[(sd_sn >> 20) & 0xF];
        serial_buf[3] = hex[(sd_sn >> 16) & 0xF];
        serial_buf[4] = hex[(sd_sn >> 12) & 0xF];
        serial_buf[5] = hex[((sd_sn >> 8) ^ scsi_id) & 0xF];
    }
    // Drives seem to all have 75 as last two digits of the serial number
    serial_buf[6] = '7';
    serial_buf[7] = '5';
}

#endif // PLATFORM_AS400
