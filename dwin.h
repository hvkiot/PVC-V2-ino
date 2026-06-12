#pragma once
// ============================================================
// dwin.h — port of dwin.py (DWINDisplay), UART1 @ 115200
// Protocol: 5A A5 <len> <cmd> <addr> <data>
// ============================================================
#include <Arduino.h>

void  dwinBegin();
void  dwinSendValue(uint16_t vpin, float value);   // x10, int16, cached
void  dwinSendMode(const String &mode);            // "V"->0, "C"->1 to 0x5000
void  dwinSwitchPage(uint16_t pageId);
// Poll VP_5100 (page-28 user selection). Returns 0 (V), 1 (C) or -1 on timeout.
int   dwinReadVP5100(uint32_t timeoutMs);
float dwinScaleValue(float raw, const String &mode, int function);
