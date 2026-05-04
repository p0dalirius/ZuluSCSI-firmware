/**
 * ZuluSCSI(TM) - AS/400 disk profile: IBM DGVS09U (FRU 09L4044).
 *
 * Captured byte arrays live in the sibling .inc files (generated from the
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
#include "page_01.inc"
#include "page_03.inc"
#include "page_80.inc"
#include "page_82.inc"
#include "page_83.inc"
#include "page_d1.inc"
#include "page_d2.inc"
#include "mode_sense_all.inc"

constexpr as400_inject_t kSpdInjections[] = {
    { 38,  AS400_INJECT_IBM_SERIAL_8  },
    { 114, AS400_INJECT_IBM_FRU_ASCII },
};

constexpr as400_inject_t kPage01Injections[] = {
    { 5,  AS400_INJECT_IBM_FRU_ASCII  },
    { 29, AS400_INJECT_IBM_FRU_EBCDIC },
};
constexpr as400_inject_t kPage80Injections[] = {
    { 12, AS400_INJECT_IBM_SERIAL_8 },
};
constexpr as400_inject_t kPage82Injections[] = {
    { 16, AS400_INJECT_IBM_SERIAL_8 },
};
constexpr as400_inject_t kPage83Injections[] = {
    { 34, AS400_INJECT_IBM_SERIAL_8 },
};
constexpr as400_inject_t kPageD1Injections[] = {
    { 70, AS400_INJECT_IBM_SERIAL_8 },
};

constexpr as400_vpd_page_t kPages[] = {
    { 0x00, sizeof(kPage00), kPage00, nullptr,            0 },
    { 0x01, sizeof(kPage01), kPage01, kPage01Injections,  sizeof(kPage01Injections) / sizeof(kPage01Injections[0]) },
    { 0x03, sizeof(kPage03), kPage03, nullptr,            0 },
    { 0x80, sizeof(kPage80), kPage80, kPage80Injections,  sizeof(kPage80Injections) / sizeof(kPage80Injections[0]) },
    { 0x82, sizeof(kPage82), kPage82, kPage82Injections,  sizeof(kPage82Injections) / sizeof(kPage82Injections[0]) },
    { 0x83, sizeof(kPage83), kPage83, kPage83Injections,  sizeof(kPage83Injections) / sizeof(kPage83Injections[0]) },
    { 0xD1, sizeof(kPageD1), kPageD1, kPageD1Injections,  sizeof(kPageD1Injections) / sizeof(kPageD1Injections[0]) },
    { 0xD2, sizeof(kPageD2), kPageD2, nullptr,            0 },
};

} // namespace

extern "C" const as400_disk_profile_t as400_profile_dgvs09u = {
    /* .partNumber          = */ "09L4044",
    /* .vendorId            = */ "IBMAS400",
    /* .productId           = */ "DGVS09U",
    /* .revision            = */ "02A1",
    /* .defaultSerial       = */ "067ACE75",
    /* .modelName           = */ "IBM DGVS09U 9.1GB",

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
