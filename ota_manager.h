#pragma once
#include <Arduino.h>

// Public functions the OTA service will call

// Start OTA session. Call this when app sends "START:12345"
bool otaBegin(size_t totalFirmwareSize);

// Write a chunk of firmware. Call this for each binary block from the app
bool otaWrite(const uint8_t* data, size_t len);

// Finish OTA. Call this when app sends "END"
// This will reboot the ESP
bool otaFinish();

// Check if an OTA is currently in progress
bool otaIsRunning();

// How many bytes received so far (for progress bar in app)
size_t otaBytesReceived();