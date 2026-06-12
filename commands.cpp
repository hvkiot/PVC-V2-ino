// ============================================================
// commands.cpp — queued command execution (single-threaded PAM access)
// ============================================================
#include "commands.h"
#include "config.h"
#include "pam_usb.h"
#include "machine_state.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

static QueueHandle_t sQueue;

void commandsInit() { sQueue = xQueueCreate(50, sizeof(PvcCommand)); }

bool commandsEnqueue(const PvcCommand &c) {
  return xQueueSend(sQueue, &c, 0) == pdTRUE;
}

/* ---------------- BLE string -> command (from gatt_server.py) ----------------
 *  "195" / "196"        -> CHANGE_MODE
 *  "VOLTAGE"/"CURRENT"  -> SET_AIN_MODE V / C
 *  "CUR:1500:195"       -> SET_CURRENT  ch A, mode 195
 *  "CURA:1600:196"      -> SET_CURRENT  ch A, mode 196
 *  "CURB:1200:196"      -> SET_CURRENT  ch B, mode 196
 * --------------------------------------------------------------------------- */
bool parseBleCommand(const String &raw, PvcCommand &out) {
  String s = raw; s.trim();

  if (s == "195" || s == "196") {
    out.type = CMD_CHANGE_MODE; out.mode = s.toInt(); return true;
  }
  if (s == "VOLTAGE") { out.type = CMD_SET_AIN_MODE; out.unit = 'V'; return true; }
  if (s == "CURRENT") { out.type = CMD_SET_AIN_MODE; out.unit = 'C'; return true; }

  if (s.startsWith("CUR")) {
    int c1 = s.indexOf(':');
    int c2 = s.indexOf(':', c1 + 1);
    if (c1 < 0 || c2 < 0) return false;
    String head = s.substring(0, c1);          // CUR / CURA / CURB
    int value   = s.substring(c1 + 1, c2).toInt();
    int mode    = s.substring(c2 + 1).toInt();
    out.type    = CMD_SET_CURRENT;
    out.value   = value;
    out.mode    = mode;
    out.channel = (head == "CURB") ? 'B' : 'A';
    return true;
  }
  return false;
}

/* ---------------- handlers (port of command_processor.py) ---------------- */

static void handleChangeMode(const PvcCommand &c) {
  gState.setTransition(true);
  Serial.printf("[CMD] CHANGE_MODE -> %d\n", c.mode);

  pamCmd("FUNCTION " + String(c.mode));
  pamCmd("IA 0");
  pamCmd("IB 0");
  pamCmd("SAVE");
  delay(EEPROM_SAVE_DELAY_MS);          // rare transition; blocking is acceptable

  if (c.mode == 196) {
    // sync AIN modes: apply A's mode to B
    String ainA = pamValue("AINA");
    if (ainA == "V" || ainA == "C") {
      pamCmd("AINB " + ainA);
      pamCmd("SAVE");
      delay(EEPROM_SAVE_DELAY_MS);
    }
  }
  // verify
  String f = pamValue("FUNCTION");
  Serial.printf("[CMD] mode now: %s\n", f.c_str());
  gState.setTransition(false);
}

static void handleSetAinMode(const PvcCommand &c) {
  gState.setTransition(true);
  String u(c.unit);
  Serial.printf("[CMD] SET_AIN_MODE -> %s\n", u.c_str());

  gState.lock(); int func = gState.func; gState.unlock();
  if (func == 196) {
    pamCmd("AINA " + u);                 // both channels must match in 196
    pamCmd("AINB " + u);
  } else {
    pamCmd("AINA " + u);
  }
  pamCmd("SAVE");
  delay(EEPROM_SAVE_DELAY_MS);
  gState.setTransition(false);
}

static void handleSetCurrent(const PvcCommand &c) {
  int v = c.value;
  if (v < CURRENT_MIN_MA || v > CURRENT_MAX_MA) {
    Serial.printf("[CMD] SET_CURRENT rejected (%d out of %d..%d)\n",
                  v, CURRENT_MIN_MA, CURRENT_MAX_MA);
    return;
  }
  gState.setTransition(true);
  Serial.printf("[CMD] SET_CURRENT ch=%c v=%d mode=%d\n", c.channel, v, c.mode);

  if (c.mode == 195)            pamCmd("CURRENT " + String(v));
  else if (c.channel == 'A')    pamCmd("CURRENT:A " + String(v));
  else                          pamCmd("CURRENT:B " + String(v));

  gState.setTransition(false);
}

void commandsProcessPending() {
  PvcCommand c;
  while (xQueueReceive(sQueue, &c, 0) == pdTRUE) {
    if (!pamIsConnected()) { Serial.println("[CMD] dropped (PAM offline)"); continue; }
    switch (c.type) {
      case CMD_CHANGE_MODE:  handleChangeMode(c);  break;
      case CMD_SET_AIN_MODE: handleSetAinMode(c);  break;
      case CMD_SET_CURRENT:  handleSetCurrent(c);  break;
      default: break;
    }
  }
}
