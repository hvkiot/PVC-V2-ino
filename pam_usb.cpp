// ============================================================
// pam_usb.cpp — hardened version of the POC that already works
// on your bench (FUNCTION -> 196). Includes the inXferCb race
// fix and a mutex around the RX buffer.
// ============================================================
#include "pam_usb.h"
#include "config.h"
#include "usb/usb_host.h"
#include "usb/usb_helpers.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// FTDI vendor requests
#define FTDI_RESET        0
#define FTDI_MODEM_CTRL   1
#define FTDI_SET_FLOW     2
#define FTDI_SET_BAUD     3
#define FTDI_SET_DATA     4
#define FTDI_SET_LATENCY  9

static usb_host_client_handle_t sClient;
static usb_device_handle_t      sDev        = NULL;
static volatile uint8_t         sNewDevAddr = 0;
static volatile bool            sDevGone    = false;
static uint8_t                  sEpIn = 0, sEpOut = 0;
static usb_transfer_t          *sXferIn = NULL;
static SemaphoreHandle_t        sCtrlSem;
static SemaphoreHandle_t        sRxMtx;
static volatile bool            sReady = false;

static String sRxBuf;

bool pamIsConnected() { return sReady; }

/* ---------------- USB callbacks ---------------- */
static void clientEventCb(const usb_host_client_event_msg_t *msg, void *arg) {
  if (msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV)  sNewDevAddr = msg->new_dev.address;
  if (msg->event == USB_HOST_CLIENT_EVENT_DEV_GONE) sDevGone = true;
}

static void ctrlXferCb(usb_transfer_t *t) { xSemaphoreGive(sCtrlSem); }

static void inXferCb(usb_transfer_t *t) {
  if (t->status == USB_TRANSFER_STATUS_COMPLETED && t->actual_num_bytes > 2) {
    xSemaphoreTake(sRxMtx, portMAX_DELAY);
    // FTDI prefixes every IN packet with 2 modem-status bytes
    for (int i = 2; i < t->actual_num_bytes; i++) sRxBuf += (char)t->data_buffer[i];
    xSemaphoreGive(sRxMtx);
  }
  if (sReady && t->status != USB_TRANSFER_STATUS_NO_DEVICE
             && t->status != USB_TRANSFER_STATUS_CANCELED) {
    usb_host_transfer_submit(t);
  }
}

static void outXferCb(usb_transfer_t *t) { usb_host_transfer_free(t); }

/* ---------------- FTDI control helper ---------------- */
static bool ftdiCtrl(uint8_t req, uint16_t value, uint16_t index) {
  usb_transfer_t *t;
  if (usb_host_transfer_alloc(sizeof(usb_setup_packet_t), 0, &t) != ESP_OK) return false;
  usb_setup_packet_t *s = (usb_setup_packet_t *)t->data_buffer;
  s->bmRequestType = 0x40;
  s->bRequest = req;  s->wValue = value;  s->wIndex = index;  s->wLength = 0;
  t->num_bytes        = sizeof(usb_setup_packet_t);
  t->device_handle    = sDev;
  t->bEndpointAddress = 0;
  t->callback         = ctrlXferCb;
  bool ok = false;
  if (usb_host_transfer_submit_control(sClient, t) == ESP_OK)
    if (xSemaphoreTake(sCtrlSem, pdMS_TO_TICKS(1000)) == pdTRUE)
      ok = (t->status == USB_TRANSFER_STATUS_COMPLETED);
  usb_host_transfer_free(t);
  return ok;
}

/* ---------------- device bring-up ---------------- */
static bool setupPam(uint8_t addr) {
  if (usb_host_device_open(sClient, addr, &sDev) != ESP_OK) return false;

  const usb_device_desc_t *dd;
  usb_host_get_device_descriptor(sDev, &dd);
  Serial.printf("[USB] Device VID=%04X PID=%04X\n", dd->idVendor, dd->idProduct);
  if (dd->idVendor != PAM_VID || dd->idProduct != PAM_PID) {
    usb_host_device_close(sClient, sDev); sDev = NULL; return false;
  }

  const usb_config_desc_t *cfg;
  usb_host_get_active_config_descriptor(sDev, &cfg);
  int off = 0;
  const usb_intf_desc_t *intf = usb_parse_interface_descriptor(cfg, 0, 0, &off);
  for (int i = 0; i < intf->bNumEndpoints; i++) {
    int eoff = off;
    const usb_ep_desc_t *ep =
        usb_parse_endpoint_descriptor_by_index(intf, i, cfg->wTotalLength, &eoff);
    if (ep->bEndpointAddress & 0x80) sEpIn = ep->bEndpointAddress;
    else                             sEpOut = ep->bEndpointAddress;
  }
  if (usb_host_interface_claim(sClient, sDev, 0, 0) != ESP_OK) return false;

  bool ok = true;
  ok &= ftdiCtrl(FTDI_RESET,       0,               FTDI_PORT_IDX);
  ok &= ftdiCtrl(FTDI_SET_BAUD,    PAM_BAUD_WVALUE, PAM_BAUD_WINDEX);
  ok &= ftdiCtrl(FTDI_SET_DATA,    0x0008,          FTDI_PORT_IDX);   // 8N1
  ok &= ftdiCtrl(FTDI_SET_FLOW,    0x0000,          FTDI_PORT_IDX);
  ok &= ftdiCtrl(FTDI_MODEM_CTRL,  0x0303,          FTDI_PORT_IDX);   // DTR+RTS
  ok &= ftdiCtrl(FTDI_SET_LATENCY, 16,              FTDI_PORT_IDX);
  if (!ok) { Serial.println("[FTDI] init failed"); return false; }

  usb_host_transfer_alloc(64, 0, &sXferIn);
  sXferIn->num_bytes        = 64;
  sXferIn->device_handle    = sDev;
  sXferIn->bEndpointAddress = sEpIn;
  sXferIn->callback         = inXferCb;
  sReady = true;
  usb_host_transfer_submit(sXferIn);
  Serial.println("[PAM] connected & configured (57600 8N1)");
  return true;
}

/* ---------------- background USB tasks ---------------- */
static void usbLibTask(void *) {
  while (1) {
    uint32_t f;
    usb_host_lib_handle_events(portMAX_DELAY, &f);
    if (f & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
  }
}
static void usbClientTask(void *) {
  while (1) usb_host_client_handle_events(sClient, portMAX_DELAY);
}

/* ---------------- public API ---------------- */
void pamUsbInit() {
  sCtrlSem = xSemaphoreCreateBinary();
  sRxMtx   = xSemaphoreCreateMutex();

  usb_host_config_t hostCfg = { .skip_phy_setup = false,
                                .intr_flags = ESP_INTR_FLAG_LEVEL1 };
  ESP_ERROR_CHECK(usb_host_install(&hostCfg));

  usb_host_client_config_t cliCfg = {
    .is_synchronous = false,
    .max_num_event_msg = 8,
    .async = { .client_event_callback = clientEventCb, .callback_arg = NULL }
  };
  ESP_ERROR_CHECK(usb_host_client_register(&cliCfg, &sClient));

  xTaskCreatePinnedToCore(usbLibTask,    "usb_lib",    4096, NULL, 5, NULL, 0);
  xTaskCreatePinnedToCore(usbClientTask, "usb_client", 4096, NULL, 5, NULL, 0);
}

void pamUsbService() {
  if (sNewDevAddr) { uint8_t a = sNewDevAddr; sNewDevAddr = 0; setupPam(a); }
  if (sDevGone) {
    sDevGone = false; sReady = false;
    if (sXferIn) { usb_host_transfer_free(sXferIn); sXferIn = NULL; }
    if (sDev) { usb_host_interface_release(sClient, sDev, 0);
                usb_host_device_close(sClient, sDev); sDev = NULL; }
    Serial.println("[USB] PAM disconnected — waiting for re-enumeration");
  }
}

String pamCmd(const String &cmd, uint32_t timeoutMs) {
  if (!sReady) return "";

  // flush stale RX
  xSemaphoreTake(sRxMtx, portMAX_DELAY);
  sRxBuf = "";
  xSemaphoreGive(sRxMtx);

  String full = cmd + "\r\n";
  usb_transfer_t *t;
  if (usb_host_transfer_alloc(full.length(), 0, &t) != ESP_OK) return "";
  memcpy(t->data_buffer, full.c_str(), full.length());
  t->num_bytes        = full.length();
  t->device_handle    = sDev;
  t->bEndpointAddress = sEpOut;
  t->callback         = outXferCb;
  if (usb_host_transfer_submit(t) != ESP_OK) { usb_host_transfer_free(t); return ""; }

  // wait for '>' prompt
  uint32_t start = millis();
  String out;
  while (millis() - start < timeoutMs) {
    xSemaphoreTake(sRxMtx, portMAX_DELAY);
    bool done = (sRxBuf.indexOf('>') >= 0);
    if (done) out = sRxBuf;
    xSemaphoreGive(sRxMtx);
    if (done) return out;
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  // timeout — return whatever arrived (mirrors Python behaviour)
  xSemaphoreTake(sRxMtx, portMAX_DELAY);
  out = sRxBuf;
  xSemaphoreGive(sRxMtx);
  return out;
}

String pamValue(const String &cmd, uint32_t timeoutMs) {
  String r = pamCmd(cmd, timeoutMs);
  if (r.length() == 0) return "";
  r.replace(">", "");
  r.replace("\r", " ");
  r.replace("\n", " ");
  r.trim();
  if (r.indexOf("command error") >= 0) return "";
  int sp = r.lastIndexOf(' ');
  return (sp >= 0) ? r.substring(sp + 1) : r;
}
