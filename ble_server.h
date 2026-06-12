#pragma once
// ============================================================
// ble_server.h — port of gatt_server.py (BlueZ/D-Bus -> ESP32 BLE)
// Same service/characteristic UUIDs, same notification packet,
// same write-command grammar — your Flutter app connects unchanged.
// ============================================================
#include <Arduino.h>
#include "ota_manager.h"

extern BLECharacteristic* pOtaControl;
extern BLECharacteristic* pOtaData;

void initOTAService(BLEServer* pServer);

void bleInit();  // creates server, service, characteristic, advertising
                 // and starts the 200ms notify task
bool bleIsConnected();
