#!/usr/bin/env python3
"""Copy the four MIT-licensed Pico HID descriptor arrays into the Flipper source."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
source = (root / "PicoSwitch/include/SwitchConsts.h").read_text()
names = ("switch_usb_device_descriptor", "switch_usb_configuration_descriptor",
         "switch_usb_report_descriptor", "keyboard_usb_report_descriptor")
out = ["/* Generated from CivSwitch PicoSwitch/include/SwitchConsts.h at 2465a68.",
       " * Original Switch descriptor is MIT-licensed, copyright Jason Skuby.",
       " * See repository LICENSE and PicoSwitch/include/SwitchConsts.h.",
       " */", "#pragma once", "#include <stdint.h>", ""]
for name in names:
    match = re.search(r"static const uint8_t " + name + r"\[\] = \{.*?\n\};", source, re.S)
    if not match:
        raise SystemExit(f"missing {name}")
    array = match.group()
    if name == "switch_usb_device_descriptor":
        # Stock F7 USB HAL calls usbd_init(..., USB_EP0_SIZE=8, ...).
        array = array.replace("0x40,  // bMaxPacketSize0 64", "0x08,  // bMaxPacketSize0 8 (stock Flipper HAL)")
    out.extend((array, ""))
(root / "flipper/usb_descriptors.h").write_text("\n".join(out))
