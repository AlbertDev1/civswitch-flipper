#!/usr/bin/env python3
"""Read a chosen Linux hidraw node and print reports; never sends input."""
import argparse
import os
import select
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("hidraw", help="/dev/hidrawN for the Flipper interface")
    parser.add_argument("--seconds", type=float, default=30)
    args = parser.parse_args()
    fd = os.open(args.hidraw, os.O_RDONLY | os.O_NONBLOCK)
    try:
        end = time.monotonic() + args.seconds
        while time.monotonic() < end:
            ready, _, _ = select.select([fd], [], [], max(0, end - time.monotonic()))
            if ready:
                try: print(f"{time.monotonic():.3f} {os.read(fd, 64).hex(' ')}", flush=True)
                except BlockingIOError: pass
    finally:
        os.close(fd)


if __name__ == "__main__": main()
