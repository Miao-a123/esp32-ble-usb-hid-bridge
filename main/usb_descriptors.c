/*
 * usb_descriptors.c - USB HID 设备描述符 (键盘 + 鼠标复合设备)
 *
 * 单个 HID 接口, 通过 Report ID 区分键盘和鼠标:
 *   Report ID 1: 键盘报告 (9 bytes = 1 ID + 8 keyboard data)
 *   Report ID 2: 鼠标报告 (6 bytes = 1 ID + 5 mouse data)
 */

#include "usb_descriptors.h"
#include <string.h>

/*
 * HID Report Descriptor - 键盘 + 鼠标
 * 使用 Report ID 区分两种设备类型
 */
uint8_t const desc_hid_report[] = {
    /* Keyboard Report (Report ID = 1) */
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
    /* Mouse Report (Report ID = 2) */
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(REPORT_ID_MOUSE))
};

/*
 * USB Device Descriptor
 *
 * VID: 0x303A (Espressif)
 * PID: 0x4002 (自定义, 键盘+鼠标复合设备)
 * USB 2.0 Full Speed
 */
tusb_desc_device_t const desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,  /* USB 2.0 */
    .bDeviceClass       = 0x00,    /* 由接口指定类 */
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x303A,  /* Espressif VID */
    .idProduct          = 0x4002,  /* 自定义 PID (键盘+鼠标) */
    .bcdDevice          = 0x0100,  /* Device version 1.00 */
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

/*
 * USB Configuration Descriptor
 *
 * 结构: Configuration(9) + Interface(9) + HID(9) + Endpoint(7) = 34 bytes
 *
 * 接口: HID (Report Protocol, 键盘+鼠标)
 * 端点: Interrupt IN, EP1, 16 bytes, 1ms 轮询
 */
uint8_t const desc_configuration[] = {
    /* --- Configuration Descriptor (9 bytes) --- */
    0x09,                               /* bLength */
    TUSB_DESC_CONFIGURATION,            /* bDescriptorType */
    U16_TO_U8S_LE(CONFIG_TOTAL_LEN),    /* wTotalLength = 34 */
    0x01,                               /* bNumInterfaces */
    0x01,                               /* bConfigurationValue */
    0x00,                               /* iConfiguration */
    0x80 | TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, /* bmAttributes: Remote Wakeup */
    50,                                 /* bMaxPower: 50 * 2mA = 100mA */

    /* --- Interface Descriptor (9 bytes) --- */
    0x09,                               /* bLength */
    TUSB_DESC_INTERFACE,                /* bDescriptorType */
    0x00,                               /* bInterfaceNumber */
    0x00,                               /* bAlternateSetting */
    0x01,                               /* bNumEndpoints */
    0x03,                               /* bInterfaceClass: HID */
    0x00,                               /* bInterfaceSubClass: No Boot (使用 Report Protocol) */
    0x00,                               /* bInterfaceProtocol: None (键盘+鼠标复合) */
    0x00,                               /* iInterface */

    /* --- HID Descriptor (9 bytes) --- */
    0x09,                               /* bLength */
    0x21,                               /* bDescriptorType: HID */
    0x11, 0x01,                         /* bcdHID: HID 1.11 */
    0x00,                               /* bCountryCode: Not Supported */
    0x01,                               /* bNumDescriptors: 1 */
    0x22,                               /* bDescriptorType: Report */
    U16_TO_U8S_LE(sizeof(desc_hid_report)), /* wDescriptorLength */

    /* --- Endpoint Descriptor (7 bytes) --- */
    0x07,                               /* bLength */
    TUSB_DESC_ENDPOINT,                 /* bDescriptorType */
    0x81,                               /* bEndpointAddress: IN, EP1 */
    0x03,                               /* bmAttributes: Interrupt */
    U16_TO_U8S_LE(16),                  /* wMaxPacketSize: 16 bytes (键盘9+鼠标6) */
    0x01                                /* bInterval: 1ms polling */
};

/*
 * String Descriptors
 */
char const *string_desc_arr[] = {
    (char[]){0x09, 0x04},          /* 0: Language ID (English US = 0x0409, LE) */
    "Espressif",                   /* 1: Manufacturer */
    "BLE-USB HID Bridge",          /* 2: Product */
    "ESP32S3-KBD-MSE-001"          /* 3: Serial Number */
};

/* ====== TinyUSB Descriptor Callbacks ====== */

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return desc_hid_report;
}

/* ====== TinyUSB HID Callbacks ====== */

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t *buffer, uint16_t reqlen)
{
    (void)itf; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void)itf; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
    /* TODO: 可将键盘 LED 状态转发给 BLE 键盘 */
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len)
{
    (void)instance; (void)report; (void)len;
}
