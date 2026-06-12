// ============================================================
// dwin.cpp — DWIN HMI over UART1
// ============================================================
#include "dwin.h"
#include "config.h"

static HardwareSerial DwinSerial(1);

// value cache: skip writes when unchanged (like Python's caching)
#include <map>
static std::map<uint16_t, int16_t> sCache;

void dwinBegin() {
  DwinSerial.begin(DWIN_BAUD, SERIAL_8N1, DWIN_RX_PIN, DWIN_TX_PIN);
}

static void dwinWriteFrame(const uint8_t *payload, uint8_t payloadLen) {
  // frame: 5A A5 <len=payloadLen> <payload...>
  uint8_t hdr[3] = { 0x5A, 0xA5, payloadLen };
  DwinSerial.write(hdr, 3);
  DwinSerial.write(payload, payloadLen);
}

void dwinSendValue(uint16_t vpin, float value) {
  long scaled = lroundf(value * 10.0f);
  if (scaled > 32767)  scaled = 32767;
  if (scaled < -32768) scaled = -32768;
  int16_t v = (int16_t)scaled;

  auto it = sCache.find(vpin);
  if (it != sCache.end() && it->second == v) return;   // unchanged -> skip
  sCache[vpin] = v;

  uint8_t p[5] = { 0x82,
                   (uint8_t)(vpin >> 8), (uint8_t)(vpin & 0xFF),
                   (uint8_t)(((uint16_t)v) >> 8), (uint8_t)(((uint16_t)v) & 0xFF) };
  dwinWriteFrame(p, 5);
}

void dwinSendMode(const String &mode) {
  uint16_t m = (mode == "C") ? 1 : 0;
  auto it = sCache.find(VPIN_MODE_ADDR);
  if (it != sCache.end() && it->second == (int16_t)m) return;
  sCache[VPIN_MODE_ADDR] = (int16_t)m;

  uint8_t p[5] = { 0x82,
                   (uint8_t)(VPIN_MODE_ADDR >> 8), (uint8_t)(VPIN_MODE_ADDR & 0xFF),
                   (uint8_t)(m >> 8), (uint8_t)(m & 0xFF) };
  dwinWriteFrame(p, 5);
}

void dwinSwitchPage(uint16_t pageId) {
  uint8_t p[7] = { 0x82, 0x00, 0x84, 0x5A, 0x01,
                   (uint8_t)(pageId >> 8), (uint8_t)(pageId & 0xFF) };
  dwinWriteFrame(p, 7);
}

int dwinReadVP5100(uint32_t timeoutMs) {
  while (DwinSerial.available()) DwinSerial.read();   // flush

  uint8_t p[4] = { 0x83,
                   (uint8_t)(VP_5100 >> 8), (uint8_t)(VP_5100 & 0xFF),
                   0x01 };                            // read 1 word
  dwinWriteFrame(p, 4);

  // expected reply: 5A A5 06 83 51 00 01 <hi> <lo>  (8-9 bytes)
  uint8_t buf[16];
  int got = 0;
  uint32_t start = millis();
  while (millis() - start < timeoutMs && got < (int)sizeof(buf)) {
    if (DwinSerial.available()) buf[got++] = DwinSerial.read();
    else delay(2);
    if (got >= 9) break;
  }
  if (got >= 9 && buf[0] == 0x5A && buf[1] == 0xA5)
    return ((uint16_t)buf[got - 2] << 8) | buf[got - 1];
  return -1;
}

float dwinScaleValue(float raw, const String &mode, int function) {
  if (mode == "V") return raw / 1000.0f;
  // mode == "C"
  if (function == 196) return (raw * 0.0016f) + 4.0f;
  // function == 195
  float v = (raw * 0.0008f) + 12.0f;
  if (v < 4.0f)  v = 4.0f;
  if (v > 20.0f) v = 20.0f;
  return v;
}
