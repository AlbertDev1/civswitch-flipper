#!/usr/bin/env python3
"""Convert CivSwitch payload.txt to a bounded, checksummed Flipper macro stream."""
import argparse
import importlib.util
import os
from pathlib import Path
import struct
import sys
import tempfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("pico_macro", ROOT / "PicoSwitch/tools/payload_to_macro.py")
PICO = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PICO)

HEADER = struct.Struct("<4sHHIIII")  # magic, version, record bytes, records, lines, duration, crc32
RECORD = struct.Struct("<BBBBI")       # operation, modifier, HID key, reserved, duration ms
PAD_PRESS, PAD_RELEASE, KEY_PRESS, KEY_RELEASE, WAIT, LINE, END, TYPE_ASCII = range(1, 9)
MAX_LINE = 4096
KEY_NUM = {f"HID_KEY_{chr(c).upper()}": 4 + c - 97 for c in range(97, 123)}
KEY_NUM.update({f"HID_KEY_{n}": 0x1E + i for i, n in enumerate("1234567890")})
KEY_NUM.update(dict(zip(
    ["HID_KEY_ENTER", "HID_KEY_ESCAPE", "HID_KEY_SPACE", "HID_KEY_TAB", "HID_KEY_MINUS",
     "HID_KEY_EQUAL", "HID_KEY_BRACKET_LEFT", "HID_KEY_BRACKET_RIGHT", "HID_KEY_BACKSLASH",
     "HID_KEY_SEMICOLON", "HID_KEY_APOSTROPHE", "HID_KEY_GRAVE", "HID_KEY_COMMA",
     "HID_KEY_PERIOD", "HID_KEY_SLASH"],
    [0x28, 0x29, 0x2C, 0x2B, 0x2D, 0x2E, 0x2F, 0x30, 0x31, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38])))


def records(lines, line_count):
    for _ in range(PICO.PREAMBLE_A_COUNT):
        yield PAD_PRESS, 0, 0, PICO.A_PRESS_MS
        yield PAD_RELEASE, 0, 0, PICO.PREAMBLE_GAP_MS
    for index, line in enumerate(lines):
        yield LINE, 0, 0, index + 1
        yield KEY_PRESS, 0, KEY_NUM["HID_KEY_ESCAPE"], PICO.KEY_HOLD_MS
        yield KEY_RELEASE, 0, 0, PICO.ESC_TO_A_WAIT_MS
        yield PAD_PRESS, 0, 0, PICO.A_PRESS_MS
        yield PAD_RELEASE, 0, 0, PICO.POST_A_WAIT_MS
        previous = None
        for char in line:
            shift, name = PICO.ASCII_TO_KEY[char]
            key = KEY_NUM[name]
            if key == previous:
                yield KEY_RELEASE, 0, 0, PICO.KEY_GAP_MS
            yield TYPE_ASCII, 0, ord(char), PICO.KEY_HOLD_MS
            yield WAIT, 0, 0, PICO.KEY_GAP_MS
            previous = key
        yield KEY_PRESS, 0, KEY_NUM["HID_KEY_ENTER"], PICO.ENTER_HOLD_MS
        yield KEY_RELEASE, 0, 0, PICO.ENTER_GAP_MS
        yield KEY_PRESS, 0, KEY_NUM["HID_KEY_ENTER"], PICO.ENTER_HOLD_MS
        if index < line_count - 1:
            yield KEY_RELEASE, 0, 0, PICO.PRE_A_WAIT_MS
            yield PAD_PRESS, 0, 0, PICO.A_PRESS_MS
            yield PAD_RELEASE, 0, 0, PICO.POST_A_WAIT_MS
    yield KEY_RELEASE, 0, 0, 0
    yield END, 0, 0, 0


def validated_lines(path):
    with open(path, "rb") as src:
        for number, raw in enumerate(src, 1):
            if len(raw) > MAX_LINE + 2:
                raise ValueError(f"line {number}: exceeds {MAX_LINE} bytes")
            if raw.endswith(b"\n"):
                raw = raw[:-1]
            if raw.endswith(b"\r"):
                raise ValueError(f"line {number}: CRLF is not supported")
            if not raw:
                raise ValueError(f"line {number}: empty line")
            if not raw.strip():
                raise ValueError(f"line {number}: whitespace-only line is skipped by the Pico generator")
            if any(c not in PICO.ASCII_TO_KEY for c in raw.decode("ascii", errors="replace")):
                raise ValueError(f"line {number}: unsupported character")
            yield raw.decode("ascii")


def convert(source, target):
    # A first streaming pass validates and counts lines; the second emits operations.
    line_count = sum(1 for _ in validated_lines(source))
    if not line_count:
        raise ValueError("payload is empty")
    target = Path(target)
    target.parent.mkdir(parents=True, exist_ok=True)
    fd, temp = tempfile.mkstemp(prefix=target.name + ".", dir=target.parent)
    try:
        with os.fdopen(fd, "wb") as dst:
            dst.write(bytes(HEADER.size))
            crc = count = duration = 0
            for op, mod, key, ms in records(validated_lines(source), line_count):
                if ms > 0xFFFFFFFF or duration + ms > 0xFFFFFFFF:
                    raise ValueError("macro duration exceeds format limit")
                item = RECORD.pack(op, mod, key, 0, ms)
                dst.write(item)
                crc = zlib.crc32(item, crc)
                duration += ms if op != LINE else 0
                count += 1
            dst.seek(0)
            dst.write(HEADER.pack(b"FMAC", 1, RECORD.size, count, line_count, duration, crc))
            dst.flush()
            os.fsync(dst.fileno())
        os.replace(temp, target)
    finally:
        if os.path.exists(temp):
            os.unlink(temp)
    return line_count, count, duration


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        lines, count, ms = convert(args.input, args.output)
    except (OSError, ValueError) as exc:
        parser.exit(1, f"convert_payload: {exc}\n")
    print(f"{lines} lines, {count} operations, {ms} ms -> {args.output}")


if __name__ == "__main__":
    main()
