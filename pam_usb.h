#pragma once
// ============================================================
// pam_usb.h — PAM 199-P over USB Host (FTDI FT231X, VID 0403 PID 6015)
// Replaces Python pam.py + serial_reconnect.py.
// Auto-reconnect comes free: USB re-enumeration is handled in
// pamUsbService(), mirroring SerialReconnect's behavior.
// ============================================================
#include <Arduino.h>

void   pamUsbInit();                 // call once in setup()
void   pamUsbService();             // call every loop(): handles (re)connect
bool   pamIsConnected();

// Send a command, block until '>' prompt or timeout. Returns full raw reply.
String pamCmd(const String &cmd, uint32_t timeoutMs = 300);

// Send a command and return only the value (last whitespace token of reply).
// e.g. "FUNCTION" reply "FUNCTION      196\r\n>"  ->  "196"
String pamValue(const String &cmd, uint32_t timeoutMs = 300);
