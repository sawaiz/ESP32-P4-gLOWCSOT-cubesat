#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <inttypes.h>
#include <ctype.h>
#include <sys/time.h>
#include <unistd.h>
#include <limits.h>
#include "audit.h"
#define COUNT_CHANNELS 7
#define SD_MOUNT_POINT "."
#define portMAX_DELAY 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define xSemaphoreTake(x,y) ((void)(x))
#define xSemaphoreGive(x) ((void)(x))
extern int s_state_mux,s_sd_mutex;
extern bool s_sd_mounted,s_wifi_stopped,s_time_set;
extern uint16_t s_dac_codes[8];
extern uint8_t s_hv_byte;
extern uint64_t s_totals[7],s_physics_totals[7],s_physics_exposure_ms;
extern struct environment { bool valid; double humidity_pct; } s_bme280_latest;
typedef struct { int64_t uptime_ms;time_t epoch;uint32_t counts[7],sequence,interval_ms;bool physics_valid,wifi_off,hv_settled,time_set,env_valid;double temp_c,pressure_hpa;record_audit_t audit; } count_record_t;
extern count_record_t s_latest_minute;
extern const char *s_count_names[7];
int64_t esp_timer_get_time(void);
void count_csv_header(char *,size_t);
void count_csv_record(const count_record_t*,char*,size_t);
