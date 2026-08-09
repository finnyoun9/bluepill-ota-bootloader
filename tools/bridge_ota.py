#!/usr/bin/env python3
"""Upload an STM32 application through the Windows Bluetooth SPP bridge.

The command talks to the ESP32's outgoing Bluetooth virtual COM port (normally
COM6 on this bench), stages an application binary in ESP32 SPIFFS, and asks
the bridge to perform the UART OTA transfer to the STM32 bootloader.

The bridge protocol is deliberately text-safe and acknowledged:
    FW <version>,<size>,<crc32>\r\n
    DATA <offset>,<base64>\r\n
    VERIFY\r\n
Base64 plus an offset ACK for every block makes staging deterministic and
retryable before ``SEND`` starts the STM32 transfer.
"""

from __future__ import annotations

import argparse
import base64
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


def wait_for_text(ser: serial.Serial, expected: str, timeout_s: float,
                  echo: bool = True) -> bool:
    """Print bridge output and return once ``expected`` appears."""
    deadline = time.monotonic() + timeout_s
    received = ""

    while time.monotonic() < deadline:
        data = ser.read(256)
        if not data:
            continue

        text = data.decode("utf-8", errors="replace")
        if echo:
            sys.stdout.write(text)
            sys.stdout.flush()
        received = (received + text)[-4096:]

        if expected in received:
            return True
        if ("STATUS: Transfer failed" in received or
                "FW: CRC mismatch" in received or
                "DATA: NAK" in received):
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
    parser.add_argument("--chunk-size", type=int, default=128,
                        help="decoded bytes per Base64 DATA command (default: 128, max: 128)")
    parser.add_argument("--pace-ms", type=float, default=0.0,
                        help="optional delay after each acknowledged DATA block")
    parser.add_argument("--retries", type=int, default=3,
                        help="maximum attempts per DATA block (default: 3)")
    parser.add_argument("--timeout", type=float, default=90.0,
                        help="Maximum time for STM32 transfer and verification in seconds")
    args = parser.parse_args()

    if args.version < 1 or args.version > 0xFFFFFFFF:
        parser.error("--version must be in 1..4294967295")
    if args.chunk_size < 1 or args.chunk_size > 128:
        parser.error("--chunk-size must be in 1..128")
    if args.pace_ms < 0:
        parser.error("--pace-ms must be non-negative")
    if args.retries < 1:
        parser.error("--retries must be at least 1")
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

        print("Sending Base64 firmware blocks to ESP32 SPIFFS...")
        delay_s = args.pace_ms / 1000.0
        for offset in range(0, len(firmware), args.chunk_size):
            chunk = firmware[offset:offset + args.chunk_size]
            encoded = base64.b64encode(chunk)
            command = f"DATA {offset},".encode("ascii") + encoded + b"\r\n"
            expected = f"DATA: ACK {offset + len(chunk)}"

            acknowledged = False
            for attempt in range(1, args.retries + 1):
                if ser.write(command) != len(command):
                    continue
                ser.flush()
                if wait_for_text(ser, expected, timeout_s=5, echo=False):
                    acknowledged = True
                    break
                print(f"\nRetrying DATA offset {offset} ({attempt}/{args.retries})...")

            if not acknowledged:
                print(f"\nERROR: ESP32 did not acknowledge DATA offset {offset}.")
                return 1
            completed = offset + len(chunk)
            if completed % 1024 == 0 or completed == len(firmware):
                print(f"  Staged {completed} / {len(firmware)} bytes")
            if delay_s:
                time.sleep(delay_s)

        ser.write(b"VERIFY\r\n")
        ser.flush()
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
