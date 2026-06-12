#pragma once
// ============================================================
// commands.h — port of command_processor.py
// Commands are queued from the BLE write callback (BLE task)
// and executed in loop() so ALL PAM access stays single-threaded.
// ============================================================
#include <Arduino.h>

enum CmdType : uint8_t {
  CMD_NONE = 0,
  CMD_CHANGE_MODE,    // mode = 195/196
  CMD_SET_AIN_MODE,   // unit = 'V'/'C'
  CMD_SET_CURRENT,    // channel 'A'/'B', mode, value
};

struct PvcCommand {
  CmdType type = CMD_NONE;
  int     mode = 0;
  char    unit = 'V';
  char    channel = 'A';
  int     value = 0;
};

void commandsInit();
bool commandsEnqueue(const PvcCommand &c);   // called from BLE callback
void commandsProcessPending();               // called from loop()

// Parses BLE WriteValue strings ("195","VOLTAGE","CURA:1600:196",...)
// into a PvcCommand. Returns false if unrecognized.
bool parseBleCommand(const String &s, PvcCommand &out);
