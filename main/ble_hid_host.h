#ifndef BLE_HID_HOST_H
#define BLE_HID_HOST_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* HID 报告类型 (避免与 TinyUSB hid_report_type_t 冲突, 使用 app_ 前缀) */
typedef enum {
    APP_HID_TYPE_KEYBOARD = 0,
    APP_HID_TYPE_MOUSE = 1,
} app_hid_type_t;

/**
 * 统一 HID 报告结构 (支持键盘和鼠标)
 * 通过 type 字段区分报告类型
 */
typedef struct {
    app_hid_type_t type;
    union {
        /* 键盘报告 (8 字节, 与 BLE/USB HID 格式一致) */
        struct {
            uint8_t modifier;     /* Modifier keys bit flags */
            uint8_t reserved;     /* Always 0 */
            uint8_t keycode[6];   /* Up to 6 simultaneous keycodes */
        } keyboard;

        /* 鼠标报告 */
        struct {
            uint8_t buttons;      /* Button states (bit 0=left, 1=right, 2=middle) */
            int8_t x;             /* X movement (-127 to 127) */
            int8_t y;             /* Y movement (-127 to 127) */
            int8_t wheel;         /* Scroll wheel (-127 to 127) */
        } mouse;
    };
} __attribute__((packed)) hid_report_t;

/**
 * 初始化 BLE HID Host
 * @param report_queue FreeRTOS 队列, BLE 收到的 HID 报告将推入此队列
 * @return ESP_OK on success
 */
esp_err_t ble_hid_host_init(QueueHandle_t report_queue);

/**
 * 启动 BLE 扫描和连接任务 (非阻塞)
 * 创建后台 FreeRTOS 任务, 自动扫描并连接蓝牙 HID 设备 (键盘和鼠标)
 */
void ble_hid_host_start(void);

#endif /* BLE_HID_HOST_H */
