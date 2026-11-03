# USB and Pico pipeline analysis

## Pinned sources

| Project | Revision | Use |
| --- | --- | --- |
| [Gezine/CivSwitch](https://github.com/Gezine/CivSwitch/tree/2465a68cb22c38b11fb0b2205963641cad9809c1) | `2465a68cb22c38b11fb0b2205963641cad9809c1` | Pico descriptors, macro generator, compressor |
| [butterfossil/flipper-tomodachi-drawer](https://github.com/butterfossil/flipper-tomodachi-drawer/tree/23f762e8d1eda90e2d623ddd0b2235d99965fcd4) | `23f762e8d1eda90e2d623ddd0b2235d99965fcd4` | Read-only reference for custom Furi USB mode |
| [Official firmware 1.5.1](https://github.com/flipperdevices/flipperzero-firmware/tree/9e810dc399e027e48ce07751df72168abbb19065) | `9e810dc399e027e48ce07751df72168abbb19065` | Stock F7 API and build target |

The Tomodachi repository has no license file at this revision. Its source was not copied. The Pico descriptor bytes are generated from this repository's MIT-licensed `PicoSwitch/include/SwitchConsts.h`; the generator changes only EP0 packet size.

## Pico pipeline

`core.js` is minified with terser, compressed with raw DEFLATE, and encoded by `PicoSwitch/compress.js` into constrained base-25 chunks. The compressor emits one ASCII line per game chat batch, with a maximum of 254 bytes per line. The shipped core produced 87 lines in this environment. `PicoSwitch/tools/payload_to_macro.py` reads those lines and emits a C macro array. Its default mode uses one new HID keycode per report. A neutral 32 ms report is inserted before a repeated HID keycode, even if only the Shift modifier changes. Distinct keys follow one another directly after 32 ms. The experimental six-key packing mode is disabled upstream and is not used here.

The USB loop in `SwitchUsb.cpp` calls TinyUSB `tud_task()`, checks `tud_hid_n_ready()` separately for gamepad and keyboard, and sends current reports continuously. It attempts remote wakeup when suspended. The macro engine starts after `MACRO_START_DELAY_MS=1000` from boot, then advances steps using `to_ms_since_boot`. The README's approximate boot delay differs from this code value; this port deliberately starts only after user confirmation and USB configuration.

| Stage | Pico operation and duration |
| --- | --- |
| Preamble | A held 50 ms, neutral 550 ms, repeated five times |
| Each batch start | ESC held 32 ms; neutral 750 ms; A held 50 ms; neutral 550 ms |
| Typing | One ASCII key report for 32 ms; neutral 32 ms only before repeated HID keycode |
| Submit | Enter 32 ms; neutral 200 ms; Enter 32 ms |
| Between batches | neutral 750 ms; A held 50 ms; neutral 550 ms |
| Finish | neutral outputs, END |

No retry of a failed host keystroke is present in the Pico macro generator or engine. The Pico USB loop sends reports whenever an interface is ready. This port pauses on a USB send failure or loss of configuration and requires the user to resume or reset. It cannot know whether a report reached the host immediately before disconnection.

The Pico generator silently skips empty lines and removes unsupported characters. The converter rejects those cases so it cannot execute an altered payload. It accepts the exact upstream US QWERTY character set: letters, digits, space, tab, and the printable punctuation mapped in `payload_to_macro.py`. It rejects UTF-8/non-ASCII, CRLF, blank lines, and lines over 4096 bytes. The official compressor output passes unchanged.

## USB descriptors and report layouts

The Pico USB device is `0F0D:0092`, USB 2.00, per-interface class, one configuration, 500 mA advertised. The configuration is 73 bytes with two HID interfaces. Manufacturer is `PICO SWITCH PRO.`, product is `J24 CONTROLLER`.

| Interface | Class / subclass / protocol | HID report descriptor | Endpoints | Interrupt interval |
| --- | --- | --- | --- | --- |
| 0 gamepad | HID / nonboot / none | 86 bytes | OUT `0x02` 64 bytes; IN `0x81` 64 bytes | 1 ms |
| 1 keyboard | HID / boot / keyboard | 63 bytes | IN `0x83` 8 bytes; OUT `0x03` 8 bytes | 1 ms |

Gamepad input is 8 bytes: a 16-bit little-endian button field, four-bit hat plus four-bit padding, four 8-bit stick axes, and one 8-bit vendor field. A is button bit 2 (`0x0004`). Neutral is buttons=0, hat=8, axes=128, vendor=0. The report descriptor also declares an 8-byte vendor output report. The keyboard input is the standard 8-byte boot report: modifier byte, reserved byte, six keycodes. Left Shift is modifier bit 1 (`0x02`). ESC is key `0x29`, Enter `0x28`. A one-byte LED output report is declared. The gamepad and keyboard have no report ID prefixes.

TinyUSB handles standard enumeration on Pico. Its application callback chooses the report descriptor by interface. Its `GET_REPORT` callback supplies keyboard neutral state but constructs an uninitialized gamepad struct, and its gamepad `SET_REPORT` callback echoes host data. The Flipper driver returns its initialized current reports instead and consumes output reports without echoing them. This is a correctness and safety deviation, not a timing change.

## Stock Flipper USB findings

Official firmware 1.5.1 exposes `furi_hal_usb_set_config` to FAPs. The `usbd_reg_*`, `usbd_ep_*`, and `usbd_connect` helpers in the pinned `libusb_stm32` headers are `static inline`, so a custom FAP can use them without separate exported symbols. The app built and passed `APPCHK` against API 88.2 with no firmware patch.

The F7 firmware initializes EP0 with `USB_EP0_SIZE=8`; therefore the generated Flipper device descriptor advertises 8 rather than the Pico's 64. The USB stack has eight endpoint numbers; this configuration uses numbers 0, 1, 2, and 3, with the keyboard sharing number 3 in opposite directions. The STM32WB USB PMA is 1024 bytes according to its driver. The required 64+64+8+8-byte interrupt buffers and EP0 fit by source inspection. Endpoint configuration calls are checked at runtime. This is still not proof of live enumeration.

The driver handles interface-specific standard HID and report descriptor requests, GET_REPORT, SET_REPORT, SET_IDLE, GET_IDLE, and keyboard GET_PROTOCOL/SET_PROTOCOL. The keyboard remains an 8-byte boot report under both protocol modes. Each outgoing report waits for its endpoint's completion token before the next report is queued. The stock USB stack does not call the class configuration callback on bus reset, so connection status also checks the stack's device state and endpoint tokens are restored on reconfiguration. On configuration loss or suspend, the engine stops; it never resumes automatically. On exit, it sends neutral reports and restores the previous mode if it still owns USB. The app does not take the optional USB mode lock; the first live test reported an unresponsive UI while that lock was in use, though its cause was not established.

The Pico source demonstrates a two-interface controller/keyboard design intended for the Switch. It does not prove that this Flipper implementation, its 8-byte EP0, or its connection lifecycle is accepted by an actual Switch. The final gate is live host inspection followed by Switch hardware validation.
