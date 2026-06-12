// ============================================================
// ble_server.cpp — ESP32 BLE GATT server + 200ms notify task
// ============================================================
#include "ble_server.h"
#include "config.h"
#include "machine_state.h"
#include "commands.h"
#include "ota_manager.h"


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

void initOTAService(BLEServer *pServer) {
  Serial.println("[OTA] Creating OTA service...");

  // Create a NEW service (separate from the existing command service)
  BLEService *pOtaService = pServer->createService("12345678-1234-1234-1234-1234567890AB");

  // OTA Control characteristic (for START/END commands)
  pOtaControl = pOtaService->createCharacteristic(
    "12345678-1234-1234-1234-1234567890AC",
    BLECharacteristic::PROPERTY_WRITE);
  pOtaControl->setCallbacks(new OtaControlCallbacks());

  // OTA Data characteristic (for binary chunks)
  pOtaData = pOtaService->createCharacteristic(
    "12345678-1234-1234-1234-1234567890AD",
    BLECharacteristic::PROPERTY_WRITE);
  pOtaData->setCallbacks(new OtaDataCallbacks());

  // Start the service
  pOtaService->start();

  Serial.println("[OTA] OTA service ready");
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

  initOTAService(sServer);

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  adv->setScanResponse(true);
  adv->start();

  xTaskCreatePinnedToCore(notifyTask, "ble_notify", 4096, NULL, 3, NULL, 0);
  Serial.printf("[BLE] advertising as %s\n", BLE_DEVICE_NAME);
}

// === OTA CONTROL CALLBACK ===
// Handles START and END commands
class OtaControlCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String cmd = String(pCharacteristic->getValue().c_str());
    cmd.trim();

    Serial.printf("[OTA CTRL] Received: %s\n", cmd.c_str());

    // App sends "START:123456" where 123456 is firmware size in bytes
    if (cmd.startsWith("START:")) {
      String sizeStr = cmd.substring(6);  // Extract "123456" from "START:123456"
      size_t fwSize = sizeStr.toInt();

      if (fwSize > 0 && fwSize < 2000000) {  // Sanity check: between 1 byte and 2MB
        Serial.printf("[OTA] Starting OTA: %u bytes\n", fwSize);
        if (otaBegin(fwSize)) {
          Serial.println("[OTA] Ready to receive firmware chunks");
        } else {
          Serial.println("[OTA] Failed to start OTA session");
        }
      } else {
        Serial.printf("[OTA] Invalid size: %u\n", fwSize);
      }
    }
    // App sends "END" when all chunks are done
    else if (cmd == "END") {
      Serial.println("[OTA] Received END. Finalizing...");
      if (otaFinish()) {
        Serial.println("[OTA] Reboot initiated");
      } else {
        Serial.println("[OTA] Failed to finish OTA");
      }
    } else {
      Serial.printf("[OTA] Unknown control command: %s\n", cmd.c_str());
    }
  }
};

// === OTA DATA CALLBACK ===
// Handles binary firmware chunks
class OtaDataCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    // Check if an OTA session is active
    if (!otaIsRunning()) {
      Serial.println("[OTA] ERROR: Received data but no active OTA. Send START:size first!");
      return;
    }

    // Get the raw binary data from the BLE write
    std::string rawData = pCharacteristic->getValue();
    if (rawData.empty()) {
      Serial.println("[OTA] Empty data chunk");
      return;
    }

    // Write this chunk to flash
    bool success = otaWrite((uint8_t *)rawData.data(), rawData.size());
    if (!success) {
      Serial.printf("[OTA] ERROR: Failed to write chunk (%u bytes)\n", rawData.size());
    }

    // Log progress
    size_t received = otaBytesReceived();
    Serial.printf("[OTA] Chunk OK. Total: %u bytes\n", received);
  }
};

// Global pointers (declare these at the top of ble_server.cpp with other globals)
static BLECharacteristic *pOtaControl = nullptr;
static BLECharacteristic *pOtaData = nullptr;
