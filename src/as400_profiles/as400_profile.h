/**
 * ZuluSCSI(TM) - AS/400 disk profile descriptors.
 *
 * Each AS/400 disk model the firmware can emulate is described by an
 * `as400_disk_profile_t` instance. A profile bundles the captured raw bytes
 * of the disk's standard INQUIRY response, VPD pages, and mode-sense
 * (all-pages) reply, alongside per-page injection metadata that lets the
 * runtime patch in per-target identifiers (serial, FRU, ...) without altering
 * the stored captures.
 *
 * Profile data lives in `src/as400_profiles/<model>/` and is generated from
 * the `.bin` captures via `utils/bin_to_inc.py`. See SOURCE.txt in each
 * profile directory for the provenance of its captures.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */
#pragma once

#include <ZuluSCSI_platform_config.h>

#ifdef PLATFORM_AS400

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Per-page / per-blob injection field selector. The runtime substitutes a
 * per-target value (serial, FRU, ...) at the offset given in the profile's
 * injection table. Adding new field kinds is a matter of extending this enum
 * and the `applyInjection()` switch in custom_vendor_inquiry.cpp. */
typedef enum {
    AS400_INJECT_NONE = 0,
    /* 8-byte IBM short serial. Computed as "00" + 6-char IBM short serial
     * (AS400_IBMDiskSerialNumber). When no override is configured, the 6-char
     * form is derived from the SD CID / MCU id via as400_get_serial_8(). */
    AS400_INJECT_IBM_SERIAL_8,
    /* 6-byte IBM short serial. Same source as IBM_SERIAL_8 but without the
     * "00" prefix. Used by VPD page 0xC4 on XCPR036-style disks. */
    AS400_INJECT_IBM_SHORT_SERIAL_6,
    /* 7-char IBM FRU (AS400_IBMDiskPartNumber). Only injected when the user
     * has configured the FRU explicitly. */
    AS400_INJECT_IBM_FRU_ASCII,
    AS400_INJECT_IBM_FRU_EBCDIC,
    /* 8-char manufacturer disk serial number (AS400_DiskSerialNumber). Used
     * by VPD page 0x80 on XCPR036-style disks where the manufacturer's
     * Seagate-style serial occupies the first 8 chars of a 20-char field. */
    AS400_INJECT_MFR_SERIAL_8,
    /* up-to-10-char manufacturer disk part number (AS400_DiskPartNumber).
     * Right-padded with ASCII spaces. Used by VPD page 0xD1 on XCPR036. */
    AS400_INJECT_MFR_PART_10
} as400_inject_field_t;

/* Per-target accessors for INI overrides used by injections. Defined in
 * src/custom_vendor_inquiry.cpp; surfaced here so future code paths
 * (image-open, log-sense) can read the same override values without
 * pulling in custom_vendor_inquiry.h. Each function returns 0 when the
 * field is unset for `scsiId` and a positive byte length otherwise. */
size_t as400_get_ibm_short_serial(uint8_t scsiId, uint8_t *buf6);
size_t as400_get_mfr_serial(uint8_t scsiId, uint8_t *buf8);
size_t as400_get_ibm_fru_ascii(uint8_t scsiId, uint8_t *buf7);
size_t as400_get_ibm_fru_ebcdic(uint8_t scsiId, uint8_t *buf7);
size_t as400_get_mfr_part(uint8_t scsiId, uint8_t *buf, size_t maxlen);
size_t as400_get_plant_code(uint8_t scsiId, uint8_t *buf5);

typedef struct {
    uint16_t offset;  /* byte offset within the page or SPD blob       */
    uint8_t  field;   /* `as400_inject_field_t` enum value             */
} as400_inject_t;

typedef struct {
    uint8_t        pageCode;        /* SCSI VPD page code                          */
    uint16_t       length;          /* bytes in `data`                             */
    const uint8_t *data;             /* raw page bytes (header + payload)           */
    const as400_inject_t *injections;/* may be NULL when injectionsLen == 0          */
    size_t                injectionsLen;
} as400_vpd_page_t;

typedef struct {
    /* --- Identification --- */
    const char *partNumber;     /* canonical 7-char IBM FRU (profile lookup key)        */
    const char *vendorId;       /* SCSI vendor id, max 8 chars (e.g. "IBMAS400")        */
    const char *productId;      /* SCSI product id, max 16 chars (e.g. "DGVS09U")       */
    const char *revision;       /* SCSI rev, max 4 chars (e.g. "02A1")                  */
    const char *defaultSerial;  /* 8-char fallback IBM serial when no SD/MCU id usable  */
    const char *modelName;      /* human-readable label                                  */

    /* --- Standard INQUIRY response (raw blob) --- */
    const uint8_t        *standardInquiry;
    size_t                standardInquiryLen;
    const as400_inject_t *spdInjections;
    size_t                spdInjectionsLen;

    /* --- VPD pages --- */
    const as400_vpd_page_t *vpdPages;
    size_t                  vpdPagesLen;

    /* --- Mode sense (all-pages) response --- */
    const uint8_t *modeSenseAllPages;
    size_t         modeSenseAllPagesLen;
} as400_disk_profile_t;

/* Returns the profile whose `partNumber` matches `partNumber` (case-insensitive
 * exact match). Returns the default profile when `partNumber` is NULL/empty or
 * does not match any registered profile. Never returns NULL. */
const as400_disk_profile_t *as400_lookup_profile(const char *partNumber);

/* Returns the profile used when no AS400_DiskPartNumber is configured. */
const as400_disk_profile_t *as400_default_profile(void);

/* Per-target active-profile cache. Set during parseCustomInquiryData(),
 * read by mode-sense / image-open paths that need profile-specific data
 * after boot. Returns the default profile if the target has not been set. */
const as400_disk_profile_t *as400_get_active_profile(uint8_t scsiId);
void                        as400_set_active_profile(uint8_t scsiId, const as400_disk_profile_t *profile);

#ifdef __cplusplus
}
#endif

#endif /* PLATFORM_AS400 */
