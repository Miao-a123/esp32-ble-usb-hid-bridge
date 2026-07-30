#ifndef USB_HID_DEVICE_H
#define USB_HID_DEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "ble_hid_host.h"

/**
 * 初始化 USB HID 设备 (TinyUSB)
 * 配置 ESP32-S3 原生 USB OTG 为复合 HID 设备 (键盘 + 鼠标)
 * @param report_queue FreeRTOS 队列, 从中读取 HID 报告转发到 USB
 * @return ESP_OK on success
 */
esp_err_t usb_hid_device_init(QueueHandle_t report_queue);

#endif /* USB_HID_DEVICE_H */
