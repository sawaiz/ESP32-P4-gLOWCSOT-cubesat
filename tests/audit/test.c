#include "shim.h"
#include "cJSON.h"
#include <assert.h>
#include "count_totals.h"
int s_state_mux,s_sd_mutex=1;
bool s_sd_mounted=true,s_wifi_stopped=true,s_time_set=true;
uint16_t s_dac_codes[8]={753,753,753,753,112,112,112,112};
uint8_t s_hv_byte=234;
uint64_t s_totals[7],s_physics_totals[7],s_physics_exposure_ms;
struct environment s_bme280_latest={true,40.5};
count_record_t s_latest_minute;
const char*s_count_names[7]={"ch01_p13","ch02_p12","ch12_p11","ch012_p22","gpio6_p31","gpio5_p29","gpio16_p36"};
int64_t esp_timer_get_time(void){return 120000000;}
void esp_fill_random(void*p,size_t n){memset(p,1,n);}
int esp_read_mac(uint8_t*p,int type){(void)type;memset(p,2,6);return 0;}
int mbedtls_base64_encode(unsigned char*out,size_t cap,size_t*len,const unsigned char*in,size_t n){
 static const char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";size_t k=0;assert(cap>=4*((n+2)/3)+1);
 for(size_t i=0;i<n;i+=3){unsigned v=(unsigned)in[i]<<16;if(i+1<n)v|=(unsigned)in[i+1]<<8;if(i+2<n)v|=in[i+2];out[k++]=t[v>>18];out[k++]=t[(v>>12)&63];out[k++]=i+1<n?t[(v>>6)&63]:'=';out[k++]=i+2<n?t[v&63]:'=';}*len=k;return 0;
}
static cJSON*request(const char*text){cJSON*j=cJSON_Parse(text),*r=cJSON_CreateObject();assert(j);cJSON_AddBoolToObject(r,"ok",true);assert(audit_command(j,r));cJSON_Delete(j);return r;}
static void check(bool success,cJSON*r){assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"ok"))==success);cJSON_Delete(r);}
int main(void){
 uint64_t raw[2]={100,200},physics[2]={20,30};uint32_t counts[2]={3,4};bool overflow;
 assert(!count_totals_accumulate(raw,physics,counts,2,false,&overflow));
 assert(!overflow&&raw[0]==103&&physics[0]==20);
 assert(count_totals_accumulate(raw,physics,counts,2,true,&overflow));
 assert(!overflow&&raw[0]==106&&physics[0]==23&&physics[1]==34);
 raw[0]=UINT64_MAX-1;
 assert(!count_totals_accumulate(raw,physics,counts,2,true,&overflow));
 assert(overflow&&raw[0]==UINT64_MAX&&physics[0]==23&&physics[1]==34);
 audit_init();audit_boot_id=123;audit_begin(60000);s_dac_codes[0]=754;audit_event(Q_TEMP,"temp_comp",0,754);
 count_record_t row={.epoch=1790500000,.sequence=1,.interval_ms=60000,.uptime_ms=120000,.physics_valid=true,.wifi_off=true,.hv_settled=true,.time_set=true,.env_valid=true,.temp_c=24.5,.pressure_hpa=982.8};
 for(int i=0;i<7;i++){row.counts[i]=20+i;s_totals[i]=UINT64_MAX-10;s_physics_totals[i]=9007199254740993ULL;}
 audit_capture(&row.audit);assert(row.audit.dac_start[0]==753&&row.audit.dac[0]==754);assert(row.audit.quality&Q_TEMP);
 char header[RECORD_CSV_SIZE]={0},csv[RECORD_CSV_SIZE]={0};count_csv_header(header,sizeof(header));count_csv_record(&row,csv,sizeof(csv));
 unsigned a=0,b=0;for(char*p=header;*p;p++)a+=*p==',';for(char*p=csv;*p;p++)b+=*p==',';assert(a==b);assert(strstr(csv,"9007199254740993"));assert(csv[strlen(csv)-1]=='\n');
 FILE*f=fopen("fixture.csv","w");fputs(header,f);fputs(csv,f);fclose(f);assert(audit_store_record(1,csv));
 uint32_t hash=2166136261U;for(size_t i=0;i<strlen(csv);i++)hash=(hash^(uint8_t)csv[i])*16777619U;
 for(size_t offset=0;offset<strlen(csv);){
  char query[120];snprintf(query,sizeof(query),"{\"op\":\"record\",\"boot\":\"123\",\"seq\":1,\"offset\":%zu}",offset);
  cJSON*r=request(query);assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"ok")));
  size_t count=strlen(csv)-offset;if(count>200)count=200;
  unsigned char encoded[269];size_t n;mbedtls_base64_encode(encoded,sizeof(encoded),&n,(unsigned char*)csv+offset,count);encoded[n]=0;
  assert(!strcmp(cJSON_GetObjectItem(r,"data")->valuestring,(char*)encoded));
  assert(cJSON_GetObjectItem(r,"hash")->valuedouble==hash);
  assert(cJSON_GetObjectItem(r,"length")->valuedouble==strlen(csv));
  assert(cJSON_GetObjectItem(r,"next")->valuedouble==offset+count);
  assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"eof"))==(offset+count==strlen(csv)));
  char*wire=cJSON_PrintUnformatted(r);assert(strlen(wire)<490);free(wire);cJSON_Delete(r);offset+=count;
 }
 check(false,request("{\"op\":\"record\",\"boot\":\"../123\",\"seq\":1,\"offset\":0}"));
 check(false,request("{\"op\":\"record\",\"boot\":\"123\",\"seq\":2,\"offset\":0}"));
 check(false,request("{\"op\":\"record\",\"boot\":\"123\",\"seq\":1,\"offset\":4096}"));
 const char*loc="{\"op\":\"location\",\"boot\":\"123\",\"seq\":1,\"rev\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"lat\":407000000,\"lon\":-740000000,\"fix\":1790500000000,\"acc\":1000,\"alt\":2000,\"src\":0}";
 check(true,request(loc));check(true,request(loc));
 cJSON *changed=cJSON_Parse(loc);cJSON_ReplaceItemInObject(changed,"lat",cJSON_CreateNumber(407000001));char*conflict=cJSON_PrintUnformatted(changed);check(false,request(conflict));free(conflict);cJSON_Delete(changed);
 f=fopen("locations_123.csv","a");fputs("123,1,partial",f);fclose(f);
 changed=cJSON_Parse(loc);cJSON_ReplaceItemInObject(changed,"rev",cJSON_CreateString("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));char*retry=cJSON_PrintUnformatted(changed);check(true,request(retry));free(retry);cJSON_Delete(changed);
 check(true,request("{\"op\":\"journal\",\"boot\":\"123\",\"seq\":0}"));
 check(true,request("{\"op\":\"record\",\"boot\":\"123\",\"seq\":0,\"offset\":0}"));
 f=fopen("locations_123.csv","r");assert(f);int lines=0,c;while((c=fgetc(f))!=EOF)lines+=c=='\n';fclose(f);assert(lines==3);
 check(false,request("{\"op\":\"location\",\"boot\":\"123\",\"seq\":2}"));
 row.sequence=3;count_csv_record(&row,csv,sizeof(csv));assert(audit_store_record(3,csv));
 cJSON*manifest=request("{\"op\":\"journal\",\"boot\":\"123\",\"seq\":0}");assert(cJSON_GetObjectItem(manifest,"last")->valueint==3);cJSON_Delete(manifest);
 check(true,request("{\"op\":\"record\",\"boot\":\"123\",\"seq\":3,\"offset\":0}"));
 check(false,request("{\"op\":\"record\",\"boot\":\"123\",\"seq\":2,\"offset\":0}"));
 s_sd_mounted=false;check(false,request(loc));
 puts("PASS: separate raw/physics totals and saturation, revision conflicts, partial-write recovery, real firmware CSV, exact uint64 counters, indexed recovery, absent records, path validation, idempotent GPS persistence");
}
