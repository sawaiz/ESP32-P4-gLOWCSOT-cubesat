#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Keep raw and physics populations separate. On saturation retain raw evidence,
// flag the whole interval, and do not contaminate the valid-physics totals.
static inline bool count_totals_accumulate(uint64_t *raw, uint64_t *physics,
        const uint32_t *counts, size_t channels, bool valid, bool *overflow)
{
    *overflow = false;
    for (size_t i = 0; i < channels; ++i)
        if (counts[i] == UINT32_MAX || UINT64_MAX - raw[i] < counts[i] ||
                (valid && UINT64_MAX - physics[i] < counts[i])) *overflow = true;
    valid = valid && !*overflow;
    for (size_t i = 0; i < channels; ++i) {
        raw[i] = UINT64_MAX - raw[i] < counts[i] ? UINT64_MAX : raw[i] + counts[i];
        if (valid) physics[i] += counts[i];
    }
    return valid;
}
