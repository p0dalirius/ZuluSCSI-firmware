/**
 * ZuluSCSI(TM) - AS/400 disk profile: IBM XCPR036 (FRU 53P3239).
 *
 * Captured byte arrays live in the sibling .inc files (generated from
 * .bin captures by utils/bin_to_inc.py). This file binds them into an
 * `as400_disk_profile_t` instance with per-page injection metadata.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "../as400_profile.h"

#ifdef PLATFORM_AS400

namespace {

#include "standard_inquiry.inc"
#include "page_00.inc"
#include "page_03.inc"
#include "page_80.inc"
#include "page_81.inc"
#include "page_c0.inc"
#include "page_c1.inc"
#include "page_c2.inc"
#include "page_c3.inc"
#include "page_c4.inc"
#include "page_c7.inc"
#include "page_c8.inc"
#include "page_d1.inc"
#include "page_d2.inc"
#include "mode_sense_all.inc"

// Std INQUIRY-level injections.
//   offset 36 -> 8-byte zero-prefixed IBM short serial ("000ACD83" form)
//   offset 114 -> 7-char IBM FRU ASCII ("53P3239" slot)
// XCPR036 has no EBCDIC FRU slot in the standard inquiry.
constexpr as400_inject_t kSpdInjections[] = {
    {  36, AS400_INJECT_IBM_SERIAL_8  },
    { 114, AS400_INJECT_IBM_FRU_ASCII },
};

// Page 0x80 carries the 20-char manufacturer-format unit serial; the first
// 8 chars are the manufacturer prefix (e.g. "3HX1QZE2") and are addressable
// via AS400_DiskSerialNumber.
constexpr as400_inject_t kPage80Injections[] = {
    { 4, AS400_INJECT_MFR_SERIAL_8 },
};

// Page 0xC4 carries only the 6-char IBM short serial (e.g. "0ACD83").
constexpr as400_inject_t kPageC4Injections[] = {
    { 4, AS400_INJECT_IBM_SHORT_SERIAL_6 },
};

// Page 0xD1 starts with a 10-char manufacturer disk part number
// (e.g. "9U9006-026") at offset 4.
constexpr as400_inject_t kPageD1Injections[] = {
    { 4, AS400_INJECT_MFR_PART_10 },
};

constexpr as400_vpd_page_t kPages[] = {
    { 0x00, sizeof(kPage00), kPage00, nullptr,           0 },
    { 0x03, sizeof(kPage03), kPage03, nullptr,           0 },
    { 0x80, sizeof(kPage80), kPage80, kPage80Injections, sizeof(kPage80Injections) / sizeof(kPage80Injections[0]) },
    { 0x81, sizeof(kPage81), kPage81, nullptr,           0 },
    { 0xC0, sizeof(kPageC0), kPageC0, nullptr,           0 },
    { 0xC1, sizeof(kPageC1), kPageC1, nullptr,           0 },
    { 0xC2, sizeof(kPageC2), kPageC2, nullptr,           0 },
    { 0xC3, sizeof(kPageC3), kPageC3, nullptr,           0 },
    { 0xC4, sizeof(kPageC4), kPageC4, kPageC4Injections, sizeof(kPageC4Injections) / sizeof(kPageC4Injections[0]) },
    { 0xC7, sizeof(kPageC7), kPageC7, nullptr,           0 },
    { 0xC8, sizeof(kPageC8), kPageC8, nullptr,           0 },
    { 0xD1, sizeof(kPageD1), kPageD1, kPageD1Injections, sizeof(kPageD1Injections) / sizeof(kPageD1Injections[0]) },
    { 0xD2, sizeof(kPageD2), kPageD2, nullptr,           0 },
};

} // namespace

extern "C" const as400_disk_profile_t as400_profile_xcpr036 = {
    /* .partNumber          = */ "53P3239",
    /* .vendorId            = */ "IBMAS400",
    /* .productId           = */ "XCPR036",
    /* .revision            = */ "1AEB",
    /* .defaultSerial       = */ "000ACD83",
    /* .modelName           = */ "IBM 53P3239 Ultra160 35GB (XCPR036, ST336753LC)",

    /* .standardInquiry     = */ kStandardInquiry,
    /* .standardInquiryLen  = */ sizeof(kStandardInquiry),
    /* .spdInjections       = */ kSpdInjections,
    /* .spdInjectionsLen    = */ sizeof(kSpdInjections) / sizeof(kSpdInjections[0]),

    /* .vpdPages            = */ kPages,
    /* .vpdPagesLen         = */ sizeof(kPages) / sizeof(kPages[0]),

    /* .modeSenseAllPages   = */ kModeSenseAll,
    /* .modeSenseAllPagesLen= */ sizeof(kModeSenseAll),
};

#endif // PLATFORM_AS400
