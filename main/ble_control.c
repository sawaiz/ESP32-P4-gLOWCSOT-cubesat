#include "app_common.h"
#include "ble_control.h"
#include "cJSON.h"
#include "freertos/queue.h"
#include "host/ble_hs.h"
#include "mbedtls/base64.h"

// Work happens outside NimBLE's host callback; HV settling must not block BLE.
static QueueHandle_t jobs;
static SemaphoreHandle_t response_mutex;
static uint16_t *response_handle;
static char response[512]="{\"id\":0,\"ok\":false,\"error\":\"no command\"}";
static uint32_t session;
static volatile TickType_t last_control_tick;
static volatile bool had_control;
bool ble_control_recent(void) { return had_control && (TickType_t)(xTaskGetTickCount()-last_control_tick)<pdMS_TO_TICKS(30000); }
typedef struct { uint16_t conn; uint32_t session; char json[241]; } control_job_t;
static const char *str(cJSON *j,const char *key) { cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key); return cJSON_IsString(v)?v->valuestring:""; }
static bool number(cJSON *j,const char *key,double min,double max,double *out) {
    cJSON *v=cJSON_GetObjectItemCaseSensitive(j,key);
    if (!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<min||v->valuedouble>max||floor(v->valuedouble)!=v->valuedouble) return false;
    *out=v->valuedouble; return true;
}
static void error(cJSON *r,const char *message) { cJSON_ReplaceItemInObject(r,"ok",cJSON_CreateBool(false));cJSON_AddStringToObject(r,"error",message); }
static bool safe_name(const char *s) {
    size_t len=strlen(s); if (!len||len>SD_LOG_NAME_MAX||s[0]=='.') return false;
    for (size_t i=0;i<len;i++) if (!isalnum((unsigned char)s[i])&&s[i]!='_'&&s[i]!='-'&&s[i]!='.') return false;
    const char *dot=strrchr(s,'.'); return dot&&(!strcmp(dot,".csv")||!strcmp(dot,".log"));
}
static void add_counts(cJSON *r) {
    uint32_t live[COUNT_CHANNELS],last[COUNT_CHANNELS]; uint64_t totals[COUNT_CHANNELS];
    snapshot_counts(live,false);
    portENTER_CRITICAL(&s_state_mux);memcpy(last,s_last_counts,sizeof(last));memcpy(totals,s_totals,sizeof(totals));portEXIT_CRITICAL(&s_state_mux);
    cJSON *l=cJSON_AddArrayToObject(r,"live"),*m=cJSON_AddArrayToObject(r,"last"),*t=cJSON_AddArrayToObject(r,"total");
    for (size_t i=0;i<COUNT_CHANNELS;i++) {
        char n[24];snprintf(n,sizeof(n),"%"PRIu64,totals[i]);
        cJSON_AddItemToArray(l,cJSON_CreateNumber(live[i]));cJSON_AddItemToArray(m,cJSON_CreateNumber(last[i]));cJSON_AddItemToArray(t,cJSON_CreateString(n));
    }
}
static void execute(cJSON *j,cJSON *r) {
    const char *op=str(j,"op"); double a,b; esp_err_t ret=ESP_OK;
    if(audit_command(j,r))return;
    if (!strcmp(op,"status")) {
        portENTER_CRITICAL(&s_state_mux);
        bool keep=s_wifi_keep_on,off=s_wifi_stopped,pending=s_shutdown_pending;
        int64_t settle=s_hv_settle_until_ms? s_hv_settle_until_ms-esp_timer_get_time()/1000:0;
        uint16_t dac[8];memcpy(dac,s_dac_codes,sizeof(dac));
        portEXIT_CRITICAL(&s_state_mux);
        cJSON_AddBoolToObject(r,"keep_wifi",keep);cJSON_AddBoolToObject(r,"wifi_off",off);cJSON_AddBoolToObject(r,"transition",pending);
        cJSON_AddNumberToObject(r,"settle_ms",settle>0?settle:0);cJSON_AddNumberToObject(r,"epoch",time(NULL));
        cJSON *d=cJSON_AddArrayToObject(r,"dac");for(int i=0;i<8;i++)cJSON_AddItemToArray(d,cJSON_CreateNumber(dac[i]));
        if(s_sd_mutex){xSemaphoreTake(s_sd_mutex,portMAX_DELAY);
            cJSON_AddStringToObject(r,"label",s_run_label);cJSON_AddStringToObject(r,"log",strrchr(s_log_path,'/')?strrchr(s_log_path,'/')+1:s_log_path);
            cJSON_AddStringToObject(r,"env",strrchr(s_env_log_path,'/')?strrchr(s_env_log_path,'/')+1:s_env_log_path);
            xSemaphoreGive(s_sd_mutex);}
    } else if (!strcmp(op,"wifi_status")) {
        // Query the radio through Hosted, rather than only returning P4 flags.
        // Serialize against Wi-Fi shutdown; no radio settings are changed.
        detector_lock();
        wifi_mode_t mode=WIFI_MODE_NULL;
        wifi_config_t config={0};
        wifi_sta_list_t stations={0};
        uint8_t primary=0; wifi_second_chan_t secondary=WIFI_SECOND_CHAN_NONE;
        int8_t power=0;
        esp_err_t mode_err=esp_wifi_get_mode(&mode);
        esp_err_t config_err=esp_wifi_get_config(WIFI_IF_AP,&config);
        esp_err_t channel_err=esp_wifi_get_channel(&primary,&secondary);
        esp_err_t stations_err=esp_wifi_ap_get_sta_list(&stations);
        esp_err_t power_err=esp_wifi_get_max_tx_power(&power);
        detector_unlock();
        cJSON_AddStringToObject(r,"mode_err",esp_err_to_name(mode_err));
        cJSON_AddStringToObject(r,"config_err",esp_err_to_name(config_err));
        cJSON_AddStringToObject(r,"channel_err",esp_err_to_name(channel_err));
        cJSON_AddStringToObject(r,"stations_err",esp_err_to_name(stations_err));
        cJSON_AddStringToObject(r,"power_err",esp_err_to_name(power_err));
        if(mode_err==ESP_OK)cJSON_AddNumberToObject(r,"mode",mode);
        if(config_err==ESP_OK) {
            char ssid[33];memcpy(ssid,config.ap.ssid,32);ssid[32]=0;
            cJSON_AddStringToObject(r,"ssid",ssid);
            cJSON_AddNumberToObject(r,"hidden",config.ap.ssid_hidden);
            cJSON_AddNumberToObject(r,"beacon_ms",config.ap.beacon_interval);
            cJSON_AddNumberToObject(r,"auth",config.ap.authmode);
            cJSON_AddNumberToObject(r,"configured_channel",config.ap.channel);
        }
        if(channel_err==ESP_OK)cJSON_AddNumberToObject(r,"channel",primary);
        if(stations_err==ESP_OK)cJSON_AddNumberToObject(r,"clients",stations.num);
        if(power_err==ESP_OK)cJSON_AddNumberToObject(r,"tx_power_quarter_dbm",power);
    } else if (!strcmp(op,"counts")) { add_counts(r);
    } else if (!strcmp(op,"environment")) {
        portENTER_CRITICAL(&s_state_mux);bme280_reading_t e=s_bme280_latest;int64_t age=esp_timer_get_time()/1000-s_bme280_sample_uptime_ms;portEXIT_CRITICAL(&s_state_mux);
        cJSON_AddBoolToObject(r,"valid",e.valid&&age<=3*BME280_SAMPLE_MS);
        cJSON_AddNumberToObject(r,"temp_c",e.temp_c);cJSON_AddNumberToObject(r,"pressure_hpa",e.pressure_hpa);cJSON_AddNumberToObject(r,"humidity_pct",e.humidity_pct);cJSON_AddNumberToObject(r,"age_ms",age);
    } else if (!strcmp(op,"time")) {
        if(!number(j,"epoch",1600000000,4102444800,&a)){error(r,"invalid epoch");return;}
        struct timeval before;gettimeofday(&before,NULL);
        struct timeval tv={.tv_sec=(time_t)a}; if(settimeofday(&tv,NULL)){error(r,"clock set failed");return;}
        audit_clock(1,(int64_t)before.tv_sec*1000+before.tv_usec/1000,(int64_t)a*1000,-1);
        portENTER_CRITICAL(&s_state_mux);s_time_set=true;portEXIT_CRITICAL(&s_state_mux);
        if(s_sd_mutex){xSemaphoreTake(s_sd_mutex,portMAX_DELAY);
            if(s_run_start_epoch<=1600000000)s_run_start_epoch=(time_t)a-(esp_timer_get_time()/1000-s_run_start_uptime_ms)/1000;
            ret=sd_refresh_log_path_locked();xSemaphoreGive(s_sd_mutex);}
    } else if (!strcmp(op,"label")) {
        if(!s_sd_mutex){error(r,"SD unavailable");return;}
        char clean[RUN_LABEL_MAX+1];sanitize_run_label(str(j,"label"),clean,sizeof(clean));
        xSemaphoreTake(s_sd_mutex,portMAX_DELAY);snprintf(s_run_label,sizeof(s_run_label),"%s",clean);ret=sd_refresh_log_path_locked();xSemaphoreGive(s_sd_mutex);
    } else if (!strcmp(op,"start_physics")) { ret=request_physics_run();
    } else if (!strcmp(op,"wifi_keep")) {
        if(!number(j,"enable",0,1,&a)){error(r,"invalid enable");return;} ret=set_wifi_keep_on(a!=0);
    } else if (!strcmp(op,"hv")) {
        if(!number(j,"value",0,255,&a)){error(r,"invalid HV byte");return;}
        ret=hv_write_and_settle((uint8_t)a);
    } else if (!strcmp(op,"dac")) {
        if(!number(j,"channel",0,7,&a)||!number(j,"value",0,1023,&b)){error(r,"DAC requires channel 0..7 and 10-bit value 0..1023");return;}
        ret=dac_set_channel((uint8_t)a,(uint16_t)b);
    } else if (!strcmp(op,"dac_startup")) { ret=dac_zero_channels();
    } else if (!strcmp(op,"fpga")) { ret=detector_reinitialize();
    } else if (!strcmp(op,"files")) {
        if(!number(j,"index",0,100000,&a)){error(r,"invalid index");return;}
        if(!s_sd_mutex){error(r,"SD unavailable");return;} xSemaphoreTake(s_sd_mutex,portMAX_DELAY);
        DIR *d=opendir(SD_MOUNT_POINT); if(!d){xSemaphoreGive(s_sd_mutex);error(r,"SD unavailable");return;}
        struct dirent *e;int n=0;bool found=false;
        while((e=readdir(d))) { if(!safe_name(e->d_name))continue;
            char path[128];snprintf(path,sizeof(path),SD_MOUNT_POINT "/%.*s",SD_LOG_NAME_MAX,e->d_name);struct stat st;
            if(stat(path,&st)||!S_ISREG(st.st_mode))continue;
            if(n++!=(int)a)continue;
            cJSON_AddStringToObject(r,"name",e->d_name);cJSON_AddNumberToObject(r,"size",st.st_size);cJSON_AddNumberToObject(r,"modified",st.st_mtime);found=true;break;
        } closedir(d);xSemaphoreGive(s_sd_mutex);cJSON_AddBoolToObject(r,"eof",!found);
    } else if (!strcmp(op,"file")) {
        if(!safe_name(str(j,"name"))||!number(j,"offset",0,2147483647,&a)){error(r,"invalid file request");return;}
        if(!s_sd_mutex){error(r,"SD unavailable");return;}
        char path[128];snprintf(path,sizeof(path),SD_MOUNT_POINT "/%s",str(j,"name"));
        unsigned char bytes[240],encoded[321];size_t out=0,n=0;
        xSemaphoreTake(s_sd_mutex,portMAX_DELAY);FILE *f=fopen(path,"rb");
        if(!f){xSemaphoreGive(s_sd_mutex);error(r,"file open failed");return;}
        struct stat st={0};bool valid=fstat(fileno(f),&st)==0;
        // First response fixes a download limit so active append-only files terminate.
        double limit=valid?st.st_size:0;
        cJSON *lim=cJSON_GetObjectItemCaseSensitive(j,"limit");
        if(lim&&(!number(j,"limit",0,2147483647,&limit)||limit>st.st_size))valid=false;
        if(!valid||a>limit||fseek(f,(long)a,SEEK_SET)){fclose(f);xSemaphoreGive(s_sd_mutex);error(r,"file changed or invalid offset");return;}
        size_t wanted=(size_t)(limit-a);if(wanted>sizeof(bytes))wanted=sizeof(bytes);
        n=fread(bytes,1,wanted,f);bool ok=n==wanted&&!ferror(f);fclose(f);xSemaphoreGive(s_sd_mutex);
        if(!ok){error(r,"file read failed");return;}
        mbedtls_base64_encode(encoded,sizeof(encoded),&out,bytes,n);encoded[out]=0;
        cJSON_AddStringToObject(r,"data",(char *)encoded);cJSON_AddNumberToObject(r,"next",a+n);cJSON_AddNumberToObject(r,"limit",limit);cJSON_AddBoolToObject(r,"eof",a+n>=limit);
    } else if (!strcmp(op,"ram")) {
        count_record_t rec={0};bool found=false;uint32_t first=0,last=0;
        bool has=number(j,"sequence",1,UINT32_MAX,&a);
        portENTER_CRITICAL(&s_state_mux);
        if(s_log_count){first=s_log[(s_log_head+LOG_RECORDS-s_log_count)%LOG_RECORDS].sequence;last=s_latest_minute.sequence;}
        if(has)for(size_t i=0;i<s_log_count;i++){size_t k=(s_log_head+LOG_RECORDS-s_log_count+i)%LOG_RECORDS;if(s_log[k].sequence==(uint32_t)a){rec=s_log[k];found=true;break;}}
        portEXIT_CRITICAL(&s_state_mux);
        cJSON_AddNumberToObject(r,"first",first);cJSON_AddNumberToObject(r,"last",last);
        char csv[RECORD_CSV_SIZE];if(has&&!found){error(r,"record expired");return;}
        if(has)count_csv_record(&rec,csv,sizeof(csv));else count_csv_header(csv,sizeof(csv));
        double offset=0; cJSON *o=cJSON_GetObjectItem(j,"offset");
        if(o&&!number(j,"offset",0,RECORD_CSV_SIZE-1,&offset)){error(r,"invalid offset");return;}
        size_t length=strlen(csv);if(offset>length){error(r,"invalid offset");return;}
        char part[201];snprintf(part,sizeof(part),"%s",csv+(size_t)offset);
        cJSON_AddStringToObject(r,"csv",part);cJSON_AddNumberToObject(r,"next",offset+strlen(part));cJSON_AddBoolToObject(r,"eof",offset+strlen(part)==length);
    } else { error(r,"unknown operation");return; }
    if(ret!=ESP_OK)error(r,esp_err_to_name(ret));
}
static void worker(void *arg) {
    (void)arg;control_job_t job;
    while(xQueueReceive(jobs,&job,portMAX_DELAY)==pdTRUE) {
        if(job.session!=session)continue;
        cJSON *j=cJSON_Parse(job.json),*r=cJSON_CreateObject();double id=0;
        bool valid=j&&number(j,"id",1,UINT32_MAX,&id);
        cJSON_AddNumberToObject(r,"id",id);cJSON_AddBoolToObject(r,"ok",valid);
        if(valid)execute(j,r);else error(r,"invalid request");
        char encoded[512];if(!cJSON_PrintPreallocated(r,encoded,sizeof(encoded),false))snprintf(encoded,sizeof(encoded),"{\"id\":%.0f,\"ok\":false,\"error\":\"response too large\"}",id);
        cJSON_Delete(j);cJSON_Delete(r);
        xSemaphoreTake(response_mutex,portMAX_DELAY);snprintf(response,sizeof(response),"%s",encoded);xSemaphoreGive(response_mutex);
        if(job.session==session){uint8_t token[2]={'O','K'};struct os_mbuf *om=ble_hs_mbuf_from_flat(token,2);if(om)ble_gatts_notify_custom(job.conn,*response_handle,om);}
    }
}
int ble_control_access(uint16_t conn,uint16_t attr,struct ble_gatt_access_ctxt *ctx,void *arg) {
    (void)attr;(void)arg;
    if(ctx->op==BLE_GATT_ACCESS_OP_WRITE_CHR){
        last_control_tick=xTaskGetTickCount();had_control=true;
        control_job_t job={.conn=conn,.session=session};uint16_t n=OS_MBUF_PKTLEN(ctx->om);
        if(!jobs||n==0||n>=sizeof(job.json))return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if(ble_hs_mbuf_to_flat(ctx->om,job.json,sizeof(job.json)-1,NULL))return BLE_ATT_ERR_UNLIKELY;
        job.json[n]=0;return xQueueSend(jobs,&job,0)==pdTRUE?0:BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    if(ctx->op==BLE_GATT_ACCESS_OP_READ_CHR){
        if(!response_mutex)return BLE_ATT_ERR_UNLIKELY;
        static char read_snapshot[sizeof(response)];
        if(ctx->offset==0) {
            xSemaphoreTake(response_mutex,portMAX_DELAY);snprintf(read_snapshot,sizeof(read_snapshot),"%s",response);xSemaphoreGive(response_mutex);
        }
        int rc=os_mbuf_append(ctx->om,read_snapshot,strlen(read_snapshot));return rc?BLE_ATT_ERR_INSUFFICIENT_RES:0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}
void ble_control_disconnected(void){session++;}
esp_err_t ble_control_init(uint16_t *handle){
    response_handle=handle;response_mutex=xSemaphoreCreateMutex();jobs=xQueueCreate(2,sizeof(control_job_t));
    if(!response_mutex||!jobs)return ESP_ERR_NO_MEM;
    return xTaskCreatePinnedToCore(worker,"ble_control",16384,NULL,3,NULL,1)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
