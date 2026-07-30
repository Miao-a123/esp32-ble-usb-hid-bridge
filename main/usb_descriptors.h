#ifndef USB_DESCRIPTORS_H
#define USB_DESCRIPTORS_H

#include <stdint.h>
#include "tusb.h"

/* HID Report IDs (通过 Report ID 区分键盘和鼠标) */
#define REPORT_ID_KEYBOARD 1
#define REPORT_ID_MOUSE    2

/* HID Report Descriptor (键盘 + 鼠标) */
extern uint8_t const desc_hid_report[];

/* USB Device Descriptor */
extern tusb_desc_device_t const desc_device;

/* USB Configuration Descriptor (Config + Interface + HID + Endpoint) */
extern uint8_t const desc_configuration[];

/* Configuration descriptor 总长度: 9 + 9 + 9 + 7 = 34 bytes */
#define CONFIG_TOTAL_LEN  34

/* String descriptor 数组 */
extern char const *string_desc_arr[];

/* 字符串描述符数量 (含 Language) */
#define STRING_DESC_ARR_SIZE 4

#endif /* USB_DESCRIPTORS_H */
