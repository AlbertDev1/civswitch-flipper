# CivSwitch native Flipper Zero app

This is a Flipper Zero F7 external app for a gamepad plus US keyboard USB HID macro. It targets official firmware **1.5.1** and its API **88.2**. A firmware patch is not needed for the pinned SDK. The compiled FAP is at `../dist/civswitch_flipper.fap` after building from the repository root.

## Build

Install the [official uFBT tool](https://github.com/flipperdevices/flipperzero-ufbt), then run from the repository root:

```bash
ufbt
python3 -m unittest discover -s tests -v
```

The `scripts/build_flipper.sh` and `scripts/test_host.sh` wrappers run the same commands. The first build downloads the official SDK and ARM toolchain. The source manifest is the root `application.fam`.

## Make and install a payload

A prebuilt `dist/civswitch.fmacro` is already included, so you can skip this section unless you want
to regenerate it. The `PicoSwitch/` directory is **not** part of this port repo — it lives in the
upstream CivSwitch project. Get it from
[github.com/Gezine/CivSwitch](https://github.com/Gezine/CivSwitch) (the `PicoSwitch/` folder,
specifically `PicoSwitch/compress.js` and `PicoSwitch/tools/payload_to_macro.py`) and place that
`PicoSwitch/` tree at the root of this repo before running the commands below:

```bash
# PicoSwitch/ comes from https://github.com/Gezine/CivSwitch (copied to the repo root)
cd PicoSwitch
npm install
node compress.js
cd ..
python3 tools/convert_payload.py --input PicoSwitch/payload.txt --output dist/civswitch.fmacro
```

Copy `dist/civswitch_flipper.fap` to `/ext/apps/USB/` on the Flipper's microSD card and copy `dist/civswitch.fmacro` to `/ext/apps_data/civswitch/`. The application creates the data folder when launched. The payload is not embedded in the FAP. You may place multiple `.fmacro` files in the folder and choose one in `Load Payload`.

The `.fmacro` format is little-endian: a 24-byte header (`FMAC`, version 1, record width 8, record count, line count, total duration, CRC32) followed by fixed eight-byte operation records (opcode, modifier, key or ASCII byte, reserved zero, duration). The app checks the full file length, every operation, line count, duration, and CRC32 before enabling playback. It then streams one operation at a time, with a 1,000,000-record cap and no payload-sized RAM allocation. TYPE_ASCII operations carry the original source byte and are translated on the Flipper. The converter rejects unsupported characters rather than dropping them.

## Controls

- **Load Payload:** pick a `.fmacro` in the app data folder. It is validated before it can run.
- **Start Automation:** review the confirmation screen, then press OK to begin. USB must be configured.
- **USB Connection Test:** show whether a USB host configured the composite device.
- **Gamepad Test:** press OK to send one A press for 50 ms and a release.
- **Keyboard Test:** press OK to send one key for 32 ms and a release. The test key is `a` or `1`.
- **Settings:** Up/Down changes the harmless test key; it does not change macro timing.
- **About:** version purpose and validation reminder.
- **During playback:** OK requests pause at the next operation boundary; OK resumes. Back opens an abort confirmation and pauses at the next boundary. Confirm abort with OK, or cancel with Back. A USB disconnect stops playback and requires an explicit OK after reconnection. The app never resumes automatically.

The run screen shows USB status, macro state, current line, percentage, elapsed time, estimated remaining duration from the operation sequence, and an error if one occurs. When a run completes, press OK to reset it before another deliberate start.

## Limitations

The primary stock-1.5.1 build passed host compilation and simulated macro tests. A separate API-86 test build enumerated on a connected Flipper running RogueMaster 0.420.0; its gamepad and keyboard descriptors matched the source through Linux sysfs. Linux press/release events and Nintendo Switch acceptance remain unverified. The application does not know whether a report in flight reached the host during a disconnect. Resume is manual and may require inspection of the target text field; Abort/Reset returns to the first operation.

See [USB analysis](../docs/USB_ANALYSIS.md) and [troubleshooting](../docs/TROUBLESHOOTING.md) for descriptors, timing, and hardware checks.
