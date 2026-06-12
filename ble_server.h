#pragma once
// ============================================================
// ble_server.h — port of gatt_server.py (BlueZ/D-Bus -> ESP32 BLE)
// Same service/characteristic UUIDs, same notification packet,
// same write-command grammar — your Flutter app connects unchanged.
// ============================================================
#include <Arduino.h>

void bleInit();          // creates server, service, characteristic, advertising
                          // and starts the 200ms notify task
bool bleIsConnected();
