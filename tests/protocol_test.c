#include "telemetry_protocol.h"
#include "measurement_window.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
    measurement_window_t w={0};
    assert(measurement_window_step(&w,0,1,true,false)==WINDOW_RESET);
    assert(measurement_window_step(&w,60000,1,true,false)==WINDOW_COMPLETE);
    assert(!measurement_physics_valid(&w,60000,1,false));
    assert(measurement_window_step(&w,61000,2,false,false)==WINDOW_RESET);
    assert(measurement_window_step(&w,72000,3,true,true)==WINDOW_RESET);
    assert(measurement_window_step(&w,131999,3,true,true)==WINDOW_WAIT);
    assert(measurement_window_step(&w,132000,3,true,true)==WINDOW_COMPLETE);
    assert(measurement_physics_valid(&w,132000,3,true));
    assert(!measurement_physics_valid(&w,132000,4,true));
    assert(!measurement_physics_valid(&w,134000,3,true));
    // A fast transition entirely between polls must still restart the interval.
    assert(measurement_window_step(&w,132001,5,true,true)==WINDOW_RESET);
    assert(!measurement_physics_valid(&w,132002,5,true));
    telemetry_sample_t t={.boot_id=0xFEDCBA9876543210ULL,.sequence=4000000000U,.flags=127,
        .sample_uptime_ms=120000,.uptime_ms=120010,.physics_exposure_ms=60000,.interval_ms=60000,
        .temp_millic=-12345,.pressure_pa=98286,.epoch=1790000000,.status=7,.hv=234};
    for(int i=0;i<7;i++){t.totals[i]=(1ULL<<40)+i;t.counts[i]=100+i;t.device_id[i%6]=(uint8_t)i;}
    uint8_t packet[TELEMETRY_SIZE];telemetry_encode(&t,packet);
    assert(packet[0]=='M'&&packet[1]=='P'&&packet[2]==4&&packet[3]==127);
    assert(packet[4]==0x10&&packet[11]==0xfe);
    assert(packet[56+5]==1&&packet[55]==0&&packet[159]==0);
    assert(packet[112]==100&&packet[136]==106);
    if(argc>1){FILE*f=fopen(argv[1],"wb");assert(f);assert(fwrite(packet,1,sizeof(packet),f)==sizeof(packet));fclose(f);}
    puts("PASS: full-minute qualification, transition invalidation, delay rejection, 64-bit packet encoding");
}
