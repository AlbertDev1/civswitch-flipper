import importlib.util
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib
import ctypes

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import convert_payload as cv


def read_macro(path):
    data = path.read_bytes()
    magic, version, width, count, lines, duration, crc = cv.HEADER.unpack_from(data)
    assert magic == b"FMAC" and version == 1 and width == 8
    assert len(data) == cv.HEADER.size + count * width
    assert zlib.crc32(data[cv.HEADER.size:]) == crc
    recs = [cv.RECORD.unpack_from(data, cv.HEADER.size + i * width) for i in range(count)]
    assert sum(r[4] for r in recs if r[0] != cv.LINE) == duration
    return lines, recs


def parse_pico(path):
    result = []
    for raw in path.read_text().splitlines():
        m = re.match(r"\s*(PAD_BTN|KEYS|WAIT|END)\((.*?)\),", raw)
        if not m:
            continue
        op, args = m.groups()
        parts = [p.strip() for p in args.split(",")]
        if op == "PAD_BTN": result.append(("pad", 1, 0, int(parts[0])))
        elif op == "WAIT": result.append(("neutral", 0, 0, int(parts[0])))
        elif op == "KEYS":
            result.append(("key", cv.KEY_NUM[parts[2]], 2 if parts[1] != "0" else 0, int(parts[0])))
        else: result.append(("end", 0, 0, 0))
    return result


def normalized(records):
    result = []
    for op, mod, key, _, ms in records:
        if op == cv.LINE: continue
        if op == cv.PAD_PRESS: result.append(("pad", 1, 0, ms))
        elif op == cv.KEY_PRESS: result.append(("key", key, mod, ms))
        elif op == cv.TYPE_ASCII:
            shift, name = cv.PICO.ASCII_TO_KEY[chr(key)]
            result.append(("key", cv.KEY_NUM[name], 2 if shift else 0, ms))
        elif op == cv.END: result.append(("end", 0, 0, 0))
        elif op == cv.KEY_RELEASE and ms == 0: continue
        else: result.append(("neutral", 0, 0, ms))
    return result


def descriptor(name):
    text = (ROOT / "flipper/usb_descriptors.h").read_text()
    match = re.search(r"static const uint8_t " + name + r"\[\] = \{(.*?)\n\};", text, re.S)
    assert match, name
    body = re.sub(r"//[^\n]*", "", match.group(1))
    return bytes(int(n, 16) for n in re.findall(r"0x[0-9A-Fa-f]+", body))


class ConverterTests(unittest.TestCase):
    def test_pico_sequence_and_timing(self):
        samples = ["aAaa!1", "Zz0/<>[]{};:'\"\\~", "\t.@#"]
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            source, target, header = (folder / n for n in ("payload.txt", "test.fmacro", "script.h"))
            source.write_text("\n".join(samples) + "\n", encoding="ascii")
            cv.convert(source, target)
            subprocess.run([sys.executable, str(ROOT / "PicoSwitch/tools/payload_to_macro.py"),
                            str(source), str(header)], check=True, capture_output=True)
            lines, records = read_macro(target)
            self.assertEqual(lines, len(samples))
            self.assertEqual(normalized(records), parse_pico(header))

    def test_rejects_bad_characters_and_empty_lines(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, target = Path(tmp) / "p", Path(tmp) / "m"
            for text in (b"abc\xc3\xa9\n", b"abc\x00\n", b"abc\r\n", b"a\n\n"):
                source.write_bytes(text)
                with self.assertRaises(ValueError): cv.convert(source, target)

    def test_default_payload_roundtrip(self):
        source = ROOT / "PicoSwitch/payload.txt"
        if not source.exists(): self.skipTest("Run npm install && node compress.js first")
        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "m"
            cv.convert(source, target)
            header = Path(tmp) / "pico.h"
            subprocess.run([sys.executable, str(ROOT / "PicoSwitch/tools/payload_to_macro.py"),
                            str(source), str(header)], check=True, capture_output=True)
            lines, records = read_macro(target)
            self.assertEqual(lines, 87)
            self.assertEqual(normalized(records), parse_pico(header))
            typed, line = [], []
            in_text = False
            for op, mod, key, _, ms in records:
                if op == cv.LINE:
                    if line: typed.append("".join(line))
                    line, in_text = [], False
                elif op == cv.PAD_RELEASE and not in_text:
                    in_text = True
                elif op == cv.TYPE_ASCII and in_text: line.append(chr(key))
                elif op == cv.KEY_PRESS and in_text and key == 0x28: in_text = False
            if line: typed.append("".join(line))
            self.assertEqual(typed, source.read_text(encoding="ascii").splitlines())
            self.assertLess(target.stat().st_size, 2_000_000)

    def test_c_ascii_map_matches_pico(self):
        with tempfile.TemporaryDirectory() as tmp:
            lib = Path(tmp) / "ascii.so"
            subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-shared", "-fPIC",
                            str(ROOT / "flipper/ascii_hid.c"), "-o", str(lib)], check=True)
            fn = ctypes.CDLL(str(lib)).civ_ascii_to_hid
            fn.argtypes = [ctypes.c_char, ctypes.POINTER(ctypes.c_uint8), ctypes.POINTER(ctypes.c_uint8)]
            fn.restype = ctypes.c_bool
            for n in range(128):
                modifier, key = ctypes.c_uint8(), ctypes.c_uint8()
                result = fn(bytes([n]), ctypes.byref(modifier), ctypes.byref(key))
                expected = cv.PICO.ASCII_TO_KEY.get(chr(n))
                self.assertEqual(result, expected is not None, n)
                if expected:
                    self.assertEqual((modifier.value, key.value),
                                     (2 if expected[0] else 0, cv.KEY_NUM[expected[1]]), n)

    def test_descriptor_shape(self):
        device = descriptor("switch_usb_device_descriptor")
        config = descriptor("switch_usb_configuration_descriptor")
        pad = descriptor("switch_usb_report_descriptor")
        keyboard = descriptor("keyboard_usb_report_descriptor")
        self.assertEqual(len(device), 18)
        self.assertEqual(device[7], 8)
        self.assertEqual(len(config), 73)
        self.assertEqual(config[2] | config[3] << 8, len(config))
        self.assertEqual(config[4], 2)
        self.assertEqual((len(pad), len(keyboard)), (86, 63))
        self.assertEqual({config[29], config[36], config[61], config[68]}, {0x02, 0x81, 0x83, 0x03})
        self.assertEqual(config[50], 9)  # interface 1 HID descriptor

    def test_native_engine_pause_disconnect_and_corruption(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            binary = folder / "engine"
            source, target = folder / "payload.txt", folder / "payload.fmacro"
            source.write_text("aAaa!1\nSecond line\n", encoding="ascii")
            cv.convert(source, target)
            subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I", str(ROOT / "tests/stubs"), "-I", str(ROOT / "flipper"),
                            str(ROOT / "tests/native_engine.c"), str(ROOT / "flipper/macro_engine.c"),
                            str(ROOT / "flipper/ascii_hid.c"),
                            "-o", str(binary)], check=True)
            subprocess.run([str(binary), "run", str(target)], check=True)
            subprocess.run([str(binary), "timing", str(target)], check=True)
            default_macro = ROOT / "dist/civswitch.fmacro"
            if default_macro.exists():
                subprocess.run([str(binary), "run", str(default_macro)], check=True)
                subprocess.run([str(binary), "timing", str(default_macro)], check=True)
            original = target.read_bytes()
            for bad in (original[:-1], original[:30] + bytes([original[30] ^ 1]) + original[31:]):
                target.write_bytes(bad)
                subprocess.run([str(binary), "reject", str(target)], check=True)


if __name__ == "__main__": unittest.main()
