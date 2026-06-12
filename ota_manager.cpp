#include "ota_manager.h"
#include <Update.h>

// Global state
static bool gOtaRunning = false;
static size_t gBytesReceived = 0;
static size_t gTotalSize = 0;

bool otaBegin(size_t totalFirmwareSize) {
  gBytesReceived = 0;
  gTotalSize = totalFirmwareSize;

  // Tell ESP32's flash controller: "I'm about to write firmware"
  if (!Update.begin(totalFirmwareSize)) {
    Serial.println("[OTA] ERROR: Update.begin() failed");
    return false;
  }

  gOtaRunning = true;
  Serial.printf("[OTA] Started. Expecting %u bytes\n", totalFirmwareSize);
  return true;
}

bool otaWrite(const uint8_t* data, size_t len) {
  if (!gOtaRunning) {
    Serial.println("[OTA] ERROR: otaWrite() called but OTA not running");
    return false;
  }

  // Write this chunk to flash
  size_t written = Update.write((uint8_t*)data, len);
  gBytesReceived += written;

  // Log progress every 10%
  int percent = (gBytesReceived * 100) / gTotalSize;
  static int lastPercent = 0;
  if (percent >= lastPercent + 10) {
    Serial.printf("[OTA] Progress: %d%% (%u/%u bytes)\n",
                  percent, gBytesReceived, gTotalSize);
    lastPercent = percent;
  }

  return (written == len);  // Return true if all bytes written successfully
}

bool otaFinish() {
  if (!gOtaRunning) {
    Serial.println("[OTA] ERROR: otaFinish() called but OTA not running");
    return false;
  }

  gOtaRunning = false;

  // Tell ESP32: "Done writing, verify and commit"
  if (!Update.end(true)) {  // true = verify
    Serial.println("[OTA] ERROR: Update.end() failed");
    return false;
  }

  Serial.println("[OTA] SUCCESS! New firmware written. Rebooting in 2 seconds...");
  delay(2000);
  ESP.restart();  // Reboot to load new firmware
  return true;
}

bool otaIsRunning() {
  return gOtaRunning;
}

size_t otaBytesReceived() {
  return gBytesReceived;
}