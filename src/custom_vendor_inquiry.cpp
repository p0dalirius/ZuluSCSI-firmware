/**
 * Copyright (C) 2025-2026 Kevin Moonlight <me@yyzkevin.com>
 *
 * This file is part of ZuluSCSI
 *
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

// Custom SCSI inquiry data (VPD/SPD) from INI configuration

#include "custom_vendor_inquiry.h"

#include "ZuluSCSI_log.h"
#include "ZuluSCSI_config.h"
#include "ZuluSCSI_settings.h"
#include "ZuluSCSI_disk.h"
#include <ZuluSCSI_platform_config.h>
#ifdef PLATFORM_AS400
# include "as400_values.h"
# include "as400_profiles/as400_profile.h"
#endif

#include <scsi.h>
#include <minIni.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// Storage for custom VPD pages: up to 16 entries across all SCSI IDs
// Each entry: [0]=scsiId, [1]=pageCode, [2]=length, [3..]=data
#define MAX_CUSTOM_VPD_ENTRIES 16
#define MAX_VPD_DATA_SIZE 128
static struct {
    uint8_t scsiId;
    uint8_t pageCode;
    uint8_t length;
    uint8_t data[MAX_VPD_DATA_SIZE];
} g_custom_vpd[MAX_CUSTOM_VPD_ENTRIES];
static int g_custom_vpd_count = 0;

// Storage for custom standard inquiry data per SCSI ID
#define MAX_SPD_SIZE 128
static struct {
    uint8_t length;
    uint8_t data[MAX_SPD_SIZE];
} g_custom_spd[S2S_MAX_TARGETS];

#ifdef PLATFORM_AS400
// Per-SCSI-ID INI-driven overrides for AS/400 inquiry/VPD injections. Each
// `length` is 0 when no override is configured for that target.
//
// AS400_IBMDiskSerialNumber: 6-char IBM short serial (e.g. "0ACD83"). Used
// directly in VPD page 0xC4 and zero-prefixed to 8 chars in the std INQUIRY
// IBM-serial slot. When unset, the 6-char form is derived from the first 6
// bytes returned by as400_get_serial_8().
static struct {
    uint8_t length;
    uint8_t data[6];
} g_as400_ibm_short_serial_override[S2S_MAX_TARGETS];

// AS400_IBMDiskPartNumber: 7-char IBM FRU (e.g. "53P3239"). Acts as the
// profile-lookup key as well as the value injected at FRU slots in std
// INQUIRY and (for DGVS09U) VPD page 0x01.
static struct {
    uint8_t length;
    uint8_t ascii[7];
    uint8_t ebcdic[7];
} g_as400_ibm_part_override[S2S_MAX_TARGETS];

// AS400_IBMDiskPlantCode: 5-char IBM Type-11S plant + sub-code (e.g.
// "YL112"). Currently surfaced via the accessor for log/UI synthesis only;
// not patched into any wire-format field.
static struct {
    uint8_t length;
    uint8_t data[5];
} g_as400_plant_code_override[S2S_MAX_TARGETS];

// AS400_DiskSerialNumber: 8-char manufacturer (Seagate-style) serial
// (e.g. "3HX1QZE2"). Used by XCPR036's VPD page 0x80 (offset 4 within the
// 20-char unit serial). When unset, the captured profile bytes remain.
static struct {
    uint8_t length;
    uint8_t data[8];
} g_as400_mfr_serial_override[S2S_MAX_TARGETS];

// AS400_DiskPartNumber: up-to-10-char manufacturer disk part number
// (e.g. "9U9006-026"). Used by XCPR036's VPD page 0xD1 offset 4. Shorter
// values are right-padded with ASCII spaces; longer values truncated.
static struct {
    uint8_t length;
    uint8_t data[10];
} g_as400_mfr_part_override[S2S_MAX_TARGETS];

// Convert a single ASCII character to IBM EBCDIC (CP037 subset).
// Supports digits, uppercase A-Z, and space. Lowercase is uppercased first.
// Anything else returns EBCDIC space (0x40).
static uint8_t asciiToEbcdic(char c)
{
    if (c >= 'a' && c <= 'z') c -= ('a' - 'A');
    if (c >= '0' && c <= '9') return (uint8_t)(0xF0 + (c - '0'));
    if (c >= 'A' && c <= 'I') return (uint8_t)(0xC1 + (c - 'A'));
    if (c >= 'J' && c <= 'R') return (uint8_t)(0xD1 + (c - 'J'));
    if (c >= 'S' && c <= 'Z') return (uint8_t)(0xE2 + (c - 'S'));
    return 0x40;
}
#endif

// Parse space/comma-separated hex values from a string into a byte buffer.
// Returns number of bytes parsed.
static int parseHexString(const char *str, uint8_t *buf, int maxlen)
{
    const char *ptr = str;
    char *end;
    int count = 0;

    while (*ptr != '\0' && count < maxlen)
    {
        buf[count++] = (uint8_t)strtol(ptr, &end, 16);
        if (ptr == end) break; // No conversion possible
        ptr = end;
        while (*ptr == ' ' || *ptr == ',') ptr++;
    }
    return count;
}

// Check if a custom VPD page already exists for a given SCSI ID and page code
static bool hasCustomVPD(uint8_t scsiId, uint8_t pageCode)
{
    for (int i = 0; i < g_custom_vpd_count; i++)
    {
        if (g_custom_vpd[i].scsiId == scsiId && g_custom_vpd[i].pageCode == pageCode)
            return true;
    }
    return false;
}

#ifdef PLATFORM_AS400
// Public accessors so other code paths (mode.c, image-open) can read the
// per-target overrides without needing the full custom_vendor_inquiry.h.

extern "C" size_t as400_get_ibm_short_serial(uint8_t scsiId, uint8_t *buf6)
{
    uint8_t id = scsiId & S2S_CFG_TARGET_ID_BITS;
    if (g_as400_ibm_short_serial_override[id].length != 6) return 0;
    memcpy(buf6, g_as400_ibm_short_serial_override[id].data, 6);
    return 6;
}
extern "C" size_t as400_get_mfr_serial(uint8_t scsiId, uint8_t *buf8)
{
    uint8_t id = scsiId & S2S_CFG_TARGET_ID_BITS;
    if (g_as400_mfr_serial_override[id].length != 8) return 0;
    memcpy(buf8, g_as400_mfr_serial_override[id].data, 8);
    return 8;
}
extern "C" size_t as400_get_ibm_fru_ascii(uint8_t scsiId, uint8_t *buf7)
{
    uint8_t id = scsiId & S2S_CFG_TARGET_ID_BITS;
    if (g_as400_ibm_part_override[id].length != 7) return 0;
    memcpy(buf7, g_as400_ibm_part_override[id].ascii, 7);
    return 7;
}
extern "C" size_t as400_get_ibm_fru_ebcdic(uint8_t scsiId, uint8_t *buf7)
{
    uint8_t id = scsiId & S2S_CFG_TARGET_ID_BITS;
    if (g_as400_ibm_part_override[id].length != 7) return 0;
    memcpy(buf7, g_as400_ibm_part_override[id].ebcdic, 7);
    return 7;
}
extern "C" size_t as400_get_mfr_part(uint8_t scsiId, uint8_t *buf, size_t maxlen)
{
    uint8_t id = scsiId & S2S_CFG_TARGET_ID_BITS;
    size_t n = g_as400_mfr_part_override[id].length;
    if (n == 0) return 0;
    if (maxlen < 10) return 0;
    memset(buf, ' ', 10);
    if (n > 10) n = 10;
    memcpy(buf, g_as400_mfr_part_override[id].data, n);
    return 10;
}
extern "C" size_t as400_get_plant_code(uint8_t scsiId, uint8_t *buf5)
{
    uint8_t id = scsiId & S2S_CFG_TARGET_ID_BITS;
    if (g_as400_plant_code_override[id].length != 5) return 0;
    memcpy(buf5, g_as400_plant_code_override[id].data, 5);
    return 5;
}

// Compute the 6-char IBM short serial for `scsiId`: configured override
// when present, otherwise the first 6 bytes of as400_get_serial_8(). The
// caller must provide a 6-byte buffer.
static void computeIbmShortSerial6(uint8_t scsiId, uint8_t out6[6])
{
    if (as400_get_ibm_short_serial(scsiId, out6) == 6) return;
    uint8_t derived[8];
    as400_get_serial_8(scsiId, derived);
    memcpy(out6, derived, 6);
}

// Apply a single profile-defined injection to a buffer. Bounds-checks the
// requested offset against the buffer length and silently skips if it would
// overflow. IBM-serial injections always run (using either a configured
// override or the auto-derived SD/MCU value); FRU and manufacturer-field
// injections only run when the corresponding INI override is configured.
static void applyInjection(uint8_t *data, size_t dataLen,
                           const as400_inject_t *inj, uint8_t scsiId)
{
    uint8_t id = scsiId & S2S_CFG_TARGET_ID_BITS;

    switch (inj->field)
    {
    case AS400_INJECT_IBM_SERIAL_8:
    {
        if ((size_t)inj->offset + 8 > dataLen) return;
        uint8_t serial[8] = { '0', '0', 0, 0, 0, 0, 0, 0 };
        computeIbmShortSerial6(scsiId, serial + 2);
        memcpy(data + inj->offset, serial, 8);
        break;
    }
    case AS400_INJECT_IBM_SHORT_SERIAL_6:
    {
        if ((size_t)inj->offset + 6 > dataLen) return;
        uint8_t serial[6];
        computeIbmShortSerial6(scsiId, serial);
        memcpy(data + inj->offset, serial, 6);
        break;
    }
    case AS400_INJECT_IBM_FRU_ASCII:
        if (g_as400_ibm_part_override[id].length != 7) return;
        if ((size_t)inj->offset + 7 > dataLen) return;
        memcpy(data + inj->offset, g_as400_ibm_part_override[id].ascii, 7);
        break;
    case AS400_INJECT_IBM_FRU_EBCDIC:
        if (g_as400_ibm_part_override[id].length != 7) return;
        if ((size_t)inj->offset + 7 > dataLen) return;
        memcpy(data + inj->offset, g_as400_ibm_part_override[id].ebcdic, 7);
        break;
    case AS400_INJECT_MFR_SERIAL_8:
        if (g_as400_mfr_serial_override[id].length != 8) return;
        if ((size_t)inj->offset + 8 > dataLen) return;
        memcpy(data + inj->offset, g_as400_mfr_serial_override[id].data, 8);
        break;
    case AS400_INJECT_MFR_PART_10:
    {
        if (g_as400_mfr_part_override[id].length == 0) return;
        if ((size_t)inj->offset + 10 > dataLen) return;
        uint8_t buf[10];
        memset(buf, ' ', 10);
        size_t n = g_as400_mfr_part_override[id].length;
        if (n > 10) n = 10;
        memcpy(buf, g_as400_mfr_part_override[id].data, n);
        memcpy(data + inj->offset, buf, 10);
        break;
    }
    case AS400_INJECT_NONE:
    default:
        break;
    }
}

// Populate default AS/400 inquiry and VPD data from the active profile.
// Only fills in data that wasn't already provided via INI.
static void loadAS400Defaults(uint8_t scsiId, S2S_CFG_TYPE type)
{
    if (!((g_scsi_settings.getSystem()->quirks & S2S_CFG_QUIRKS_AS400) && type == S2S_CFG_FIXED))
        return;

    const as400_disk_profile_t *profile = as400_get_active_profile(scsiId);
    bool loaded_default_data = false;

    if (g_custom_spd[scsiId].length == 0 && profile->standardInquiry)
    {
        size_t len = profile->standardInquiryLen;
        if (len > MAX_SPD_SIZE) len = MAX_SPD_SIZE;
        memcpy(g_custom_spd[scsiId].data, profile->standardInquiry, len);
        for (size_t i = 0; i < profile->spdInjectionsLen; ++i)
        {
            applyInjection(g_custom_spd[scsiId].data, len, &profile->spdInjections[i], scsiId);
        }
        g_custom_spd[scsiId].length = (uint8_t)len;
        loaded_default_data = true;
    }

    for (size_t p = 0; p < profile->vpdPagesLen && g_custom_vpd_count < MAX_CUSTOM_VPD_ENTRIES; p++)
    {
        const as400_vpd_page_t *page = &profile->vpdPages[p];
        if (hasCustomVPD(scsiId, page->pageCode))
            continue;

        loaded_default_data = true;
        int idx = g_custom_vpd_count;
        size_t pageLen = page->length;
        if (pageLen > MAX_VPD_DATA_SIZE) pageLen = MAX_VPD_DATA_SIZE;
        g_custom_vpd[idx].scsiId = scsiId;
        g_custom_vpd[idx].pageCode = page->pageCode;
        g_custom_vpd[idx].length = (uint8_t)pageLen;
        memcpy(g_custom_vpd[idx].data, page->data, pageLen);

        for (size_t i = 0; i < page->injectionsLen; ++i)
        {
            applyInjection(g_custom_vpd[idx].data, pageLen, &page->injections[i], scsiId);
        }

        g_custom_vpd_count++;
    }

    if (loaded_default_data)
    {
        logmsg("---- Loaded default AS/400 inquiry data for SCSI ID ", (int)scsiId,
               " (profile: ", profile->modelName, ")");
    }
}

// Trim ASCII spaces from both ends of a NUL-terminated buffer in place.
static void trimSpaces(char *buf)
{
    char *start = buf;
    while (*start == ' ') ++start;
    if (start != buf) memmove(buf, start, strlen(start) + 1);
    size_t n = strlen(buf);
    while (n > 0 && buf[n - 1] == ' ') buf[--n] = 0;
}

// Extract the 6-char unit-serial tail from an arbitrary IBM serial input.
// Accepted forms:
//   - 6 chars            -> used directly
//   - 8 chars            -> last 6 chars used (drops "00" prefix)
//   - 22 chars "11S<FRU>YL<plant><serial>" -> last 6 chars used
//   - any other length   -> truncated/padded to 6 with trailing spaces
static void normaliseIbmShortSerial6(const char *in, uint8_t out6[6])
{
    size_t inlen = strlen(in);
    memset(out6, ' ', 6);
    if (inlen == 0) return;
    if (inlen >= 6)
    {
        memcpy(out6, in + (inlen - 6), 6);
    }
    else
    {
        memcpy(out6, in, inlen);
    }
    for (int i = 0; i < 6; ++i)
    {
        char c = (char)out6[i];
        if (c >= 'a' && c <= 'z') out6[i] = (uint8_t)(c - 'a' + 'A');
    }
}
#endif

void parseCustomInquiryData(uint8_t scsiId, S2S_CFG_TYPE type)
{
    char tmp[512];
    char section[6] = "SCSI0";
    char key[8];

    g_custom_vpd_count = 0;
    memset(g_custom_spd, 0, sizeof(g_custom_spd));
#ifdef PLATFORM_AS400
    memset(g_as400_ibm_short_serial_override, 0, sizeof(g_as400_ibm_short_serial_override));
    memset(g_as400_ibm_part_override,         0, sizeof(g_as400_ibm_part_override));
    memset(g_as400_plant_code_override,       0, sizeof(g_as400_plant_code_override));
    memset(g_as400_mfr_serial_override,       0, sizeof(g_as400_mfr_serial_override));
    memset(g_as400_mfr_part_override,         0, sizeof(g_as400_mfr_part_override));
#endif

    section[4] = scsiEncodeID(scsiId);

    // Parse VPD pages: vpd00, vpd80, etc.
    for (int page = 0; page < 0xFF && g_custom_vpd_count < MAX_CUSTOM_VPD_ENTRIES; page++)
    {
        snprintf(key, sizeof(key), "vpd%02x", page);
        if (ini_gets(section, key, "", tmp, sizeof(tmp), CONFIGFILE))
        {
            int idx = g_custom_vpd_count;
            g_custom_vpd[idx].scsiId = scsiId;
            g_custom_vpd[idx].pageCode = page;
            g_custom_vpd[idx].length = parseHexString(tmp, g_custom_vpd[idx].data, MAX_VPD_DATA_SIZE);
            if (g_custom_vpd[idx].length > 0)
            {
                logmsg("---- Custom VPD page 0x", key + 3, " for SCSI ID ", scsiId,
                        ": ", (int)g_custom_vpd[idx].length, " bytes");
                g_custom_vpd_count++;
            }
        }
    }

    // Parse standard inquiry override: spd=
    if (ini_gets(section, "spd", "", tmp, sizeof(tmp), CONFIGFILE))
    {
        g_custom_spd[scsiId].length = parseHexString(tmp, g_custom_spd[scsiId].data, MAX_SPD_SIZE);
        if (g_custom_spd[scsiId].length > 0)
        {
            logmsg("---- Custom SPD for SCSI ID ", scsiId, ": ", (int)g_custom_spd[scsiId].length, " bytes");
        }
    }
#ifdef PLATFORM_AS400
    uint8_t id = scsiId & S2S_CFG_TARGET_ID_BITS;

    // AS400_IBMDiskPartNumber = <up to 7 chars>
    // 7-char IBM FRU. Accepts [0-9 A-Z] (lowercase is uppercased); unsupported
    // characters become space. Shorter values are right-padded with spaces.
    // The same value is injected into the FRU slots of the active profile's
    // standard INQUIRY (and, for DGVS09U-style profiles, VPD page 0x01) and
    // also acts as the profile lookup key.
    if (ini_gets(section, "AS400_IBMDiskPartNumber", "", tmp, sizeof(tmp), CONFIGFILE))
    {
        trimSpaces(tmp);
        size_t slen = strlen(tmp);
        if (slen > 0)
        {
            memset(g_as400_ibm_part_override[id].ascii,  ' ',  7);
            memset(g_as400_ibm_part_override[id].ebcdic, 0x40, 7);
            if (slen > 7) slen = 7;
            for (size_t i = 0; i < slen; i++)
            {
                char c = tmp[i];
                if (c >= 'a' && c <= 'z') c -= ('a' - 'A');
                uint8_t eb = asciiToEbcdic(c);
                g_as400_ibm_part_override[id].ascii[i]  = (eb == 0x40 && c != ' ') ? ' ' : (uint8_t)c;
                g_as400_ibm_part_override[id].ebcdic[i] = eb;
            }
            g_as400_ibm_part_override[id].length = 7;
            logmsg("---- AS400_IBMDiskPartNumber for SCSI ID ", (int)scsiId, ": \"", tmp, "\"");
        }
    }

    // AS400_IBMDiskSerialNumber = <6, 8, or 22 chars>
    // Stored as a 6-char unit serial; the helper normalises common input
    // shapes (raw 6-char, 8-char zero-padded form, or full 11S barcode form).
    if (ini_gets(section, "AS400_IBMDiskSerialNumber", "", tmp, sizeof(tmp), CONFIGFILE))
    {
        trimSpaces(tmp);
        size_t slen = strlen(tmp);
        if (slen > 0)
        {
            normaliseIbmShortSerial6(tmp, g_as400_ibm_short_serial_override[id].data);
            g_as400_ibm_short_serial_override[id].length = 6;
            logmsg("---- AS400_IBMDiskSerialNumber for SCSI ID ", (int)scsiId, ": \"", tmp, "\"");
        }
    }

    // AS400_IBMDiskPlantCode = <5 chars>
    // Plant + sub-code segment of the IBM Type-11S barcode (e.g. "YL112").
    // Stored for log/UI synthesis; not patched into any wire-format field.
    if (ini_gets(section, "AS400_IBMDiskPlantCode", "", tmp, sizeof(tmp), CONFIGFILE))
    {
        trimSpaces(tmp);
        size_t slen = strlen(tmp);
        if (slen > 0)
        {
            memset(g_as400_plant_code_override[id].data, ' ', 5);
            if (slen > 5) slen = 5;
            for (size_t i = 0; i < slen; i++)
            {
                char c = tmp[i];
                if (c >= 'a' && c <= 'z') c -= ('a' - 'A');
                g_as400_plant_code_override[id].data[i] = (uint8_t)c;
            }
            g_as400_plant_code_override[id].length = 5;
            logmsg("---- AS400_IBMDiskPlantCode for SCSI ID ", (int)scsiId, ": \"", tmp, "\"");
        }
    }

    // AS400_DiskSerialNumber = <up to 8 chars>
    // Manufacturer (e.g. Seagate) 8-char disk serial (e.g. "3HX1QZE2").
    // Right-padded with spaces; truncated when longer. Used by VPD page 0x80
    // on profiles that carry a manufacturer-format unit serial (XCPR036).
    if (ini_gets(section, "AS400_DiskSerialNumber", "", tmp, sizeof(tmp), CONFIGFILE))
    {
        trimSpaces(tmp);
        size_t slen = strlen(tmp);
        if (slen > 0)
        {
            memset(g_as400_mfr_serial_override[id].data, ' ', 8);
            if (slen > 8) slen = 8;
            memcpy(g_as400_mfr_serial_override[id].data, tmp, slen);
            g_as400_mfr_serial_override[id].length = 8;
            logmsg("---- AS400_DiskSerialNumber for SCSI ID ", (int)scsiId, ": \"", tmp, "\"");
        }
    }

    // AS400_DiskPartNumber = <up to 10 chars>
    // Manufacturer 10-char disk part number (e.g. "9U9006-026"). Used by
    // VPD page 0xD1 on profiles that carry a manufacturer-format part slot.
    if (ini_gets(section, "AS400_DiskPartNumber", "", tmp, sizeof(tmp), CONFIGFILE))
    {
        trimSpaces(tmp);
        size_t slen = strlen(tmp);
        if (slen > 0)
        {
            memset(g_as400_mfr_part_override[id].data, ' ', 10);
            if (slen > 10) slen = 10;
            memcpy(g_as400_mfr_part_override[id].data, tmp, slen);
            g_as400_mfr_part_override[id].length = (uint8_t)slen;
            logmsg("---- AS400_DiskPartNumber for SCSI ID ", (int)scsiId, ": \"", tmp, "\"");
        }
    }

    // Resolve and cache the active profile for this target. Lookup key is the
    // 7-char IBM FRU just configured (NUL-terminated, trailing spaces trimmed)
    // or NULL when no AS400_IBMDiskPartNumber was provided -> default profile.
    {
        char fru[8] = {0};
        const char *lookup = NULL;
        if (g_as400_ibm_part_override[id].length == 7)
        {
            memcpy(fru, g_as400_ibm_part_override[id].ascii, 7);
            for (int i = 6; i >= 0 && fru[i] == ' '; --i) fru[i] = 0;
            lookup = fru;
        }
        const as400_disk_profile_t *profile = as400_lookup_profile(lookup);
        as400_set_active_profile(scsiId, profile);
    }

    loadAS400Defaults(scsiId, type);
#endif
}

bool getCustomVPD(uint8_t scsiId, uint8_t pageCode, uint8_t *buf, uint8_t *length)
{
    for (int i = 0; i < g_custom_vpd_count; i++)
    {
        if (g_custom_vpd[i].scsiId == (scsiId & S2S_CFG_TARGET_ID_BITS) && g_custom_vpd[i].pageCode == pageCode)
        {
            *length = g_custom_vpd[i].length;
            memcpy(buf, g_custom_vpd[i].data, g_custom_vpd[i].length);
            return true;
        }
    }
    return false;
}

bool getCustomSPD(uint8_t scsiId, uint8_t *buf, uint16_t *length)
{
    uint8_t id = scsiId & S2S_CFG_TARGET_ID_BITS;
    if (g_custom_spd[id].length > 0)
    {
        *length = g_custom_spd[id].length;
        memcpy(buf, g_custom_spd[id].data, g_custom_spd[id].length);
        return true;
    }
    return false;
}
