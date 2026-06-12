#pragma once
// ============================================================
// PVC-V2-ESP32  —  config.h   (ported from config.py)
// ============================================================

// ---------------- BLE ----------------
#define SERVICE_UUID      "12345678-1234-5678-1234-56789abcdef0"
#define CHAR_UUID         "12345678-1234-5678-1234-56789abcdef1"
#define BLE_DEVICE_NAME   "PVC-26020007"        // was .env DEVICE_SUFFIX

// ---------------- PAM (USB Host / FTDI FT-X) ----------------
#define PAM_VID           0x0403
#define PAM_PID           0x6015
#define FTDI_PORT_IDX     0                     // if garbled data: try 1
#define PAM_BAUD_WVALUE   0xC034                // FTDI divisor for 57600
#define PAM_BAUD_WINDEX   FTDI_PORT_IDX
#define PAM_CMD_TIMEOUT_MS 300                  // per-command, like Python 200ms+margin

// ---------------- DWIN (UART1) ----------------
#define DWIN_TX_PIN       17
#define DWIN_RX_PIN       18
#define DWIN_BAUD         115200

// DWIN VPIN addresses
#define VPIN_WA           0x5500
#define VPIN_WB           0x5600
#define VPIN_IA           0x5700
#define VPIN_IB           0x5800
#define VPIN_TEMP         0x5900
#define VPIN_MODE_ADDR    0x5000
#define VP_5100           0x5100
#define DWIN_PAGE_MISMATCH 28

// ---------------- Timing ----------------
#define BLE_NOTIFY_INTERVAL_MS   200
#define MODE_CHECK_INTERVAL_MS   3000
#define PAGE_SWITCH_COOLDOWN_MS  2000
#define EEPROM_SAVE_DELAY_MS     3000
#define VP5100_POLL_TIMEOUT_MS   2000

// ---------------- Limits ----------------
#define CURRENT_MIN_MA    500
#define CURRENT_MAX_MA    2600

// ---------------- Fixed values ----------------
#define FIXED_TEMP        24.0f
