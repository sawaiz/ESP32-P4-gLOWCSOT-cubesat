#include "app_common.h"
#include "audit.h"
#include "identity.h"
#include "cJSON.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "mbedtls/base64.h"
#include <stdarg.h>
uint64_t audit_boot_id;
char audit_device_id[13];
bool audit_ble_connected, audit_ble_advertising;
uint32_t audit_notifications;
static uint32_t pending_quality, begin_notifications;
static record_audit_t begin, clock_state;
void audit_init(void) {
    esp_fill_random(&audit_boot_id,sizeof(audit_boot_id));
    uint8_t mac[6];esp_read_mac(mac,ESP_MAC_BASE);
    snprintf(audit_device_id,sizeof(audit_device_id),"%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    clock_state.sync_uncertainty_ms=-1;
}
void audit_begin(int64_t start_ms) {
    memcpy(begin.dac_start,s_dac_codes,sizeof(begin.dac_start));begin.hv_start=s_hv_byte;
    begin.start_ms=start_ms;pending_quality=0;begin_notifications=audit_notifications;audit_ble_advertising=false;
}
void audit_capture(record_audit_t *a) {
    *a=begin;memcpy(a->dac,s_dac_codes,sizeof(a->dac));a->hv=s_hv_byte;
    a->quality=pending_quality;a->ble_connected=audit_ble_connected;a->ble_advertising=audit_ble_advertising;
    a->notifications=audit_notifications-begin_notifications;a->wifi_on=!s_wifi_stopped;
    memcpy(a->totals,s_totals,sizeof(a->totals));memcpy(a->physics_totals,s_physics_totals,sizeof(a->physics_totals));
    a->physics_exposure_ms=s_physics_exposure_ms;
    a->humidity=s_bme280_latest.valid?s_bme280_latest.humidity_pct:NAN;
    a->sync_source=clock_state.sync_source;a->sync_epoch_ms=clock_state.sync_epoch_ms;
    a->sync_uptime_ms=clock_state.sync_uptime_ms;a->sync_offset_ms=clock_state.sync_offset_ms;
    a->sync_uncertainty_ms=clock_state.sync_uncertainty_ms;
}
static bool flush(FILE *f) { bool ok=fflush(f)==0; if(fsync(fileno(f)))ok=false; if(fclose(f))ok=false;return ok; }
void audit_mark_locked(unsigned flag) { pending_quality |= flag; }
void audit_event(unsigned flag,const char *kind,int channel,int value) {
    int64_t mono=esp_timer_get_time()/1000;
    portENTER_CRITICAL(&s_state_mux);pending_quality|=flag;portEXIT_CRITICAL(&s_state_mux);
    if(!s_sd_mutex)return;
    char path[100];snprintf(path,sizeof(path),SD_MOUNT_POINT "/events_%"PRIu64".csv",audit_boot_id);
    xSemaphoreTake(s_sd_mutex,portMAX_DELAY);
    if(s_sd_mounted){FILE *f=fopen(path,"a");if(f){if(ftell(f)==0)fputs("uptime_ms,epoch,event,channel,value\n",f);
        fprintf(f,"%"PRId64",%lld,%s,%d,%d\n",mono,(long long)time(NULL),kind,channel,value);flush(f);}}
    xSemaphoreGive(s_sd_mutex);
}
void audit_clock(int source,int64_t before_ms,int64_t target_ms,int uncertainty_ms) {
    portENTER_CRITICAL(&s_state_mux);
    clock_state.sync_source=source;clock_state.sync_epoch_ms=target_ms;
    clock_state.sync_uptime_ms=esp_timer_get_time()/1000;
    clock_state.sync_offset_ms=before_ms>1600000000000LL?target_ms-before_ms:INT64_MIN;
    clock_state.sync_uncertainty_ms=uncertainty_ms;
    portEXIT_CRITICAL(&s_state_mux);
    audit_event(Q_TIME,"time_sync",source,uncertainty_ms);
}
static void add(char *out,size_t size,const char *fmt,...) {
    size_t n=strlen(out);if(n>=size)return;va_list ap;va_start(ap,fmt);vsnprintf(out+n,size-n,fmt,ap);va_end(ap);
}
void audit_csv_header(char *out,size_t size) {
    add(out,size,",schema,device_id,boot_id,git_sha,build_dirty,build_utc,fpga_sha256,hardware_revision,hardware_sha,fpga_version,coincidence_semantics,fourfold,hv_measured_v,start_uptime_ms,humidity_pct");
    for(int i=0;i<8;i++)add(out,size,",dac%d_start",i);
    for(int i=0;i<8;i++)add(out,size,",dac%d",i);
    add(out,size,",hv_start,hv_byte,hv_enabled,wifi_on,ble_advertising,ble_connected,ble_notifications,quality_flags,sync_source,sync_epoch_ms,sync_uptime_ms,clock_offset_ms,clock_uncertainty_ms,physics_exposure_ms");
    for(int i=0;i<7;i++)add(out,size,",total%d,physics_total%d,rate%d",i,i,i);
    add(out,size,"\n");
}
void audit_csv_record(const record_audit_t *a,char *out,size_t size) {
    add(out,size,",5,%s,%"PRIu64",%s,%d,%s,%s,v2.2,%s,unknown,unverified,,,",audit_device_id,audit_boot_id,BUILD_GIT_SHA,BUILD_DIRTY,BUILD_UTC,FPGA_SHA,HARDWARE_SHA);
    add(out,size,"%"PRId64",",a->start_ms);if(isfinite(a->humidity))add(out,size,"%.3f",a->humidity);
    for(int i=0;i<8;i++)add(out,size,",%u",a->dac_start[i]);
    for(int i=0;i<8;i++)add(out,size,",%u",a->dac[i]);
    add(out,size,",%u,%u,%d,%d,%d,%d,%"PRIu32",%"PRIu32",%u,%"PRId64",%"PRId64",",a->hv_start,a->hv,a->hv!=0,a->wifi_on,a->ble_advertising,a->ble_connected,a->notifications,a->quality,a->sync_source,a->sync_epoch_ms,a->sync_uptime_ms);
    if(a->sync_source&&a->sync_offset_ms!=INT64_MIN)add(out,size,"%"PRId64,a->sync_offset_ms);
    add(out,size,",");if(a->sync_uncertainty_ms>=0)add(out,size,"%"PRId32,a->sync_uncertainty_ms);
    add(out,size,",%"PRIu64,a->physics_exposure_ms);
}
bool audit_store_record(uint32_t seq,const char *row) {
    if(!seq||seq>INT32_MAX/8||!row||!strchr(row,'\n')||strlen(row)>=RECORD_CSV_SIZE)return false;
    char path[100];snprintf(path,sizeof(path),SD_MOUNT_POINT "/records_%"PRIu64".csv",audit_boot_id);
    FILE *f=fopen(path,"a");if(!f)return false;
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return false;}
    if(ftell(f)==0){char header[RECORD_CSV_SIZE]="";count_csv_header(header,sizeof(header));if(fputs(header,f)<0){fclose(f);return false;}}
    long offset=ftell(f);size_t length=strlen(row);
    bool ok=fputs(row,f)>=0;ok=flush(f)&&ok;if(!ok||offset<0)return false;
    snprintf(path,sizeof(path),SD_MOUNT_POINT "/records_%"PRIu64".idx",audit_boot_id);
    f=fopen(path,"r+b");if(!f)f=fopen(path,"w+b");if(!f)return false;
    uint32_t entry[2]={(uint32_t)offset,(uint32_t)length};
    ok=fseek(f,(long)(seq-1)*sizeof(entry),SEEK_SET)==0&&fwrite(entry,sizeof(entry),1,f)==1;
    return flush(f)&&ok;
}
static void fail(cJSON *r,const char *s){cJSON_ReplaceItemInObject(r,"ok",cJSON_CreateBool(false));cJSON_AddStringToObject(r,"error",s);}
static const char *string(cJSON*j,const char*k){cJSON*v=cJSON_GetObjectItem(j,k);return cJSON_IsString(v)?v->valuestring:"";}
static bool integer(cJSON*j,const char*k,double lo,double hi,double*out){cJSON*v=cJSON_GetObjectItem(j,k);if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||floor(v->valuedouble)!=v->valuedouble||v->valuedouble<lo||v->valuedouble>hi)return false;*out=v->valuedouble;return true;}
static bool boot_valid(const char*b){size_t n=strlen(b);if(!n||n>20)return false;for(size_t i=0;i<n;i++)if(!isdigit((unsigned char)b[i]))return false;return true;}
bool audit_command(cJSON*j,cJSON*r) {
    const char *op=string(j,"op");
    if(!strcmp(op,"clock")) {
        struct timeval now;gettimeofday(&now,NULL);
        cJSON_AddNumberToObject(r,"utc_ms",(double)now.tv_sec*1000+now.tv_usec/1000);
        cJSON_AddNumberToObject(r,"uptime_ms",esp_timer_get_time()/1000);
        cJSON_AddBoolToObject(r,"time_set",s_time_set);return true;
    }
    if(!strcmp(op,"hello")) {
        cJSON_AddNumberToObject(r,"schema",5);cJSON_AddNumberToObject(r,"wire",4);
        cJSON_AddStringToObject(r,"firmware",BUILD_GIT_SHA);cJSON_AddBoolToObject(r,"dirty",BUILD_DIRTY);
        cJSON_AddStringToObject(r,"build_utc",BUILD_UTC);cJSON_AddStringToObject(r,"fpga_sha256",FPGA_SHA);
        cJSON_AddStringToObject(r,"hardware","v2.2");cJSON_AddStringToObject(r,"device",audit_device_id);
        char boot[24];snprintf(boot,sizeof(boot),"%"PRIu64,audit_boot_id);cJSON_AddStringToObject(r,"boot",boot);
        cJSON_AddNumberToObject(r,"last",s_latest_minute.sequence);
        const char *app=string(j,"app");if(strlen(app)<=32)cJSON_AddStringToObject(r,"app",app);
        return true;
    }
    if(strcmp(op,"record")&&strcmp(op,"location")&&strcmp(op,"journal"))return false;
    const char*b=string(j,"boot");double seq,offset=0;
    if(!boot_valid(b)||!integer(j,"seq",0,INT32_MAX/8,&seq)){fail(r,"invalid record key");return true;}
    if(!s_sd_mutex||!s_sd_mounted){fail(r,"SD unavailable");return true;}
    xSemaphoreTake(s_sd_mutex,portMAX_DELAY);
    char path[120];snprintf(path,sizeof(path),SD_MOUNT_POINT "/records_%s.idx",b);
    uint32_t entry[2]={0};FILE*f=fopen(path,"rb");
    if(!strcmp(op,"journal")) {
        long bytes=-1;if(f&&fseek(f,0,SEEK_END)==0)bytes=ftell(f);
        if(f)fclose(f);
        if(bytes<0)fail(r,"journal unavailable");
        else cJSON_AddNumberToObject(r,"last",bytes/sizeof(entry));
        goto done;
    }
    if(seq>0&&f){if(fseek(f,(long)(seq-1)*sizeof(entry),SEEK_SET)||fread(entry,sizeof(entry),1,f)!=1)entry[1]=0;}
    if(f)fclose(f);
    if(!strcmp(op,"location")) {
        double lat,lon,fix,acc,alt,source;
        const char *rev=string(j,"rev");bool valid=strlen(rev)==32;
        for(size_t i=0;valid&&i<32;i++)valid=isxdigit((unsigned char)rev[i]);
        if(!entry[1]||!valid||!integer(j,"lat",-900000000,900000000,&lat)||!integer(j,"lon",-1800000000,1800000000,&lon)||
           !integer(j,"fix",0,4102444800000.0,&fix)||!integer(j,"acc",-1,100000000,&acc)||!integer(j,"alt",-100000000,100000000,&alt)||!integer(j,"src",0,1,&source)) {
            fail(r,"invalid location or record absent");goto done;
        }
        snprintf(path,sizeof(path),SD_MOUNT_POINT "/locations_%s.csv",b);
        // A revision is immutable. Reject conflicting replays and remove only an
        // incomplete trailing write before appending the next complete observation.
        char expected[320];snprintf(expected,sizeof(expected),"%s,%.0f,%s,%.0f,%.0f,%.0f,%.0f,%.0f,%s\n",b,seq,rev,lat,lon,fix,acc,alt,source==0?"gps":"manual_stationary");
        f=fopen(path,"r+");char line[320];bool found=false,conflict=false;long complete=0;
        if(f){
            while(fgets(line,sizeof(line),f)) {
                if(!strchr(line,'\n'))break;
                complete=ftell(f);
                if(strstr(line,rev)){found=true;conflict=strcmp(line,expected)!=0;break;}
            }
            if(!found&&ftruncate(fileno(f),complete)!=0){fclose(f);fail(r,"location repair failed");goto done;}
            if(found&&!conflict){if(!flush(f)){fail(r,"location flush failed");goto done;}}
            else fclose(f);
        }
        if(conflict){fail(r,"location revision conflict");goto done;}
        if(!found){f=fopen(path,"a");if(!f){fail(r,"location open failed");goto done;}
            bool ok=true;
            if(ftell(f)==0)ok=fputs("boot_id,sequence,revision,latitude_e7,longitude_e7,fix_epoch_ms,h_accuracy_cm,altitude_cm,location_source\n",f)>=0;
            ok=fputs(expected,f)>=0&&ok;
            if(!flush(f)||!ok){fail(r,"location write failed");goto done;}}
        cJSON_AddStringToObject(r,"rev",rev);goto done;
    }
    if(!integer(j,"offset",0,RECORD_CSV_SIZE,&offset)){fail(r,"invalid offset");goto done;}
    char *row=calloc(1,RECORD_CSV_SIZE);if(!row){fail(r,"no memory");goto done;}
    if(seq==0){
        snprintf(path,sizeof(path),SD_MOUNT_POINT "/records_%s.csv",b);f=fopen(path,"rb");
        bool ok=f&&fgets(row,RECORD_CSV_SIZE,f)&&strchr(row,'\n');if(f)fclose(f);
        if(!ok){free(row);fail(r,"journal unavailable");goto done;}
    }
    else {
        if(!entry[1]||entry[1]>=RECORD_CSV_SIZE){free(row);fail(r,"record unavailable");goto done;}
        snprintf(path,sizeof(path),SD_MOUNT_POINT "/records_%s.csv",b);f=fopen(path,"rb");
        bool ok=f&&fseek(f,entry[0],SEEK_SET)==0&&fread(row,1,entry[1],f)==entry[1];if(f)fclose(f);
        if(!ok||strlen(row)!=entry[1]||row[entry[1]-1]!='\n'){free(row);fail(r,"record read failed");goto done;}
    }
    size_t length=strlen(row);if(offset>length){free(row);fail(r,"invalid offset");goto done;}
    uint32_t hash=2166136261U;for(size_t i=0;i<length;i++)hash=(hash^(uint8_t)row[i])*16777619U;
    size_t n=length-(size_t)offset;if(n>200)n=200;unsigned char encoded[269];size_t out;
    mbedtls_base64_encode(encoded,sizeof(encoded),&out,(unsigned char*)row+(size_t)offset,n);encoded[out]=0;
    cJSON_AddStringToObject(r,"data",(char*)encoded);cJSON_AddNumberToObject(r,"next",offset+n);
    cJSON_AddNumberToObject(r,"length",length);cJSON_AddNumberToObject(r,"hash",hash);cJSON_AddBoolToObject(r,"eof",offset+n==length);free(row);
 done: xSemaphoreGive(s_sd_mutex);return true;
}
