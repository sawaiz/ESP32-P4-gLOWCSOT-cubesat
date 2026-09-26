#include "telemetry_protocol.h"
#include <string.h>
static void put(uint8_t *p, uint64_t n, size_t width) {
    for (size_t i = 0; i < width; ++i) p[i] = (uint8_t)(n >> (8 * i));
}
void telemetry_encode(const telemetry_sample_t *s, uint8_t out[TELEMETRY_SIZE]) {
    memset(out, 0, TELEMETRY_SIZE);
    out[0] = 'M'; out[1] = 'P'; out[2] = TELEMETRY_VERSION; out[3] = s->flags;
    put(out+4,s->boot_id,8); put(out+12,s->sequence,4);
    put(out+16,s->sample_uptime_ms,8); put(out+24,s->physics_exposure_ms,8);
    put(out+32,s->epoch,8); put(out+40,s->interval_ms,4);
    put(out+44,(uint32_t)s->temp_millic,4); put(out+48,s->pressure_pa,4);
    put(out+52,s->status,2); out[54]=s->hv;
    for (size_t i=0;i<7;++i) { put(out+56+8*i,s->totals[i],8); put(out+112+4*i,s->counts[i],4); }
    memcpy(out+140,s->device_id,6); put(out+146,s->uptime_ms,8);
}
