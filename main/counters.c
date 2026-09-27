#include "app_common.h"
#include "count_totals.h"
#include "measurement_window.h"

// GPIO interrupt hook for the FPGA counter outputs. It only increments a RAM
// counter so pulse handling stays fast and deterministic.
static void IRAM_ATTR count_isr(void *arg)
{
    // Keep this ISR boring: one bounds check and one increment. Anything slower
    // belongs in the minute task so Wi-Fi/web traffic cannot stretch dead time.
    uintptr_t index = (uintptr_t)arg;
    if (s_counting_enabled && index < COUNT_CHANNELS) {
        portENTER_CRITICAL_ISR(&s_count_mux);
        if(s_counts[index]!=UINT32_MAX)s_counts[index]++;
        portEXIT_CRITICAL_ISR(&s_count_mux);
    }
}


// Configure the FPGA output pins as rising-edge interrupt inputs and attach the
// small ISR used for live counting.
esp_err_t init_counters(void)
{
    uint64_t mask = 0;
    for (size_t i = 0; i < COUNT_CHANNELS; i++) {
        mask |= (1ULL << s_count_pins[i]);
    }
    gpio_config_t inputs = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&inputs), TAG, "configure counter inputs");
    esp_err_t ret = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(ret, TAG, "install GPIO ISR service");
    }
    for (size_t i = 0; i < COUNT_CHANNELS; i++) {
        ESP_RETURN_ON_ERROR(gpio_isr_handler_add(s_count_pins[i], count_isr, (void *)i), TAG, "add counter ISR");
    }
    ESP_LOGI(TAG, "counter inputs armed");
    return ESP_OK;
}


// Copy the live interrupt counters into a caller buffer, optionally clearing
// them for the next integration window.
void snapshot_counts(uint32_t out[COUNT_CHANNELS], bool reset)
{
    // The reset option gives the one-minute task an atomic "read and clear"
    // operation while the web page can still take non-destructive snapshots.
    portENTER_CRITICAL(&s_count_mux);
    for (size_t i = 0; i < COUNT_CHANNELS; i++) {
        out[i] = s_counts[i];
        if (reset) {
            s_counts[i] = 0;
        }
    }
    portEXIT_CRITICAL(&s_count_mux);
}

// Caller holds s_state_mux. This describes the operating state, not a
// calibration guarantee; completed records additionally require a full minute.
bool physics_ready_locked(void)
{
    return s_power_save_mode && !s_shutdown_pending && s_wifi_stopped &&
           s_fpga_ok && s_hv_byte != 0 && s_counting_enabled &&
           s_hv_settle_until_ms == 0;
}

static bool append_log_record(count_record_t *record, const measurement_window_t *window)
{
    portENTER_CRITICAL(&s_state_mux);
    if (!s_counting_enabled || s_measurement_generation != window->generation ||
        physics_ready_locked() != window->physics) {
        portEXIT_CRITICAL(&s_state_mux);
        return false;
    }
    record->physics_valid = measurement_physics_valid(window, record->uptime_ms,
        s_measurement_generation, physics_ready_locked());
    record->sequence = s_latest_minute.sequence + 1;
    record->time_set = s_time_set;
    record->wifi_off = s_wifi_stopped;
    record->hv_settled = s_counting_enabled && s_hv_byte != 0 && s_hv_settle_until_ms == 0;
    record->env_valid = s_bme280_latest.valid &&
        record->uptime_ms - s_bme280_sample_uptime_ms <= 3 * BME280_SAMPLE_MS &&
        isfinite(s_bme280_latest.temp_c) && isfinite(s_bme280_latest.pressure_hpa) &&
        s_bme280_latest.temp_c > -327.68 && s_bme280_latest.temp_c <= 327.67 &&
        s_bme280_latest.pressure_hpa > 0 && s_bme280_latest.pressure_hpa < 167772.15;
    record->temp_c = record->env_valid ? s_bme280_latest.temp_c : 0;
    record->pressure_hpa = record->env_valid ? s_bme280_latest.pressure_hpa : 0;
    bool overflow;
    record->physics_valid = count_totals_accumulate(s_totals, s_physics_totals,
        record->counts, COUNT_CHANNELS, record->physics_valid, &overflow);
    memcpy(s_last_counts, record->counts, sizeof(s_last_counts));
    if (record->physics_valid) s_physics_exposure_ms += record->interval_ms;
    audit_capture(&record->audit);
    if(overflow)record->audit.quality|=Q_OVERFLOW;
    if(!record->env_valid)record->audit.humidity=NAN;
    s_latest_minute = *record;
    s_latest_minute_valid = true;
    s_log[s_log_head] = *record;
    s_log_head = (s_log_head + 1) % LOG_RECORDS;
    if (s_log_count < LOG_RECORDS) {
        s_log_count++;
    }
    audit_begin(record->uptime_ms);
    portEXIT_CRITICAL(&s_state_mux);
    sd_append_record(record);
    return true;
}

// Console inspection must not reset the minute integration or publish a
// partial record as a full minute.
void print_live_counts(void)
{
    uint32_t snapshot[COUNT_CHANNELS];
    snapshot_counts(snapshot, false);
    printf("live_counts,uptime_ms=%" PRId64, esp_timer_get_time() / 1000);
    for (size_t i = 0; i < COUNT_CHANNELS; i++) {
        printf(",%s=%" PRIu32, s_count_names[i], snapshot[i]);
    }
    printf("\n");
    fflush(stdout);
}

void counter_task(void *arg)
{
    (void)arg;
    measurement_window_t window = {0};
    while (true) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        portENTER_CRITICAL(&s_state_mux);
        uint32_t generation = s_measurement_generation;
        bool enabled = s_counting_enabled;
        bool ready = physics_ready_locked();
        portEXIT_CRITICAL(&s_state_mux);
        window_action_t action = measurement_window_step(&window, now_ms, generation, enabled, ready);
        if (action == WINDOW_RESET) {
            clear_live_counts();
            portENTER_CRITICAL(&s_state_mux);audit_begin(now_ms);portEXIT_CRITICAL(&s_state_mux);
        } else if (action == WINDOW_COMPLETE) {
            count_record_t record = {
                .uptime_ms = now_ms,
                .epoch = time(NULL),
                .interval_ms = (uint32_t)(now_ms - window.start_ms),
            };
            snapshot_counts(record.counts, true);
            if (append_log_record(&record, &window)) {
                window.start_ms = now_ms;
                if (!record.physics_valid) {
                    ESP_LOGW(TAG, "minute %" PRIu32 " is setup/non-physics data", record.sequence);
                }
            } else {
                window.active = false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
