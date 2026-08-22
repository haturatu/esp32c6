#!/usr/bin/env python3
"""Store Wi-Fi credentials in ESP32 Preferences/NVS over USB serial."""

import argparse
import binascii
import sys
import time

import serial


def hex_value(value: str) -> str:
    return binascii.hexlify(value.encode("utf-8")).decode("ascii")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--ssid", required=True)
    parser.add_argument("--password", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()
    if not args.ssid or not args.password:
        parser.error("--ssid and --password must not be empty")

    command = f"PROVISION1 {hex_value(args.ssid)} {hex_value(args.password)}\n".encode()
    with serial.Serial(args.port, args.baud, timeout=0.2) as device:
        time.sleep(1.0)
        device.reset_input_buffer()
        device.write(command)
        device.flush()
        deadline = time.monotonic() + 10.0
        while time.monotonic() < deadline:
            line = device.readline().decode("utf-8", errors="replace").strip()
            if line:
                print(line)
            if "PROVISION_OK" in line:
                return 0
    print("provisioning timed out", file=sys.stderr)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
