/*
 * main.c - ESP32-S3 BLE HID Host -> USB HID Bridge (键盘 + 鼠标)
 *
 * 系统入口: 初始化 NVS -> USB HID Device -> BLE HID Host -> 开始扫描
 *
 * 数据流:
 *   蓝牙键盘/鼠标 --BLE--> ESP32-S3 (HID Host) --Queue--> USB HID Device --USB--> PC
 *
 * 硬件:
 *   - ESP32-S3 开发板 (原生 USB OTG)
 *   - BLE 蓝牙键盘/鼠标 (支持 BLE 的 HID 设备)
 *
 * 接线:
 *   - TTL/UART USB 口: 烧录固件 + 串口日志 (115200 baud)
 *   - OTG USB 口 (GPIO19/20): HID 键盘+鼠标复合设备 -> 电脑
 *   - 两个 USB 口同时连接电脑
 */

#include <stdio.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "ble_hid_host.h"
#include "usb_hid_device.h"

static const char *TAG = "MAIN";

/* HID 报告队列大小 */
#define REPORT_QUEUE_SIZE 32

void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "ESP32-S3 BLE->USB HID Bridge (键盘+鼠标)");
    ESP_LOGI(TAG, "========================================");

    /* 1. 初始化 NVS (存储 BLE 配对信息) */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 需要擦除并重新初始化");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS 初始化完成");

    /* 2. 创建 HID 报告队列 (BLE -> USB 数据通道) */
    QueueHandle_t report_queue = xQueueCreate(REPORT_QUEUE_SIZE, sizeof(hid_report_t));
    if (report_queue == NULL) {
        ESP_LOGE(TAG, "创建报告队列失败!");
        return;
    }
    ESP_LOGI(TAG, "报告队列创建完成 (size=%d)", REPORT_QUEUE_SIZE);

    /* 3. 初始化 USB HID 设备 (先初始化 USB, 确保电脑能尽快识别设备) */
    ESP_ERROR_CHECK(usb_hid_device_init(report_queue));

    /* 4. 初始化 BLE HID Host */
    ESP_ERROR_CHECK(ble_hid_host_init(report_queue));

    /* 5. 启动 BLE 扫描任务 */
    ble_hid_host_start();

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "系统初始化完成!");
    ESP_LOGI(TAG, "首次使用: 请将键盘/鼠标置于配对模式");
    ESP_LOGI(TAG, "配对成功后, 下次开机只需打开设备电源即可自动连接");
    ESP_LOGI(TAG, "========================================");
}
