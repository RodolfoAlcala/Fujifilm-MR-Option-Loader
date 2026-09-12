#include "tusb.h"

#define USB_PID 0x4004
#define ITF_NUM_TOTAL 1
#define EPNUM_KEYBOARD 0x81
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0xCafe,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&desc_device;
}

uint8_t const desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return desc_hid_report;
}

uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_KEYBOARD,
                       sizeof(desc_hid_report), EPNUM_KEYBOARD,
                       CFG_TUD_HID_EP_BUFSIZE, 10)
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

static uint16_t desc_str[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    const char *text;

    if (index == 0) {
        desc_str[1] = 0x0409;
        desc_str[0] = (TUSB_DESC_STRING << 8) | 4;
        return desc_str;
    }

    if (index == 1) {
        text = "Pico";
    } else if (index == 2) {
        text = "Pico Keyboard";
    } else if (index == 3) {
        text = "Pico Keyboard";
    } else {
        return NULL;
    }

    uint8_t length = 0;
    while (text[length] != '\0' && length < 31) {
        desc_str[1 + length] = text[length];
        length++;
    }
    desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * length + 2);
    return desc_str;
}