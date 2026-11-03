# CivSwitch for Flipper Zero

A native **Flipper Zero** app that replays the [CivSwitch](https://github.com/Gezine/CivSwitch)
chat‑XSS payload over USB, as a drop‑in replacement for the Raspberry Pi Pico used by the
original project. It presents the same composite **gamepad + US keyboard** HID device
(`0F0D:0092`, a HORIPAD‑style controller) and types the payload into the Nintendo Switch
Civilization 7 multiplayer chat, exactly as the upstream PicoSwitch firmware does.

This repository is a port and companion tooling. The exploit itself, its payload, and the
compression/transport pipeline are the work of the original CivSwitch author — see
[Credits & attribution](#credits--attribution).

📹 **Demo video:** [CivSwitch on Flipper Zero (r/switch2hacks)](https://www.reddit.com/r/switch2hacks/comments/1xmimlp/comment/19s3agkg0ordv/)

> ⚠️ **Legal / safety notice — use entirely at your own risk.**
>
> - **The bundled payload is upstream CivSwitch's work, not ours.** This repository ships a prebuilt
>   `dist/civswitch.fmacro` (the Remote JS Loader chat‑XSS payload) together with the Flipper app
>   that types it. The payload itself and its compression/transport pipeline are the work of the
>   upstream [CivSwitch](https://github.com/Gezine/CivSwitch) author — this project only ports the
>   *typing mechanism* to the Flipper Zero and re‑packages that payload into the `.fmacro` format
>   the app plays.
> - This is a security‑research and homebrew tool for consoles and game copies **you own**,
>   intended for authorized testing only. The macro drives real USB HID input into a Nintendo
>   Switch; run it only on hardware you are allowed to modify.
> - **The author accepts no responsibility or liability for any damage, data loss, bricked
>   hardware, bans, or legal consequences arising from use or misuse of this software.** It is
>   provided "as is", without warranty of any kind (see [`LICENSE`](LICENSE)). You use it entirely
>   at your own risk.
> - Nothing here is affiliated with or endorsed by Nintendo, HORI, or Flipper Devices.

---

## What this is

The upstream **CivSwitch** project has two parts: `PicoSwitch` (Pico firmware that types a
chat‑XSS payload into the Switch over USB HID) and `installer-creator` (which turns that one‑shot
chat exploit into a boot‑persistent trigger). This repository re‑implements the **PicoSwitch
“type the payload” role** on a Flipper Zero instead of a Pico, so you don't need a separate Pico
board.

The Flipper app:

- enumerates as the same two‑interface composite HID device the Switch expects
  (interface 0 gamepad, interface 1 boot keyboard);
- loads a pre‑converted, length‑checked, CRC32‑validated **`.fmacro`** payload from the microSD
  card and streams it one HID report at a time (no payload‑sized RAM allocation);
- reproduces the Pico macro's exact operation order and timing (32 ms key holds, 50 ms A holds,
  the 750/550/200 ms batch gaps, etc.);
- exposes manual **start / pause / resume / abort / reset** control and stops automatically on
  USB disconnect — it never resumes on its own.

As of the last update the project is **not yet Switch‑hardware accepted**: composite USB
enumeration and a PC gamepad/keyboard dry‑run passed on device, but on‑device payload playback and
the Nintendo Switch acceptance test are still pending. Treat it as a work in progress before you
rely on it for a real run.

---

## How it works (payload pipeline)

```
core.js  ──(upstream CivSwitch: terser → deflateRaw → base‑25 chunks)──►  payload.txt   (~87 chat lines)
payload.txt  ──(tools/convert_payload.py)──►  dist/civswitch.fmacro        (checksummed HID op stream)
dist/civswitch.fmacro  ──(copy to microSD)──►  Flipper app streams it as USB HID into the Switch
```

- **`.fmacro` format** — little‑endian, a 24‑byte header (`FMAC`, version, record width 8,
  record count, line count, total duration, CRC32) followed by fixed 8‑byte operation records
  (opcode, modifier, HID key / ASCII byte, reserved, duration ms). The app validates file length,
  every operation, line count, duration, and CRC32 **before** playback is enabled. `TYPE_ASCII`
  records carry the original source byte and are translated to HID on the Flipper. The converter
  **rejects** unsupported characters (non‑ASCII, CRLF, blank lines, over‑long lines) instead of
  silently dropping them, so it cannot run an altered payload.

The full USB descriptor and timing analysis is in [`docs/USB_ANALYSIS.md`](docs/USB_ANALYSIS.md).

---

## Repository layout

| Path | Contents |
| --- | --- |
| `application.fam` | Flipper app manifest (appid `civswitch_flipper`, category USB). |
| `flipper/` | App source: `civswitch.c` (UI + control), `macro_engine.c`, `usb_composite.c`, `ascii_hid.c`, `usb_descriptors.h`, and [`flipper/README.md`](flipper/README.md). |
| `tools/` | `convert_payload.py` (payload → `.fmacro`), `generate_usb_descriptors.py`, `hid_host_inspect.py` (read‑only Linux HID harness). |
| `tests/` | Host unit tests (`test_converter.py`, `native_engine.c`) and stub headers. |
| `scripts/` | `build_flipper.sh`, `test_host.sh` convenience wrappers. |
| `dist/` | Prebuilt release `.fap` binaries and the prebuilt `civswitch.fmacro` payload, ready to copy to the Flipper. |
| `docs/` | `USB_ANALYSIS.md`, `TROUBLESHOOTING.md`. |

> **Note:** the upstream `PicoSwitch/` and `installer-creator/` directories are **not checked in
> here** — this repo only contains the Flipper port and shared tooling. The payload‑generation
> tools (`convert_payload.py`, `generate_usb_descriptors.py`) import from the upstream
> `PicoSwitch/` sources, so to **regenerate** a `.fmacro` from scratch you need a checkout of
> upstream CivSwitch next to these tools. To just run the app, use the prebuilt
> `dist/civswitch.fmacro`.

---

## Requirements

- A **Flipper Zero** (F7).
- A **data‑capable USB‑C cable**.
- To build: the official [**uFBT**](https://github.com/flipperdevices/flipperzero-ufbt) tool
  (`pip install ufbt`). The first build downloads the official SDK and ARM toolchain.
- To run the host tests: Python 3.
- To regenerate a payload: Node.js + npm and a checkout of upstream CivSwitch (for
  `PicoSwitch/compress.js` and `PicoSwitch/tools/payload_to_macro.py`).

### Firmware compatibility

The primary build targets **official Flipper firmware 1.5.1 (API 88.2)**. A `.fap` is tied to its
target API; if the Flipper reports `ApiTooNew`/`ApiTooOld`, match your firmware and rebuild.

- **Official 1.5.1** → build with `ufbt` and use `dist/civswitch_flipper.fap`.
- **RogueMaster** → uFBT builds are **not** reliably compatible (an in‑tree `ViewDispatcher`
  app crashed/received no input under RogueMaster even when APPCHK passed). The verified‑working
  RogueMaster build is `dist/civswitch_flipper_rm1024_api88.fap` (RM1024, API 88.23), produced by
  building the app **inside the RogueMaster firmware source tree** as an
  `applications_user/civswitch/` app with `fbt`.

---

## Build

From the repository root:

```bash
pip install ufbt          # once
ufbt                      # builds dist/civswitch_flipper.fap
python3 -m unittest discover -s tests -v   # host tests (expect 6/6)
```

`scripts/build_flipper.sh` and `scripts/test_host.sh` wrap the same two commands.

---

## Make a payload

A prebuilt **`dist/civswitch.fmacro`** (the official upstream payload) is included, so you can use
the app without building anything. The payload itself is **not authored here** — it is the upstream
CivSwitch author's work. To regenerate it from source you need the upstream `PicoSwitch/` tree
alongside this repo; this repo's `convert_payload.py` turns an already‑built `payload.txt` into the
`.fmacro` stream the Flipper app plays:

```bash
# 1. Produce payload.txt from core.js (upstream CivSwitch tooling)
cd PicoSwitch
npm install
node compress.js
cd ..

# 2. Convert payload.txt → checksummed .fmacro
python3 tools/convert_payload.py --input PicoSwitch/payload.txt --output dist/civswitch.fmacro
```

The converter is deterministic: the shipped `civswitch.fmacro` is bit‑for‑bit reproducible
(23,775 operations, 992,190 ms, SHA‑256 `277be1c9…c84bdd26`).

---

## Install on the Flipper

1. Copy the `.fap` for your firmware to `/ext/apps/USB/` on the Flipper's microSD card.
2. Copy `dist/civswitch.fmacro` to `/ext/apps_data/civswitch/`. The app creates that folder on
   first launch; you may place several `.fmacro` files there and choose one in **Load Payload**.

The payload is **not** embedded in the `.fap`.

---

## Usage

Launch **CivSwitch** from the Flipper's `Apps → USB` menu.

- **Load Payload** — pick a `.fmacro`; it is validated before it can run.
- **Start Automation** — review the confirmation screen, press OK to begin (USB must be
  configured by the host first).
- **USB Connection Test** — shows whether a host has configured the composite device.
- **Gamepad Test** — sends one A press for 50 ms, then release.
- **Keyboard Test** — sends one key (`a` or `1`) for 32 ms, then release.
- **Settings** — Up/Down changes the harmless test key only (never macro timing).
- **About** — version, purpose, validation reminder.
- **During playback** — OK pauses at the next operation boundary / resumes; Back opens an abort
  confirmation. A USB disconnect stops playback and requires an explicit OK after reconnection.
  **The app never resumes automatically.**

The run screen shows USB status, macro state, current line, percentage, elapsed time, estimated
remaining duration, and any error. On completion, press OK to reset before another deliberate start.

For the end‑to‑end exploit flow on the Switch (90DNS, language settings, the menu navigation into
multiplayer chat, and the persistent installer), follow the **upstream
[CivSwitch README](https://github.com/Gezine/CivSwitch)** — this app only replaces the Pico that
types the payload.

---

## Testing & verification

- **Host tests:** `python3 -m unittest discover -s tests -v` — 6 tests covering ASCII→HID
  mapping, full‑payload preservation, operation equivalence with the Pico generator, HID
  descriptor shape, malformed‑input rejection, native streaming through pause/disconnect/resume,
  and truncated/CRC‑corrupted macro rejection.
- **Linux USB verification (no exploit):** see [`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md)
  for `lsusb -v -d 0f0d:0092` checks and `tools/hid_host_inspect.py`.
- **Current status:** composite enumeration and live report descriptors passed on device; the app
  launches, navigates, and passed a PC gamepad/keyboard dry‑run on a RogueMaster build. **On‑device
  payload load + full automation and the Nintendo Switch acceptance test are still pending.**

---

## Troubleshooting

See [`docs/TROUBLESHOOTING.md`](docs/TROUBLESHOOTING.md) for a frozen‑Flipper recovery
(hold **LEFT + BACK** for 5 s; hard reboot with **BACK** for 30 s), API‑mismatch and macro‑file
errors, Linux host verification, and the Nintendo Switch validation procedure.

---

## Credits & attribution

This project stands on the work of others. The upstream authors and reference sources used to
build and verify this Flipper port (see [`docs/USB_ANALYSIS.md`](docs/USB_ANALYSIS.md) for the
descriptor and pipeline details) are:

- **[Gezine](https://github.com/Gezine)** — creator of **[CivSwitch](https://github.com/Gezine/CivSwitch)**
  (pinned revision `2465a68`): the entire exploit toolchain, the PicoSwitch firmware, the payload
  and its compression/transport pipeline (`compress.js`, `payload_to_macro.py`), and the chat‑XSS
  delivery method this app reproduces. **CivSwitch is MIT‑licensed, © 2026 Gezine.** This Flipper
  port exists only to carry that work onto a Flipper Zero.
- **[Jason Skuby](https://github.com/mytimidstudio)** — original MIT‑licensed Nintendo Switch USB
  HID descriptors (`switch_usb_*` / `keyboard_usb_report` arrays), copied into the Flipper source
  via `tools/generate_usb_descriptors.py` with only the EP0 packet size changed (64 → 8) for the
  Flipper USB stack.
- **[Element2424](https://github.com/Element2424)** — author of
  **[Pico‑Switch‑Controller](https://github.com/Element2424/Pico-Switch-Controller)**, which the
  upstream PicoSwitch firmware is based on.
- **[DavidPagels](https://github.com/DavidPagels)** — author of
  **[retro‑pico‑switch](https://github.com/DavidPagels/retro-pico-switch)**, the Pico Nintendo
  Switch **gamepad/controller input** implementation that Pico‑Switch‑Controller started from, and
  the root of the gamepad USB input in this lineage.
- **[Netflix‑N‑Hack](https://github.com/NetflixNHack/Netflix-N-Hack)** (NetflixNHack) and the
  **[ufm42 fork](https://github.com/ufm42/Netflix-N-Hack)** — the `inject.js` JS exploit core that
  upstream `core.js` references.
- **[butterfossil / flipper-tomodachi-drawer](https://github.com/butterfossil/flipper-tomodachi-drawer)**
  (revision `23f762e8`) — read‑only reference for the custom Furi USB mode. The repository had no
  license file at that revision, so **no source was copied** from it.
- **[Flipper Devices](https://github.com/flipperdevices)** — the
  [official firmware](https://github.com/flipperdevices/flipperzero-firmware) (1.5.1, API 88.2)
  build target and the [uFBT](https://github.com/flipperdevices/flipperzero-ufbt) build tool.
- **[RogueMaster](https://github.com/RogueMaster/flipperzero-firmware-wPlugins)** — used only as a
  **test target**: the on‑device verification build was compiled against its API (RM1024, API 88.23)
  and run on a device flashed with it. The app does not ship or require RogueMaster — the primary
  build targets official firmware.
- **[90DNS](https://gitlab.com/a/90dns)** — DNS block used in the upstream exploit workflow.

If you build on this work, please preserve these attributions.

---

## License

MIT — © 2026 Gezine (original CivSwitch) and © 2026 rvelez (Flipper Zero port). See
[`LICENSE`](LICENSE). The bundled Nintendo Switch HID descriptors are
MIT‑licensed, © Jason Skuby (see `tools/generate_usb_descriptors.py`). Third‑party references are
owned by their respective authors under their own licenses.
