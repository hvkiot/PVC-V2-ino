// ============================================================
// ble_server.cpp — ESP32 BLE GATT server + 200ms notify task
// ============================================================
#include "ble_server.h"
#include "config.h"
#include "machine_state.h"
#include "commands.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

static BLEServer *sServer = nullptr;
static BLECharacteristic *sChar = nullptr;
static volatile bool sConnected = false;

bool bleIsConnected() {
  return sConnected;
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *s) override {
    sConnected = true;
    Serial.println("[BLE] central connected");
  }
  void onDisconnect(BLEServer *s) override {
    sConnected = false;
    Serial.println("[BLE] central disconnected — re-advertising");
    s->getAdvertising()->start();
  }
};

class CharCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    String v = String(c->getValue().c_str());
    Serial.printf("[BLE] write: %s\n", v.c_str());
    PvcCommand cmd;
    if (parseBleCommand(v, cmd)) {
      if (!commandsEnqueue(cmd)) Serial.println("[BLE] command queue full!");
    } else {
      Serial.println("[BLE] unrecognized command");
    }
  }
};

// 200ms state broadcast — equivalent of the Python GLib timeout
static void notifyTask(void *) {
  for (;;) {
    if (sConnected && sChar) {
      String pkt = gState.buildPacket();
      sChar->setValue((uint8_t *)pkt.c_str(), pkt.length());
      sChar->notify();
    }
    vTaskDelay(pdMS_TO_TICKS(BLE_NOTIFY_INTERVAL_MS));
  }
}

void bleInit() {
  BLEDevice::init(BLE_DEVICE_NAME);
  BLEDevice::setPower(ESP_PWR_LVL_N3);  // -3dBm; plenty for short-range industrial use
  BLEDevice::setMTU(247);               // full packet in one notify

  sServer = BLEDevice::createServer();
  sServer->setCallbacks(new ServerCallbacks());

  BLEService *svc = sServer->createService(SERVICE_UUID);
  sChar = svc->createCharacteristic(
    CHAR_UUID,
    BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  sChar->addDescriptor(new BLE2902());
  sChar->setCallbacks(new CharCallbacks());

  svc->start();
  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();

  xTaskCreatePinnedToCore(notifyTask, "ble_notify", 4096, NULL, 3, NULL, 0);
  Serial.printf("[BLE] advertising as %s\n", BLE_DEVICE_NAME);
}
