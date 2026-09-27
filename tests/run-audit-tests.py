#!/usr/bin/env python3
"""Host-test the production audit/storage code. Pass the ESP-IDF root as argv[1]."""
from pathlib import Path
import subprocess,sys,tempfile,shutil
repo=Path(__file__).resolve().parents[1]
idf=Path(sys.argv[1])
with tempfile.TemporaryDirectory() as t:
 d=Path(t); (d/'mbedtls').mkdir()
 shutil.copy(repo/'main/count_totals.h',d/'count_totals.h');shutil.copy(repo/'main/audit.h',d/'audit.h');shutil.copy(repo/'tests/audit/shim.h',d/'shim.h')
 (d/'audit.c').write_text((repo/'main/audit.c').read_text().replace('#include "app_common.h"','#include "shim.h"'))
 s=(repo/'main/storage.c').read_text();a=s.index('void csv_time_string');b=s.index('// Construct the full SD path')
 (d/'csv.c').write_text('#include "shim.h"\n'+s[a:b])
 (d/'identity.h').write_text('#define BUILD_GIT_SHA "test"\n#define BUILD_DIRTY 1\n#define BUILD_UTC "2026-09-27T00:00:00Z"\n#define FPGA_SHA "fixture"\n')
 (d/'esp_random.h').write_text('#include <stddef.h>\nvoid esp_fill_random(void*,size_t);\n')
 (d/'esp_mac.h').write_text('#define ESP_MAC_BASE 0\nint esp_read_mac(uint8_t*,int);\n')
 (d/'mbedtls/base64.h').write_text('int mbedtls_base64_encode(unsigned char*,size_t,size_t*,const unsigned char*,size_t);\n')
 json=idf/'components/json/cJSON'
 subprocess.run(['cc','-std=c11','-D_POSIX_C_SOURCE=200809L','-Wall','-Wextra','-Werror','-I'+str(d),'-I'+str(json),str(d/'audit.c'),str(d/'csv.c'),str(repo/'tests/audit/test.c'),str(json/'cJSON.c'),'-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],cwd=d,check=True)
 if len(sys.argv)>2: shutil.copy(d/'fixture.csv',sys.argv[2])
