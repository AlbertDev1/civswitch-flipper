# Troubleshooting and hardware validation

## Frozen Flipper after USB test

The attached RogueMaster/API-86 device enumerated the app's composite descriptors, then became unresponsive during an attempted gamepad test. No input event was captured and the cause remains unknown. Do not relaunch this compatibility build until the UI issue is investigated. The [official Flipper recovery instructions](https://support.flipper.net/hc/en-us/articles/17914789056669-The-device-is-frozen) say to hold **LEFT and BACK together for 5 seconds**. If that fails, disconnect USB and hold **BACK for 30 seconds** for a hard reboot, then reconnect. Firmware flashing was not attempted in this project.

## Build and file errors

- `ufbt` downloads the official 1.5.1 SDK and toolchain on first run. It may need network access. Run it from the repository root, where `application.fam` resides.
- The `.fap` is tied to target F7 API 88.2. If the Flipper reports an API mismatch, update official firmware to 1.5.1 and rebuild against the matching SDK. Do not assume an old `.fap` works on a different firmware API.
- `Invalid macro header`, `Invalid macro size`, `Checksum or count mismatch`, or `Invalid operation` means the app refused the file before playback. Regenerate it with `tools/convert_payload.py`; copy the complete `.fmacro` file to the microSD card.
- If USB is locked by another app, exit that app or release the lock before starting CivSwitch. This app does not override another app's USB ownership.

## Linux USB host verification (no exploit)

Use a Linux host and a data-capable USB-C cable. Launch CivSwitch on the Flipper, leave it on `USB Connection Test`, and do not start the payload.

```bash
lsusb -d 0f0d:0092
lsusb -v -d 0f0d:0092
sudo usbhid-dump -d 0f0d:0092 -e descriptor
```

Confirm exactly one device, two HID interfaces, endpoints `0x02`, `0x81`, `0x83`, `0x03`, and report descriptor sizes 86 and 63. The VID/PID can also belong to a real HORI controller; unplug it to avoid inspecting the wrong device. Inspect `/sys/class/hidraw/hidrawN/device` to identify the Flipper interfaces. Then run the read-only harness on the selected device:

```bash
python3 tools/hid_host_inspect.py /dev/hidrawN --seconds 30
```

Use `Gamepad Test` to send one 50 ms A press and neutral report; verify both events. Open a harmless text editor with focus and use `Keyboard Test` to type one `a` or `1`; verify key-down and release. Repeat characters, Shift transitions, ESC, and Enter can then be checked with a non-exploit test macro generated from sample text. Confirm the app restores the previous USB mode when exited. Measure poll and report timing with USB capture equipment if exact on-wire timing matters.

On 2026-11-02, an attached Flipper running RogueMaster 0.420.0/API 86.0 successfully enumerated the isolated API-86 compatibility build as `0f0d:0092`. `lsusb -v` showed both interfaces and four endpoints, and the kernel's sysfs report descriptors matched the source byte-for-byte. The primary stock-1.5.1 FAP could not launch on that older firmware (`ApiTooNew`). Press/release input reports were not captured before the device disconnected, so those checks remain open. On hosts where `usbhid-dump` lacks permission, `/sys/class/hidraw/hidrawN/device/report_descriptor` provides a read-only copy of each live report descriptor.

## Nintendo Switch validation

Only after Linux verification, connect the Flipper directly to the user's Switch over USB-C. Verify the USB keyboard types into a benign text field first, then verify a single A press in a harmless UI. Check that both interfaces remain recognized after disconnect/reconnect. Finally load the official `civswitch.fmacro`, navigate to the intended game entry state, and start automation from the Flipper. No manual Joy-Con input should be needed during playback. Record the firmware version, Switch version, game version, USB descriptors, any missed characters, elapsed time, and whether the full sequence completed.

If USB disconnects during a line, the app requires an explicit resume. A report already in flight may have reached the host, so the line could be incomplete or duplicated. Inspect the target state and use Abort/Reset if uncertain; the app cannot infer the host's text buffer over HID.
