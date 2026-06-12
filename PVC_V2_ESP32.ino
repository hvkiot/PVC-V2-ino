// ============================================================
// PVC_V2_ESP32.ino — main orchestration (port of main.py)
//
// Hardware:
//   PAM 199-P  -> USB host port (printer cable + OTG adapter)
//   DWIN HMI   -> UART1: TX=GPIO17 -> DWIN RX, RX=GPIO18 <- DWIN TX, 115200
//   BLE        -> built-in, advertises as BLE_DEVICE_NAME
//
// Board settings (Arduino IDE):
//   Board: ESP32S3 Dev Module | USB CDC On Boot: Disabled
//   USB Mode: Hardware CDC and JTAG | upload via the COM port
// ============================================================
#include "config.h"
#include "machine_state.h"
#include "pam_usb.h"
#include "dwin.h"
#include "ble_server.h"
#include "commands.h"

MachineState gState;

static uint32_t lastModeCheck = 0;
static uint32_t lastPageSwitch = 0;
static bool bleStarted = false;
static uint32_t bootMs = 0;  // set in setup(): bootMs = millis();



/* ---------- status word decoding (from pam.py) ---------- */
static String decodeReady(int func, long rx1) {
  if (func == 196) {
    bool a = rx1 & (1L << 14);
    bool b = rx1 & (1L << 15);
    if (a && b) return "A + B ACTIVE";
    if (a) return "A ACTIVE";  // ← verify exact strings
    if (b) return "B ACTIVE";  // ← against pam.py
    return "OFF";              // ← get_ready_status()
  } else {                     // 195
    if (rx1 == 65532) return "ALL OFF";
    bool aOk = rx1 & (1L << 7);
    bool bOk = rx1 & (1L << 8);
    if (aOk && bOk) return "A + B ACTIVE";  // ← verify these too
    if (aOk) return "A ACTIVE";
    if (bOk) return "B ACTIVE";
    return "OFF";
  }
}

/* ---------- one full read/refresh cycle ---------- */
static void readCycle() {
  // FUNCTION
  String fs = pamValue("FUNCTION");
  int func = fs.toInt();
  if (func != 195 && func != 196) {
    gState.lock();
    gState.func = 0;
    gState.unlock();
    return;
  }

  String ainA = pamValue("AINA");
  if (ainA != "V" && ainA != "C") ainA = "V";

  float wa = 0, wb = 0;
  String curA = "None", curB = "None", curS = "None";

  if (func == 196) {
    String ainB = pamValue("AINB");

    // AIN mismatch -> DWIN page 28 user prompt (with cooldown)
    if ((ainB == "V" || ainB == "C") && ainB != ainA && millis() - lastPageSwitch > PAGE_SWITCH_COOLDOWN_MS) {
      lastPageSwitch = millis();
      Serial.println("[MAIN] AIN mismatch -> DWIN page 28");
      dwinSwitchPage(DWIN_PAGE_MISMATCH);
      int sel = dwinReadVP5100(VP5100_POLL_TIMEOUT_MS);
      if (sel == 0 || sel == 1) {
        PvcCommand c;
        c.type = CMD_SET_AIN_MODE;
        c.unit = (sel == 1) ? 'C' : 'V';
        commandsEnqueue(c);
      }
      return;  // restart cycle after resolution
    }

    wa = pamValue("WA").toFloat();
    wb = pamValue("WB").toFloat();
    curA = pamValue("CURRENT:A");
    if (curA == "") curA = "None";
    curB = pamValue("CURRENT:B");
    if (curB == "") curB = "None";

  } else {  // 195
    wa = pamValue("W").toFloat();
    wb = 0.0f;  // forced 0 in directional mode
    curS = pamValue("CURRENT");
    if (curS == "") curS = "None";
  }

  float ia = pamValue("IA").toFloat();
  float ib = pamValue("IB").toFloat();
  long rx1 = pamValue("RX1:READYA").toInt();

  String rcsRaw = pamCmd("RC:S");    // full raw reply for debugging
  String rcsVal = pamValue("RC:S");  // parsed last-token
  long rcs = rcsVal.toInt();

  String enB = pamValue("ENABLE_B");

  String ready = decodeReady(func, rx1);
  bool pin15 = (rcs >> 6) & 1;  // bit 6
  bool pin6 = (rcs >> 3) & 1;   // bit 3

  // ---- DWIN refresh ----
  float waScaled = dwinScaleValue(wa, ainA, func);
  float wbScaled = (func == 196) ? dwinScaleValue(wb, ainA, func) : 0.0f;

  dwinSendMode(ainA);
  dwinSendValue(VPIN_WA, waScaled);
  dwinSendValue(VPIN_WB, wbScaled);
  dwinSendValue(VPIN_IA, ia);
  dwinSendValue(VPIN_IB, ib);
  dwinSendValue(VPIN_TEMP, FIXED_TEMP);

  // ---- state update (BLE notify task reads this) ----
  gState.lock();
  gState.func = func;
  gState.wa = waScaled;
  gState.wb = wbScaled;
  gState.ia = ia;
  gState.ib = ib;
  gState.mode = ainA;
  gState.ready = ready;
  gState.pin15 = pin15;
  gState.pin6 = pin6;
  gState.enabledB = (enB == "ON");
  gState.currentA = curA;
  gState.currentB = curB;
  gState.currentS = curS;
  gState.unlock();
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== PVC-V2-ESP32 (PAM USB-host + DWIN + BLE) ===");

  gState.begin();
  commandsInit();
  dwinBegin();

  delay(5000);
  pamUsbInit();
  // bleInit();
  bootMs = millis();
  Serial.println("[MAIN] waiting for PAM enumeration...");
}

void loop() {
  pamUsbService();  // USB connect/disconnect handling

  if (!bleStarted && (pamIsConnected() || millis() - bootMs > 5000)) {
    bleInit();
    bleStarted = true;
  }

  if (!pamIsConnected()) {
    delay(50);
    return;
  }

  // 1. execute queued BLE commands first (single-threaded PAM access)
  commandsProcessPending();

  // 2. skip reads while a transition is in progress
  if (gState.isInTransition()) {
    delay(10);
    return;
  }

  // 3. every 3s ensure PAM is in STD mode
  if (millis() - lastModeCheck > MODE_CHECK_INTERVAL_MS) {
    lastModeCheck = millis();
    String m = pamValue("MODE");
    if (m.length() && m != "STD") {
      Serial.printf("[MAIN] PAM in %s mode -> forcing STD\n", m.c_str());
      pamCmd("MODE STD");
    }
  }

  // 4. full read + DWIN + state refresh
  readCycle();

  delay(5);  // MAIN_LOOP_DELAY
}
