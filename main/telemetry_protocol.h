#pragma once
#include <stdint.h>
#include <stddef.h>
#define TELEMETRY_SIZE 160
#define TELEMETRY_VERSION 4
#define TELEMETRY_SAMPLE_VALID 1
#define TELEMETRY_PHYSICS_VALID 2
#define TELEMETRY_ENV_VALID 4
#define TELEMETRY_CLOCK_SET 8
#define TELEMETRY_WIFI_OFF 16
#define TELEMETRY_HV_SETTLED 32
#define TELEMETRY_PHYSICS_READY 64
#define TELEMETRY_TRANSITION 128
// Status: bit0 FPGA ready, bit1 SD mounted, bit2 last count write/flush succeeded.
typedef struct {
    uint8_t flags, hv;
    uint16_t status;
    uint64_t boot_id, sample_uptime_ms, physics_exposure_ms, epoch, uptime_ms;
    uint32_t sequence, interval_ms;
    int32_t temp_millic;
    uint32_t pressure_pa;
    uint64_t totals[7];
    uint32_t counts[7];
    uint8_t device_id[6];
} telemetry_sample_t;
void telemetry_encode(const telemetry_sample_t *s, uint8_t out[TELEMETRY_SIZE]);
