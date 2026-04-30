/**
 * ZuluSCSI(TM) - AS/400 disk profile registry.
 *
 * Maintains the list of compiled-in profiles, exposes the lookup-by-FRU API,
 * and caches the active profile per SCSI target so the mode-sense and
 * image-open paths can reach it without re-parsing the INI.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "as400_profile.h"

#ifdef PLATFORM_AS400

#include <ctype.h>
#include <string.h>
#include <scsi2sd.h>

extern "C" const as400_disk_profile_t as400_profile_dgvs09u;

namespace {

const as400_disk_profile_t *const kProfiles[] = {
    &as400_profile_dgvs09u,
};
constexpr size_t kProfilesLen = sizeof(kProfiles) / sizeof(kProfiles[0]);

const as400_disk_profile_t *g_active[S2S_MAX_TARGETS] = { nullptr };

bool ieq(const char *a, const char *b)
{
    if (!a || !b) return false;
    while (*a && *b)
    {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return false;
        ++a; ++b;
    }
    return *a == 0 && *b == 0;
}

} // namespace

extern "C" const as400_disk_profile_t *as400_default_profile(void)
{
    return &as400_profile_dgvs09u;
}

extern "C" const as400_disk_profile_t *as400_lookup_profile(const char *partNumber)
{
    if (!partNumber || !*partNumber) return as400_default_profile();
    for (size_t i = 0; i < kProfilesLen; ++i)
    {
        if (ieq(kProfiles[i]->partNumber, partNumber)) return kProfiles[i];
    }
    return as400_default_profile();
}

extern "C" const as400_disk_profile_t *as400_get_active_profile(uint8_t scsiId)
{
    uint8_t id = scsiId & S2S_CFG_TARGET_ID_BITS;
    if (id >= S2S_MAX_TARGETS) return as400_default_profile();
    const as400_disk_profile_t *p = g_active[id];
    return p ? p : as400_default_profile();
}

extern "C" void as400_set_active_profile(uint8_t scsiId, const as400_disk_profile_t *profile)
{
    uint8_t id = scsiId & S2S_CFG_TARGET_ID_BITS;
    if (id >= S2S_MAX_TARGETS) return;
    g_active[id] = profile ? profile : as400_default_profile();
}

#endif // PLATFORM_AS400
