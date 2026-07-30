/*
 * usb_hid_device.c - USB HID Device 实现 (键盘 + 鼠标)
 *
 * 使用 esp_tinyusb (TinyUSB) 配置 ESP32-S3 原生 USB OTG 为复合 HID 设备
 * 从 FreeRTOS 队列读取 HID 报告 (键盘或鼠标), 通过 USB 转发给电脑
 *
 * 硬件: ESP32-S3 原生 USB (D+ = GPIO20, D- = GPIO19)
 */

#include "usb_hid_device.h"
#include "usb_descriptors.h"

#include "esp_log.h"
#include "tinyusb.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "USB_HID";

static QueueHandle_t s_report_queue = NULL;

/*
 * USB HID 转发任务
 * 从队列读取 HID 报告 (键盘或鼠标), 通过 TinyUSB 发送给电脑
 */
static void usb_hid_task(void *arg)
{
    hid_report_t report;
    static uint32_t kbd_count = 0;
    static uint32_t mse_count = 0;

    while (1) {
        /* 阻塞等待队列中的 HID 报告 */
        if (xQueueReceive(s_report_queue, &report, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        /* 检查 USB 是否已连接且 HID 就绪 */
        if (!tud_mounted()) {
            ESP_LOGD(TAG, "USB 未连接, 丢弃报告");
            continue;
        }

        /* 等待 HID 端点就绪 (最多等待 50ms) */
        int wait_ms = 0;
        while (!tud_hid_ready() && wait_ms < 50) {
            vTaskDelay(pdMS_TO_TICKS(1));
            wait_ms++;
        }

        if (!tud_hid_ready()) {
            ESP_LOGW(TAG, "USB HID 未就绪, 丢弃报告");
            continue;
        }

        /* 根据报告类型发送 */
        if (report.type == APP_HID_TYPE_KEYBOARD) {
            kbd_count++;
            if (kbd_count <= 5 || (kbd_count % 50) == 0) {
                ESP_LOGI(TAG, "USB KBD #%lu: mod=0x%02x kc=%02x %02x %02x %02x %02x %02x",
                         (unsigned long)kbd_count, report.keyboard.modifier,
                         report.keyboard.keycode[0], report.keyboard.keycode[1],
                         report.keyboard.keycode[2], report.keyboard.keycode[3],
                         report.keyboard.keycode[4], report.keyboard.keycode[5]);
            }
            /* 发送键盘报告 (Report ID = 1) */
            bool ok = tud_hid_keyboard_report(REPORT_ID_KEYBOARD,
                                    report.keyboard.modifier,
                                    report.keyboard.keycode);
            if (!ok) {
                ESP_LOGW(TAG, "键盘报告发送失败 (tud_hid_keyboard_report 返回 false)");
            }
        } else if (report.type == APP_HID_TYPE_MOUSE) {
            mse_count++;
            /* 发送鼠标报告 (Report ID = 2) */
            tud_hid_mouse_report(REPORT_ID_MOUSE,
                                 report.mouse.buttons,
                                 report.mouse.x,
                                 report.mouse.y,
                                 report.mouse.wheel,
                                 0);  /* horizontal scroll = 0 */
        }
    }
}

esp_err_t usb_hid_device_init(QueueHandle_t report_queue)
{
    if (report_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_report_queue = report_queue;

    ESP_LOGI(TAG, "初始化 USB HID 设备 (键盘+鼠标)...");

    /*
     * 配置 TinyUSB
     * 复合 HID 设备: 键盘 (Report ID=1) + 鼠标 (Report ID=2)
     */
    tinyusb_config_t tusb_cfg = {
        .device_descriptor = &desc_device,
        .string_descriptor = (const char **)string_desc_arr,
        .string_descriptor_count = STRING_DESC_ARR_SIZE,
        .external_phy = false,
        .configuration_descriptor = desc_configuration,
    };

    esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "TinyUSB 初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "USB HID 复合设备已就绪 (VID=0x303A PID=0x4002, 键盘+鼠标)");

    /* 创建 USB 转发任务 (高优先级, 确保低延迟) */
    BaseType_t task_ret = xTaskCreate(usb_hid_task, "usb_hid_task",
                                       4 * 1024, NULL, 5, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "创建 USB 任务失败");
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
