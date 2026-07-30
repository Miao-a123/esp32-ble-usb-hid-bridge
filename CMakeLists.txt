# ESP32-S3 BLE HID Host -> USB HID Keyboard Bridge
# Top-level CMakeLists.txt
cmake_minimum_required(VERSION 3.16)

# Include ESP-IDF build system
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(esp_ble_usb_hid)
