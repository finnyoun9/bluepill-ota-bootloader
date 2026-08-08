#!/usr/bin/env python3
"""Upload an STM32 application through the Windows Bluetooth SPP bridge.

The command talks to the ESP32's outgoing Bluetooth virtual COM port (normally
COM6 on this bench), stages an application binary in ESP32 SPIFFS, and asks
the bridge to perform the UART OTA transfer to the STM32 bootloader.

The bridge protocol is deliberately length-prefixed:
    FW <version>,<size>,<crc32>\r\n
The script waits for the bridge's ``FW: staged`` reply before sending ``SEND``.
That prevents arbitrary firmware bytes from ever being interpreted as text.
"""

from __future__ import annotations

import argparse
import pathlib
import sys
import time
import zlib

try:
    import serial
except ImportError:
    print("ERROR: pyserial is required. Install it with: python -m pip install pyserial")
    raise SystemExit(2)


MAX_APP_SIZE = 54 * 1024


def wait_for_text(ser: serial.Serial, expected: str, timeout_s: float) -> bool:
    """Print bridge output and return once ``expected`` appears."""
    deadline = time.monotonic() + timeout_s
    received = ""

    while time.monotonic() < deadline:
        data = ser.read(256)
        if not data:
            continue

        text = data.decode("utf-8", errors="replace")
        sys.stdout.write(text)
        sys.stdout.flush()
        received = (received + text)[-4096:]

        if expected in received:
            return True
        if "STATUS: Transfer failed" in received or "FW: binary data rejected" in received:
            return False

    return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="Bluetooth SPP outgoing COM port, e.g. COM6")
    parser.add_argument("firmware", type=pathlib.Path,
                        help="STM32 application .bin, e.g. .pio/build/app/firmware.bin")
    parser.add_argument("--version", type=int, default=1,
                        help="Firmware version written to STM32 config (default: 1)")
    parser.add_argument("--baud", type=int, default=115200,
                        help="Windows Bluetooth virtual-port speed (default: 115200)")
    parser.add_argument("--chunk-size", type=int, default=256,
                        help="PC-to-ESP32 staging write size in bytes (default: 256)")
    parser.add_argument("--pace-ms", type=float, default=8.0,
                        help="Delay between staging writes in ms (default: 8)")
    parser.add_argument("--timeout", type=float, default=90.0,
                        help="Maximum time for STM32 transfer and verification in seconds")
    args = parser.parse_args()

    if args.version < 1 or args.version > 0xFFFFFFFF:
        parser.error("--version must be in 1..4294967295")
    if args.chunk_size < 1 or args.chunk_size > 1024:
        parser.error("--chunk-size must be in 1..1024")
    if args.pace_ms < 0:
        parser.error("--pace-ms must be non-negative")
    if not args.firmware.is_file():
        parser.error(f"firmware file not found: {args.firmware}")

    firmware = args.firmware.read_bytes()
    if not firmware:
        parser.error("firmware file is empty")
    if len(firmware) > MAX_APP_SIZE:
        parser.error(f"firmware is {len(firmware)} bytes; STM32 app limit is {MAX_APP_SIZE}")

    crc = zlib.crc32(firmware) & 0xFFFFFFFF
    print(f"Firmware: {args.firmware}")
    print(f"  Size: {len(firmware)} bytes")
    print(f"  CRC32: 0x{crc:08X}")
    print(f"  Version: {args.version}")

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.2, write_timeout=10)
    except serial.SerialException as exc:
        print(f"ERROR: cannot open {args.port}: {exc}")
        return 2

    try:
        ser.reset_input_buffer()
        command = f"FW {args.version},{len(firmware)},{crc:08X}\r\n".encode("ascii")
        ser.write(command)
        ser.flush()

        print("Waiting for ESP32 staging acknowledgement...")
        if not wait_for_text(ser, "FW: ready", timeout_s=5):
            print("\nERROR: ESP32 did not accept the FW command.")
            return 1

        print("Sending firmware to ESP32 SPIFFS...")
        delay_s = args.pace_ms / 1000.0
        for offset in range(0, len(firmware), args.chunk_size):
            ser.write(firmware[offset:offset + args.chunk_size])
            ser.flush()
            if delay_s:
                time.sleep(delay_s)
        print("Waiting for staging CRC preflight...")
        if not wait_for_text(ser, "FW: staged", timeout_s=15):
            print("\nERROR: ESP32 did not finish staging the firmware.")
            return 1

        ser.write(b"SEND\r\n")
        ser.flush()
        print("Waiting for STM32 OTA result...")
        if not wait_for_text(ser, "STATUS: OTA complete!", timeout_s=args.timeout):
            print("\nERROR: OTA did not complete successfully. Keep ESP32 COM4 open for its log.")
            return 1

        print("\nSUCCESS: Bluetooth-to-STM32 OTA completed.")
        return 0
    except serial.SerialException as exc:
        print(f"ERROR: serial I/O failed: {exc}")
        return 1
    finally:
        ser.close()


if __name__ == "__main__":
    raise SystemExit(main())
