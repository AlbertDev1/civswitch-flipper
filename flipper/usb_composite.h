#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <furi_hal_usb.h>

typedef struct __attribute__((packed)) {
    uint16_t buttons;
    uint8_t hat, lx, ly, rx, ry, vendor;
} CivPadReport;

typedef struct __attribute__((packed)) {
    uint8_t modifier, reserved, key[6];
} CivKeyboardReport;

extern FuriHalUsbInterface civ_usb_interface;
bool civ_usb_connected(void);
bool civ_usb_send_pad(bool a_pressed);
bool civ_usb_send_keyboard(uint8_t modifier, uint8_t key);
void civ_usb_neutral(void);
