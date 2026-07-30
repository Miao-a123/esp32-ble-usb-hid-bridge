/*
 * ble_hid_host.c - BLE HID Host 实现 (键盘 + 鼠标)
 *
 * 功能：扫描 BLE HID 设备, 连接蓝牙键盘和鼠标, 接收输入报告并转发到队列
 * 支持同时连接多个 BLE HID 设备 (最多 MAX_HID_DEVS 个)
 *
 * ESP32-S3 仅支持 BLE (不支持 BT Classic), 因此只能连接 BLE 设备
 */

#include "ble_hid_host.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_bt_device.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "esp_hidh.h"
#include "esp_hid_gap.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

static const char *TAG = "BLE_HID";

/* 最大同时连接的 HID 设备数 */
#define MAX_HID_DEVS 4

/* 内部状态 */
static QueueHandle_t s_report_queue = NULL;
static bool s_ble_initialized = false;

/* 已连接设备列表 */
static esp_hidh_dev_t *s_devs[MAX_HID_DEVS];
static int s_dev_count = 0;

/* 检查设备是否已在连接列表中 */
static bool is_dev_connected(const uint8_t *bda)
{
    for (int i = 0; i < s_dev_count; i++) {
        if (s_devs[i]) {
            const uint8_t *dev_bda = esp_hidh_dev_bda_get(s_devs[i]);
            if (dev_bda && memcmp(dev_bda, bda, ESP_BD_ADDR_LEN) == 0) {
                return true;
            }
        }
    }
    return false;
}

/* 添加设备到连接列表 */
static void add_dev(esp_hidh_dev_t *dev)
{
    for (int i = 0; i < MAX_HID_DEVS; i++) {
        if (s_devs[i] == NULL) {
            s_devs[i] = dev;
            if (i >= s_dev_count) {
                s_dev_count = i + 1;
            }
            return;
        }
    }
    ESP_LOGW(TAG, "设备列表已满, 无法添加");
}

/* 从连接列表移除设备 */
static void remove_dev(esp_hidh_dev_t *dev)
{
    for (int i = 0; i < MAX_HID_DEVS; i++) {
        if (s_devs[i] == dev) {
            s_devs[i] = NULL;
            break;
        }
    }
}

/*
 * 解析键盘报告并推入队列
 * @param report_id  设备报告 ID (0 = 无 Report ID)
 * @param data       原始报告数据 (BLE HID 通知数据, 不含 Report ID 字节)
 * @param length     数据长度
 */
static void handle_keyboard_input(uint16_t report_id, uint8_t *data, uint16_t length)
{
    /* BLE HID 通知数据不包含 Report ID 字节, data 直接从 modifier 开始 */
    if (length < 8) {
        ESP_LOGW(TAG, "键盘报告长度不足: %d", length);
        return;
    }

    hid_report_t report;
    memset(&report, 0, sizeof(report));
    report.type = APP_HID_TYPE_KEYBOARD;

    report.keyboard.modifier = data[0];
    report.keyboard.reserved = data[1];
    memcpy(report.keyboard.keycode, &data[2], 6);

    if (s_report_queue) {
        xQueueSend(s_report_queue, &report, 0);
    }

    ESP_LOGI(TAG, "KBD MOD:0x%02x KC:%02x %02x %02x %02x %02x %02x",
             report.keyboard.modifier,
             report.keyboard.keycode[0], report.keyboard.keycode[1],
             report.keyboard.keycode[2], report.keyboard.keycode[3],
             report.keyboard.keycode[4], report.keyboard.keycode[5]);
}

/*
 * 解析鼠标报告并推入队列
 * @param report_id  设备报告 ID (0 = 无 Report ID)
 * @param data       原始报告数据 (BLE HID 通知数据, 不含 Report ID 字节)
 * @param length     数据长度
 */
static void handle_mouse_input(uint16_t report_id, uint8_t *data, uint16_t length)
{
    /* BLE HID 通知数据不包含 Report ID 字节, data 直接从 buttons 开始 */
    if (length < 3) {
        ESP_LOGW(TAG, "鼠标报告长度不足: %d", length);
        return;
    }

    hid_report_t report;
    memset(&report, 0, sizeof(report));
    report.type = APP_HID_TYPE_MOUSE;

    report.mouse.buttons = data[0];
    report.mouse.x = (int8_t)data[1];
    report.mouse.y = (int8_t)data[2];
    report.mouse.wheel = 0;

    /* 如果有滚轮数据 */
    if (length >= 4) {
        report.mouse.wheel = (int8_t)data[3];
    }

    if (s_report_queue) {
        xQueueSend(s_report_queue, &report, 0);
    }

    /* 仅在按键状态变化时输出日志, 避免高频移动报告淹没日志 */
    static uint8_t last_buttons = 0;
    if (report.mouse.buttons != last_buttons) {
        ESP_LOGI(TAG, "MSE BTN:0x%02x X:%d Y:%d W:%d",
                 report.mouse.buttons, report.mouse.x,
                 report.mouse.y, report.mouse.wheel);
        last_buttons = report.mouse.buttons;
    }
}

/*
 * HID Host 事件回调
 * 在 Bluedroid 事件循环上下文中调用
 */
static void hidh_callback(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_hidh_event_t event = (esp_hidh_event_t)id;
    esp_hidh_event_data_t *param = (esp_hidh_event_data_t *)event_data;

    switch (event) {
    case ESP_HIDH_OPEN_EVENT: {
        if (param->open.status == ESP_OK) {
            const uint8_t *bda = esp_hidh_dev_bda_get(param->open.dev);
            const char *name = esp_hidh_dev_name_get(param->open.dev);
            ESP_LOGI(TAG, "设备已连接: %s [" ESP_BD_ADDR_STR "]",
                     name ? name : "Unknown", ESP_BD_ADDR_HEX(bda));
            /* dump 设备信息 (报告映射, 特征值等) */
            esp_hidh_dev_dump(param->open.dev, stdout);
            add_dev(param->open.dev);
        } else {
            ESP_LOGE(TAG, "连接失败, status=0x%x", param->open.status);
        }
        break;
    }

    case ESP_HIDH_INPUT_EVENT: {
        uint8_t *data = param->input.data;
        uint16_t length = param->input.length;
        uint16_t report_id = param->input.report_id;

        /* 根据 usage 分发到对应的处理器 */
        if (param->input.usage == ESP_HID_USAGE_KEYBOARD) {
            handle_keyboard_input(report_id, data, length);
        } else if (param->input.usage == ESP_HID_USAGE_MOUSE) {
            handle_mouse_input(report_id, data, length);
        } else {
            /* usage 为 GENERIC 时, 根据报告长度推断类型 */
            if (length >= 3 && length <= 5) {
                handle_mouse_input(report_id, data, length);
            } else if (length >= 8) {
                handle_keyboard_input(report_id, data, length);
            }
        }
        break;
    }

    case ESP_HIDH_CLOSE_EVENT: {
        const uint8_t *bda = esp_hidh_dev_bda_get(param->close.dev);
        const char *reason_str = "未知";
        switch (param->close.reason) {
            case 0x13: reason_str = "PEER_USER_TERM (设备主动断开)"; break;
            case 0x16: reason_str = "CONN_TOUT (连接超时)"; break;
            case 0x08: reason_str = "CONN_TIMEOUT (超时)"; break;
            case 0x22: reason_str = "CONN_FAIL (连接失败)"; break;
            case 0x3e: reason_str = "CONN_LMP_TIMEOUT (LMP超时)"; break;
            default: break;
        }
        ESP_LOGW(TAG, "设备已断开 [" ESP_BD_ADDR_STR "], reason=%d (%s)",
                 ESP_BD_ADDR_HEX(bda), param->close.reason, reason_str);

        remove_dev(param->close.dev);

        /* 发送全零键盘报告 (释放所有按键) */
        if (s_report_queue) {
            hid_report_t release;
            memset(&release, 0, sizeof(release));
            release.type = APP_HID_TYPE_KEYBOARD;
            xQueueSend(s_report_queue, &release, 0);

            /* 发送全零鼠标报告 (释放所有按钮) */
            memset(&release, 0, sizeof(release));
            release.type = APP_HID_TYPE_MOUSE;
            xQueueSend(s_report_queue, &release, 0);
        }

        esp_hidh_dev_free(param->close.dev);
        break;
    }

    case ESP_HIDH_BATTERY_EVENT: {
        ESP_LOGI(TAG, "电池电量: %d%%", param->battery.level);
        break;
    }

    default:
        ESP_LOGD(TAG, "HIDH 事件: %d", event);
        break;
    }
}

/*
 * 尝试重连已绑定的设备 (Bonding 重连)
 * 从 NVS 读取已配对设备列表, 直接发起连接
 * 适用于静态随机地址的设备; RPA 设备需扫描发现
 * 如果绑定设备过多 (旧数据), 自动清除后重新开始
 * @return 成功发起的重连数
 */
static int try_bonded_reconnect(void)
{
    int num_bonded = esp_ble_get_bond_device_num();
    if (num_bonded <= 0) {
        return 0;
    }

    /* 绑定设备过多: 旧配对数据, 清除后重新开始
     * (旧数据来自 bonding 启用前的无效配对) */
    if (num_bonded > MAX_HID_DEVS) {
        ESP_LOGW(TAG, "已绑定设备过多 (%d 个), 清除旧绑定数据...", num_bonded);
        esp_ble_bond_dev_t *bonded_devs = malloc(sizeof(esp_ble_bond_dev_t) * num_bonded);
        if (bonded_devs) {
            int actual_num = num_bonded;
            esp_ble_get_bond_device_list(&actual_num, bonded_devs);
            for (int i = 0; i < actual_num; i++) {
                esp_ble_remove_bond_device(bonded_devs[i].bd_addr);
            }
            free(bonded_devs);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
        ESP_LOGI(TAG, "旧绑定已清除, 将重新配对");
        return 0;
    }

    ESP_LOGI(TAG, "发现 %d 个已绑定设备, 尝试重连...", num_bonded);

    esp_ble_bond_dev_t *bonded_devs = malloc(sizeof(esp_ble_bond_dev_t) * num_bonded);
    if (!bonded_devs) {
        ESP_LOGE(TAG, "分配绑定设备列表内存失败");
        return 0;
    }

    int actual_num = num_bonded;
    esp_ble_get_bond_device_list(&actual_num, bonded_devs);

    int reconnected = 0;
    for (int i = 0; i < actual_num; i++) {
        if (is_dev_connected(bonded_devs[i].bd_addr)) {
            ESP_LOGD(TAG, "  [" ESP_BD_ADDR_STR "] 已连接, 跳过",
                     ESP_BD_ADDR_HEX(bonded_devs[i].bd_addr));
            continue;
        }

        /* 检查是否有空闲槽位 */
        bool has_slot = false;
        for (int j = 0; j < MAX_HID_DEVS; j++) {
            if (s_devs[j] == NULL) {
                has_slot = true;
                break;
            }
        }
        if (!has_slot) break;

        ESP_LOGI(TAG, "  重连: [" ESP_BD_ADDR_STR "] type=%d",
                 ESP_BD_ADDR_HEX(bonded_devs[i].bd_addr), bonded_devs[i].bd_addr_type);

        esp_hidh_dev_t *dev = esp_hidh_dev_open(
            bonded_devs[i].bd_addr, ESP_HID_TRANSPORT_BLE,
            bonded_devs[i].bd_addr_type);

        if (dev != NULL) {
            reconnected++;
        }
    }

    free(bonded_devs);
    return reconnected;
}

/*
 * BLE 扫描并连接任务
 * 循环: 优先重连已绑定设备 -> 扫描 -> 连接所有发现的 BLE HID 设备 -> 等待 -> 重新扫描
 */
static void ble_hid_host_task(void *arg)
{
    bool first_run = true;

    while (1) {
        /* 检查已连接设备数 */
        int active_count = 0;
        for (int i = 0; i < MAX_HID_DEVS; i++) {
            if (s_devs[i] != NULL) {
                active_count++;
            }
        }

        if (active_count >= MAX_HID_DEVS) {
            /* 连接数已满, 跳过扫描 */
            ESP_LOGI(TAG, "已有 %d 个设备连接 (已满), 跳过扫描", active_count);
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }

        /* 开机首次: 尝试重连已绑定设备 (Bonding - 避免重新配对)
         * 直接连接存储在 NVS 中的设备地址, 无需扫描
         * 适用于静态地址设备; RPA 设备仍需扫描发现 */
        if (first_run) {
            first_run = false;
            int reconnected = try_bonded_reconnect();
            if (reconnected > 0) {
                ESP_LOGI(TAG, "已绑定设备重连: 发起 %d 个连接, 等待结果...", reconnected);
                /* 等待连接完成 (OPEN/CLOSE 事件异步到达) */
                vTaskDelay(pdMS_TO_TICKS(5000));

                /* 重新计数已连接设备 */
                active_count = 0;
                for (int i = 0; i < MAX_HID_DEVS; i++) {
                    if (s_devs[i] != NULL) {
                        active_count++;
                    }
                }
                ESP_LOGI(TAG, "已绑定设备重连后: %d 个已连接", active_count);
            }
        }

        /* 自适应扫描参数:
         * 无设备连接: 15s 扫描 + 5s 间隔 (激进发现)
         * 已有设备连接: 10s 扫描 + 10s 间隔 (温和, 减少 BLE radio 争用) */
        uint32_t scan_duration = (active_count == 0) ? 15 : 10;
        uint32_t rescan_delay  = (active_count == 0) ? 5 : 10;

        ESP_LOGI(TAG, "开始扫描 BLE 设备 (%lu 秒, 已连接 %d 个)...",
                 (unsigned long)scan_duration, active_count);

        size_t results_len = 0;
        esp_hid_scan_result_t *results = NULL;

        esp_err_t ret = esp_hid_scan(scan_duration, &results_len, &results);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "扫描失败: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(5000));
            continue;
        }

        ESP_LOGI(TAG, "扫描完成, 发现 %u 个设备", (unsigned)results_len);

        if (results_len == 0) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        /* 遍历结果, 连接所有未连接的 BLE HID 设备 */
        int new_connections = 0;
        esp_hid_scan_result_t *r = results;

        while (r) {
            const char *transport_str = (r->transport == ESP_HID_TRANSPORT_BLE) ? "BLE" : "BT";
            ESP_LOGI(TAG, "  发现: [%s] %s, RSSI=%d, usage=%s",
                     transport_str,
                     r->name ? r->name : "Unknown",
                     r->rssi,
                     esp_hid_usage_str(r->usage));

            /* 只连接 BLE HID 设备 (ESP32-S3 不支持 BT Classic) */
            if (r->transport == ESP_HID_TRANSPORT_BLE) {
                if (is_dev_connected(r->bda)) {
                    ESP_LOGD(TAG, "  已连接, 跳过");
                } else if (s_dev_count < MAX_HID_DEVS) {
                    /* 检查是否有空闲槽位 */
                    bool has_slot = false;
                    for (int i = 0; i < MAX_HID_DEVS; i++) {
                        if (s_devs[i] == NULL) {
                            has_slot = true;
                            break;
                        }
                    }

                    if (has_slot) {
                        ESP_LOGI(TAG, "  正在连接: %s...",
                                 r->name ? r->name : "Unknown");
                        esp_hidh_dev_t *dev = esp_hidh_dev_open(r->bda, r->transport,
                                                                 r->ble.addr_type);
                        if (dev != NULL) {
                            new_connections++;

                            /* 更新连接参数: 低延迟 HID 配置
                             * min_int=7.5ms, max_int=15ms, latency=0, timeout=5s */
                            esp_ble_conn_update_params_t conn_params = {0};
                            memcpy(conn_params.bda, r->bda, ESP_BD_ADDR_LEN);
                            conn_params.min_int = 0x06;    /* 7.5ms */
                            conn_params.max_int = 0x0C;    /* 15ms */
                            conn_params.latency = 0;       /* 无延迟 */
                            conn_params.timeout = 500;     /* 5000ms */
                            esp_err_t upd_ret = esp_ble_gap_update_conn_params(&conn_params);
                            if (upd_ret != ESP_OK) {
                                ESP_LOGD(TAG, "  连接参数更新请求失败: %s", esp_err_to_name(upd_ret));
                            }
                        } else {
                            ESP_LOGW(TAG, "  连接请求失败");
                        }
                    }
                } else {
                    ESP_LOGW(TAG, "  设备数已达上限 (%d), 跳过", MAX_HID_DEVS);
                }
            }
            r = r->next;
        }

        /* 释放扫描结果 */
        if (results) {
            esp_hid_scan_results_free(results);
        }

        if (new_connections > 0) {
            ESP_LOGI(TAG, "发起了 %d 个新连接", new_connections);
        }

        /* 等待一段时间后重新扫描 (发现新设备或重连断开的设备) */
        vTaskDelay(pdMS_TO_TICKS(rescan_delay * 1000));
    }
}

esp_err_t ble_hid_host_init(QueueHandle_t report_queue)
{
    if (report_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_report_queue = report_queue;

    /* 初始化设备列表 */
    memset(s_devs, 0, sizeof(s_devs));
    s_dev_count = 0;

    /*
     * 1. 初始化 HID GAP
     * esp_hid_gap_init() 内部调用 init_low_level(), 自动完成:
     *   - BT 控制器初始化与启用 (ESP_BT_MODE_BLE)
     *   - Bluedroid 协议栈初始化 (esp_bluedroid_init_with_cfg) 与启用
     *   - BLE GAP 回调注册
     */
    esp_err_t ret = esp_hid_gap_init(HID_HOST_MODE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HID GAP 初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 2. 设置 SMP 安全参数 (启用 Bonding - 配对一次后自动重连)
     * Bonding 密钥存储在 NVS (CONFIG_BT_BLE_SMP_BOND_NVS_FLASH=y)
     * 断电重启后, ESP32 可直接用存储的密钥加密连接, 无需重新配对 */
    {
        /* 认证要求: Bonding, 无 MITM (Just Works, 适用于 BLE HID 设备) */
        esp_ble_auth_req_t auth_req = ESP_LE_AUTH_BOND;
        esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(auth_req));

        /* IO 能力: 无输入输出 -> Just Works 配对 (无需用户交互) */
        esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
        esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(iocap));

        /* 密钥请求: 加密密钥 (LTK) + 身份密钥 (IRK, 用于 RPA 地址解析) */
        uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
        esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(init_key));

        uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
        esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(rsp_key));

        /* 最大密钥长度: 16 字节 (AES-128) */
        uint8_t key_size = 16;
        esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size));
    }

    /* 3. 注册 GATTC 回调 (BLE HID Host 需要接收 GATT 通知) */
#if CONFIG_BT_BLE_ENABLED
    ret = esp_ble_gattc_register_callback(esp_hidh_gattc_event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GATTC 回调注册失败: %s", esp_err_to_name(ret));
        return ret;
    }
#endif

    /* 4. 初始化 HID Host */
    esp_hidh_config_t hidh_config = {
        .callback = hidh_callback,
        .event_stack_size = 4096,
        .callback_arg = NULL,
    };
    ret = esp_hidh_init(&hidh_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HID Host 初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    /* 打印本地蓝牙地址 */
    const uint8_t *addr = esp_bt_dev_get_address();
    ESP_LOGI(TAG, "BLE HID Host 已初始化, 本地地址: " ESP_BD_ADDR_STR,
             ESP_BD_ADDR_HEX(addr));

    s_ble_initialized = true;
    return ESP_OK;
}

void ble_hid_host_start(void)
{
    if (!s_ble_initialized) {
        ESP_LOGE(TAG, "BLE HID Host 未初始化");
        return;
    }

    BaseType_t ret = xTaskCreate(ble_hid_host_task, "ble_hid_task",
                                  6 * 1024, NULL, 2, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建 BLE 任务失败");
    }
}
