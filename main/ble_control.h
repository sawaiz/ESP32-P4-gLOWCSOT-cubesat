#pragma once
#include "host/ble_gatt.h"
int ble_control_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg);
esp_err_t ble_control_init(uint16_t *response_handle);
void ble_control_disconnected(void);

bool ble_control_recent(void);
