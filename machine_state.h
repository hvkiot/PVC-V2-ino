#pragma once
// ============================================================
// machine_state.h  —  port of state.py (MachineState)
// ============================================================
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

struct MachineState {
  volatile bool inTransition = false;
  int func = 0;  // 195 / 196 / 0 = unknown
  float wa = 0.0f;
  float wb = 0.0f;
  float ia = 0.0f;
  float ib = 0.0f;
  String mode = "None";   // AIN mode of channel A
  String ready = "None";  // decoded text e.g. "A + B ACTIVE"
  bool pin15 = false;
  bool pin6 = false;
  bool enabledB = false;
  String currentA = "None";
  String currentB = "None";
  String currentS = "None";

  SemaphoreHandle_t mtx = nullptr;

  void begin() {
    mtx = xSemaphoreCreateMutex();
  }
  void lock() {
    xSemaphoreTake(mtx, portMAX_DELAY);
  }
  void unlock() {
    xSemaphoreGive(mtx);
  }

  void setTransition(bool v) {
    inTransition = v;
  }
  bool isInTransition() const {
    return inTransition;
  }

  String buildPacket() {
    lock();
    String p = "FUNC:" + (func ? String(func) : String("None")) + ",WA:" + String(wa, 3) + ",WB:" + String(wb, 3) + ",IA:" + String(ia, 1) + ",IB:" + String(ib, 1) + ",MODE:" + mode + ",READY:" + ready + ",PIN15:" + (pin15 ? "True" : "False") + ",PIN6:" + (pin6 ? "True" : "False") + ",ENABLED_B:" + (enabledB ? "True" : "False") + ",CURRENT_A_STATUS:" + currentA + ",CURRENT_B_STATUS:" + currentB + ",CURRENT_STATUS:" + currentS + "\n";
    unlock();
    return p;
  }
};  // <<<<<< this closing brace + semicolon is what went missing

extern MachineState gState;