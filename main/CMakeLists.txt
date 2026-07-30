# main component CMakeLists.txt
idf_component_register(
    SRCS
        "main.c"
        "ble_hid_host.c"
        "usb_hid_device.c"
        "usb_descriptors.c"
        "esp_hid_gap.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        esp_hid
        bt
        nvs_flash
)
