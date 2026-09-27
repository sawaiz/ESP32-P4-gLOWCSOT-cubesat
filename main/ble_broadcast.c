#include "app_common.h"
#include "telemetry_protocol.h"
#include "ble_control.h"
_Static_assert(COUNT_CHANNELS == 7, "MuonP4 telemetry requires the seven-channel readout profile");
#include "esp_random.h"
#include "esp_mac.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

void ble_store_config_init(void);
// 73B47A10-6F6E-4D75-9A50-4D756F6E5034, characteristic ...7A11...
static const ble_uuid128_t service_uuid = BLE_UUID128_INIT(
    0x34,0x50,0x6e,0x6f,0x75,0x4d,0x50,0x9a,0x75,0x4d,0x6e,0x6f,0x10,0x7a,0xb4,0x73);
static const ble_uuid128_t sample_uuid = BLE_UUID128_INIT(
    0x34,0x50,0x6e,0x6f,0x75,0x4d,0x50,0x9a,0x75,0x4d,0x6e,0x6f,0x11,0x7a,0xb4,0x73);
static const ble_uuid128_t control_uuid = BLE_UUID128_INIT(
    0x34,0x50,0x6e,0x6f,0x75,0x4d,0x50,0x9a,0x75,0x4d,0x6e,0x6f,0x12,0x7a,0xb4,0x73);
static const ble_uuid128_t response_uuid = BLE_UUID128_INIT(
    0x34,0x50,0x6e,0x6f,0x75,0x4d,0x50,0x9a,0x75,0x4d,0x6e,0x6f,0x13,0x7a,0xb4,0x73);
static uint16_t control_response_handle;
static uint8_t own_addr_type, device_id[6];

static bool ready, subscribed, encrypted;
static int64_t connected_at_ms;
static uint16_t connection = BLE_HS_CONN_HANDLE_NONE, value_handle;
static portMUX_TYPE ble_mux = portMUX_INITIALIZER_UNLOCKED;

static telemetry_sample_t current_sample(void)
{
    telemetry_sample_t t = {.boot_id=audit_boot_id};
    memcpy(t.device_id, device_id, sizeof(device_id));
    portENTER_CRITICAL(&s_state_mux);
    count_record_t r = s_latest_minute;
    if (s_latest_minute_valid) t.flags |= TELEMETRY_SAMPLE_VALID;
    if (r.physics_valid) t.flags |= TELEMETRY_PHYSICS_VALID;
    if (r.env_valid) t.flags |= TELEMETRY_ENV_VALID;
    if (r.time_set) t.flags |= TELEMETRY_CLOCK_SET;
    if (s_wifi_stopped) t.flags |= TELEMETRY_WIFI_OFF;
    if (s_counting_enabled && s_hv_byte && !s_hv_settle_until_ms) t.flags |= TELEMETRY_HV_SETTLED;
    if (physics_ready_locked()) t.flags |= TELEMETRY_PHYSICS_READY;
    if (s_shutdown_pending) t.flags |= TELEMETRY_TRANSITION;
    t.status = (s_fpga_ok ? 1 : 0) | (s_sd_mounted ? 2 : 0) | (s_sd_write_ok ? 4 : 0) | (ble_control_recent() ? 8 : 0);
    t.hv = s_hv_byte;
    memcpy(t.totals, s_physics_totals, sizeof(t.totals));
    t.physics_exposure_ms = s_physics_exposure_ms;
    portEXIT_CRITICAL(&s_state_mux);
    t.uptime_ms=esp_timer_get_time()/1000;
    t.sequence=r.sequence; t.sample_uptime_ms=r.uptime_ms; t.epoch=r.epoch;
    t.interval_ms=r.interval_ms;
    t.temp_millic=r.env_valid ? (int32_t)lround(r.temp_c*1000) : INT32_MIN;
    t.pressure_pa=r.env_valid ? (uint32_t)lround(r.pressure_hpa*100) : UINT32_MAX;
    memcpy(t.counts,r.counts,sizeof(t.counts));
    return t;
}

static int read_sample(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg)
{
    (void)conn; (void)attr; (void)arg;
    if (ctx->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_READ_NOT_PERMITTED;
    // NimBLE callbacks are serialized; only one connection is allowed. Hold
    // one coherent snapshot across ATT Read Blob requests at small MTUs.
    static uint8_t data[TELEMETRY_SIZE];
    if(ctx->offset==0) {
        telemetry_sample_t t=current_sample();
        telemetry_encode(&t,data);
    }
    return os_mbuf_append(ctx->om,data,sizeof(data)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}
static const struct ble_gatt_svc_def services[] = {
    {.type=BLE_GATT_SVC_TYPE_PRIMARY, .uuid=&service_uuid.u,
     .characteristics=(struct ble_gatt_chr_def[]) {
        {.uuid=&sample_uuid.u, .access_cb=read_sample,
         .flags=BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, .val_handle=&value_handle},
        {.uuid=&control_uuid.u,.access_cb=ble_control_access,
         .flags=BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC},
        {.uuid=&response_uuid.u,.access_cb=ble_control_access,
         .flags=BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ_ENC,.val_handle=&control_response_handle},
        {0}}}, {0}
};

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type == BLE_GAP_EVENT_CONNECT && event->connect.status == 0) {
        portENTER_CRITICAL(&ble_mux);
        portENTER_CRITICAL(&s_state_mux);audit_ble_connected=true;portEXIT_CRITICAL(&s_state_mux);
        connection=event->connect.conn_handle; subscribed=false; encrypted=false;
        connected_at_ms=esp_timer_get_time()/1000;
        portEXIT_CRITICAL(&ble_mux);
        // Request pairing explicitly: some CoreBluetooth centrals return an
        // encryption error on the first protected write instead of pairing.
        // Keep the initial connection fast while security is negotiated.
        int rc=ble_gap_security_initiate(event->connect.conn_handle);
        if(rc && rc!=BLE_HS_EALREADY) ESP_LOGW(TAG,"BLE security request rc=%d",rc);
    } else if (event->type == BLE_GAP_EVENT_ENC_CHANGE) {
        struct ble_gap_conn_desc desc;
        bool secure=ble_gap_conn_find(event->enc_change.conn_handle,&desc)==0 && desc.sec_state.encrypted;
        portENTER_CRITICAL(&ble_mux); encrypted=secure; portEXIT_CRITICAL(&ble_mux);
        ESP_LOGW(TAG,"BLE encryption status=%d encrypted=%d",event->enc_change.status,secure);
    } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
        ble_control_disconnected();
        portENTER_CRITICAL(&ble_mux);
        portENTER_CRITICAL(&s_state_mux);audit_ble_connected=false;portEXIT_CRITICAL(&s_state_mux);
        connection=BLE_HS_CONN_HANDLE_NONE; subscribed=false; encrypted=false;
        portEXIT_CRITICAL(&ble_mux);
    } else if (event->type == BLE_GAP_EVENT_SUBSCRIBE) {
        portENTER_CRITICAL(&ble_mux);
        if(event->subscribe.attr_handle == value_handle) subscribed=event->subscribe.cur_notify;
        portEXIT_CRITICAL(&ble_mux);
    }
    return 0;
}

static void advertise(bool connected)
{
    // Flags(3) + UUID(18) + complete local name(8) = 29 bytes. The UUID is in
    // the primary packet, so iOS background discovery needs no scan response.
    struct ble_hs_adv_fields fields={0};
    fields.flags=BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.uuids128=(ble_uuid128_t *)&service_uuid;
    fields.num_uuids128=1; fields.uuids128_is_complete=1;
    fields.name=(const uint8_t *)BLE_DEVICE_NAME; fields.name_len=strlen(BLE_DEVICE_NAME);
    fields.name_is_complete=1;
    int rc=ble_gap_adv_set_fields(&fields);
    if (rc) { ESP_LOGW(TAG,"BLE advertisement fields rc=%d",rc); return; }
    // A compact active-scan summary in SCAN_RSP; full exact cumulative
    // counts are readable over GATT. SCAN_RSP is not reliable in iOS background.
    telemetry_sample_t t=current_sample();
    uint8_t summary[29]={0xff,0xff,'M','P',4,t.flags};
    for (int i=0;i<4;i++) { summary[6+i]=t.sequence>>(8*i); summary[10+i]=(uint32_t)t.temp_millic>>(8*i); }
    for (int i=0;i<3;i++) summary[14+i]=t.pressure_pa>>(8*i);
    for(int ch=0;ch<3;ch++)for(int i=0;i<4;i++)summary[17+ch*4+i]=t.counts[ch]>>(8*i);
    struct ble_hs_adv_fields scan={.mfg_data=summary,.mfg_data_len=sizeof(summary)};
    rc=ble_gap_adv_rsp_set_fields(&scan);
    if (rc) { ESP_LOGW(TAG,"BLE scan response rc=%d",rc); return; }
    struct ble_gap_adv_params params={.conn_mode=connected ? BLE_GAP_CONN_MODE_NON : BLE_GAP_CONN_MODE_UND,.disc_mode=BLE_GAP_DISC_MODE_GEN,
        .itvl_min=BLE_GAP_ADV_ITVL_MS(BLE_BURST_INTERVAL_MS),.itvl_max=BLE_GAP_ADV_ITVL_MS(BLE_BURST_INTERVAL_MS)};
    rc=ble_gap_adv_start(own_addr_type,NULL,BLE_BURST_DURATION_MS,&params,gap_event,NULL);
    if(!rc){portENTER_CRITICAL(&s_state_mux);audit_ble_advertising=true;portEXIT_CRITICAL(&s_state_mux);}
    if (rc) ESP_LOGW(TAG,"BLE advertisement start rc=%d",rc);
}

static void payload_task(void *arg)
{
    (void)arg;
    int64_t next_adv=0, next_update=0;
    bool fast=false;
    uint16_t last_conn=BLE_HS_CONN_HANDLE_NONE;
    while (true) {
        portENTER_CRITICAL(&ble_mux);
        bool synced=ready, notify=subscribed, secure=encrypted;
        int64_t connected_at=connected_at_ms;
        uint16_t conn=connection;
        portEXIT_CRITICAL(&ble_mux);
        int64_t now=esp_timer_get_time()/1000;
        bool want_fast=ble_control_recent() || (!secure && conn!=BLE_HS_CONN_HANDLE_NONE && now-connected_at<30000);
        if(synced && conn!=BLE_HS_CONN_HANDLE_NONE && (want_fast!=fast || conn!=last_conn)) {
            struct ble_gap_upd_params p={.itvl_min=want_fast?12:756,.itvl_max=want_fast?24:768,
                .latency=want_fast?0:1,.supervision_timeout=600};
            int rc=ble_gap_update_params(conn,&p);
            if(!rc){fast=want_fast;last_conn=conn;}
        }
        if(conn==BLE_HS_CONN_HANDLE_NONE){last_conn=conn;fast=false;}
        if (synced && now>=next_adv) {
            advertise(conn != BLE_HS_CONN_HANDLE_NONE); next_adv=now+BLE_BROADCAST_PERIOD_MS;
        }
        if (synced && notify && conn != BLE_HS_CONN_HANDLE_NONE && now>=next_update) {
            // A short notification is a change/heartbeat token, never a
            // truncated sample. Phone reads the full 160-byte characteristic.
            telemetry_sample_t t=current_sample();
            uint8_t token[8]={'M','N',4,t.flags};
            for (int i=0;i<4;++i) token[4+i]=t.sequence>>(8*i);
            struct os_mbuf *om=ble_hs_mbuf_from_flat(token,sizeof(token));
            if (om) {
                int rc=ble_gatts_notify_custom(conn,value_handle,om);
                if(!rc){portENTER_CRITICAL(&s_state_mux);audit_notifications++;portEXIT_CRITICAL(&s_state_mux);}
                if (rc) ESP_LOGW(TAG,"BLE notify rc=%d",rc);
            }
            next_update=now+BLE_BROADCAST_PERIOD_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
static void on_reset(int reason) {
    ble_control_disconnected();
    portENTER_CRITICAL(&ble_mux);
    ready=false; connection=BLE_HS_CONN_HANDLE_NONE; subscribed=false; encrypted=false;
    portEXIT_CRITICAL(&ble_mux);
    ESP_LOGW(TAG,"BLE host reset reason=%d",reason);
}
static void on_sync(void) {
    int rc=ble_hs_util_ensure_addr(0);
    if (!rc) rc=ble_hs_id_infer_auto(0,&own_addr_type);
    portENTER_CRITICAL(&ble_mux); ready=rc==0; portEXIT_CRITICAL(&ble_mux);
    if (rc) ESP_LOGW(TAG,"BLE address setup rc=%d",rc);
}
static void host_task(void *arg) { (void)arg; nimble_port_run(); nimble_port_freertos_deinit(); }
esp_err_t init_ble_broadcast(void)
{

    esp_read_mac(device_id,ESP_MAC_BASE);
    esp_err_t ret=nimble_port_init();
    if (ret!=ESP_OK) return ret;
    ble_hs_cfg.reset_cb=on_reset; ble_hs_cfg.sync_cb=on_sync;
    ble_hs_cfg.store_status_cb=ble_store_util_status_rr;
    ble_svc_gap_init(); ble_svc_gatt_init();
    if (ble_svc_gap_device_name_set(BLE_DEVICE_NAME) || ble_gatts_count_cfg(services) || ble_gatts_add_svcs(services))
        return ESP_FAIL;
    ble_hs_cfg.sm_io_cap=BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_bonding=1;
    ble_hs_cfg.sm_sc=1;
    ret=ble_control_init(&control_response_handle);
    if(ret!=ESP_OK)return ret;
    ble_att_set_preferred_mtu(247);
    ble_store_config_init(); nimble_port_freertos_init(host_task);
    return xTaskCreatePinnedToCore(payload_task,"ble_payload",4096,NULL,4,NULL,1)==pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
