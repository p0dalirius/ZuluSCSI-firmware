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
#pragma once

#include <ZuluSCSI_platform_config.h>
#ifdef PLATFORM_AS400
#include <stdint.h>
#include <stddef.h>

// Per-target operation counters used by the AS/400 log sense path.
extern uint32_t as400_read_ops;
extern uint32_t as400_write_ops;

// AS/400 log sense (page 0x00 / page 0x30 / page 0x31) helpers retained for
// the SCSI2SD log sense path. The concrete data lives in the active disk
// profile (see src/as400_profiles/as400_profile.h); these globals describe
// only the wire-format envelope.
extern const uint8_t  as400_log_sense_page_00[];
extern const size_t   as400_log_sense_page_00_len;
extern const uint16_t as400_log_sense_page_30_page_length;
extern const uint8_t  as400_log_sense_page_30_page_list_length;
extern const uint16_t as400_log_sense_page_31_page_length;

# ifdef __cplusplus
extern "C" {
# endif
// Compute the 8-byte AS/400 short serial for `scsi_id`, deriving from the SD
// CID when available and falling back to the platform 8-byte MCU id. The two
// trailing bytes are pinned to "75" to mirror real-disk captures.
void as400_get_serial_8(uint8_t scsi_id, uint8_t* serial_buf);
# ifdef __cplusplus
}
# endif

#endif
