#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define RECORD_CSV_SIZE 4096
#define RECORD_SCHEMA 5
#define HARDWARE_REV "v2.2"
#define HARDWARE_SHA "677fb1b10999e2f20cf2f02e3deb845203625162"
enum { Q_DAC=1, Q_TEMP=2, Q_HV=4, Q_WIFI=8, Q_TIME=16, Q_OVERFLOW=32 };
typedef struct {
    uint16_t dac_start[8], dac[8];
    uint8_t hv_start, hv;
    bool ble_connected, ble_advertising, wifi_on;
    uint32_t quality, notifications;
    uint64_t totals[7], physics_totals[7], physics_exposure_ms;
    int64_t start_ms, sync_uptime_ms, sync_epoch_ms, sync_offset_ms;
    int32_t sync_uncertainty_ms;
    uint8_t sync_source;
    double humidity;
} record_audit_t;
extern uint64_t audit_boot_id;
extern char audit_device_id[13];
extern bool audit_ble_connected, audit_ble_advertising;
extern uint32_t audit_notifications;
void audit_init(void);
void audit_begin(int64_t start_ms); // caller holds state mux
void audit_capture(record_audit_t *out); // caller holds state mux
void audit_mark_locked(unsigned flag); // caller holds state mux
void audit_event(unsigned flag, const char *kind, int channel, int value);
void audit_clock(int source, int64_t before_ms, int64_t target_ms, int uncertainty_ms);
void audit_csv_header(char *out, size_t size);
void audit_csv_record(const record_audit_t *a, char *out, size_t size);
bool audit_store_record(uint32_t sequence, const char *row); // SD mutex held
struct cJSON;
bool audit_command(struct cJSON *request, struct cJSON *response);
