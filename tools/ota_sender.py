#!/usr/bin/env python3
"""
OTA Firmware Sender — PC-side tool for testing STM32 bootloader.

Connects to the STM32 Blue Pill via USB-TTL serial adapter (USART1 PA9/PA10)
and sends a firmware .bin file using the bootloader protocol.

Usage:
    # Send firmware to STM32 (STM32 must be in bootloader/OTA mode)
    python ota_sender.py /dev/tty.usbserial-XXXX fw_v2.bin --version 2

    # Query STM32 status
    python ota_sender.py /dev/tty.usbserial-XXXX --status

    # Send OTA_AVAILABLE to running application (triggers app → reboot → bootloader)
    python ota_sender.py /dev/tty.usbserial-XXXX --notify-ota --version 2

Requirements:
    pip install pyserial
"""

import argparse
import struct
import sys
import time
import os

try:
    import serial
except ImportError:
    print("ERROR: pyserial required. Run: pip install pyserial")
    sys.exit(1)

# ---------------------------------------------------------------------------
# Protocol constants (must match shared/protocol.h)
# ---------------------------------------------------------------------------

PROTO_SYNC      = 0xA5
PROTO_MAX_PAYLOAD = 1028  # OTA chunk: sequence (4) + page data (1024)
PROTO_MAX_FRAME = 1036
FLASH_PAGE_SIZE = 1024

# Commands — master to slave
CMD_OTA_BEGIN      = 0x10
CMD_OTA_CHUNK      = 0x11
CMD_OTA_END        = 0x12
CMD_OTA_ABORT      = 0x13
CMD_OTA_AVAILABLE  = 0x14
CMD_APP_MSG        = 0x20
CMD_GET_STATUS     = 0x30
CMD_RESET          = 0x31

# Commands — slave to master
CMD_OTA_BEGIN_ACK  = 0x81
CMD_CHUNK_ACK      = 0x82
CMD_NAK            = 0x83
CMD_OTA_RESULT     = 0x84
CMD_STATUS_RSP     = 0x85
CMD_OTA_READY      = 0x86

# Error codes
ERRORS = {
    0x00: "NONE",
    0x01: "FRAME_CRC",
    0x02: "SEQ_MISMATCH",
    0x03: "FLASH_ERASE",
    0x04: "FLASH_PROGRAM",
    0x05: "FLASH_VERIFY",
    0x06: "IMAGE_CRC",
    0x07: "SIZE_TOO_LARGE",
    0x08: "UNKNOWN_CMD",
    0x09: "TIMEOUT",
    0x0A: "BUSY",
}

OTA_RESULT_OK   = 0x00000000
OTA_RESULT_FAIL = 0x00000001

# ---------------------------------------------------------------------------
# CRC-32 (must match shared/protocol.c)
# ---------------------------------------------------------------------------

CRC32_TABLE = [
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
    0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988,
    0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE,
    0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC,
    0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172,
    0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940,
    0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116,
    0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924,
    0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A,
    0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818,
    0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E,
    0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C,
    0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
    0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2,
    0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
    0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0,
    0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
    0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086,
    0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
    0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4,
    0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
    0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A,
    0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
    0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8,
    0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
    0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE,
    0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
    0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC,
    0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
    0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FD8B252,
    0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
    0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60,
    0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
    0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236,
    0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
    0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB30A04,
    0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
    0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A,
    0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
    0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38,
    0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
    0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E,
    0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
    0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C,
    0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
    0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2,
    0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
    0xAED16A4A, 0xD9D65ADC, 0x40BF0B66, 0x37B83BF0,
    0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
    0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6,
    0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
    0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94,
    0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D,
]

def crc32(data: bytes, crc: int = 0) -> int:
    """Standard IEEE CRC-32; pass a previous return value to continue."""
    crc ^= 0xFFFFFFFF
    for b in data:
        crc = CRC32_TABLE[(crc ^ b) & 0xFF] ^ (crc >> 8)
    return crc ^ 0xFFFFFFFF

# ---------------------------------------------------------------------------
# Frame building and parsing
# ---------------------------------------------------------------------------

def build_frame(cmd: int, payload: bytes) -> bytes:
    """Build a protocol frame."""
    length = len(payload)
    if length > PROTO_MAX_PAYLOAD:
        raise ValueError(f"Payload too large: {length}")

    # Header: SYNC + CMD + LEN(2 LE)
    header = struct.pack('<BBH', PROTO_SYNC, cmd, length)

    # CRC-32 over header + payload
    crc = crc32(header + payload)
    crc_bytes = struct.pack('<I', crc)

    return header + payload + crc_bytes

def parse_frame(ser: serial.Serial, timeout_ms: float = 5000) -> tuple[int, bytes] | None:
    """Read and parse a single frame from the serial port. Returns (cmd, payload) or None on timeout."""
    deadline = time.time() + timeout_ms / 1000.0

    # Wait for SYNC
    while time.time() < deadline:
        b = ser.read(1)
        if not b:
            continue
        if b[0] == PROTO_SYNC:
            break
    else:
        return None  # Timeout

    # Read CMD + LEN(2)
    remaining = ser.read(3)
    if len(remaining) < 3:
        return None

    cmd = remaining[0]
    length = remaining[1] | (remaining[2] << 8)

    if length > PROTO_MAX_PAYLOAD:
        print(f"  WARN: Invalid frame length {length}, discarding")
        return None

    # Read payload + CRC
    to_read = length + 4
    data = bytearray()
    while len(data) < to_read and time.time() < deadline:
        chunk = ser.read(to_read - len(data))
        if chunk:
            data.extend(chunk)

    if len(data) < to_read:
        return None

    payload = bytes(data[:length])
    rx_crc = struct.unpack('<I', data[length:length+4])[0]

    # Verify CRC
    frame_without_crc = struct.pack('<BBH', PROTO_SYNC, cmd, length) + payload
    calc_crc = crc32(frame_without_crc)

    if rx_crc != calc_crc:
        print(f"  WARN: CRC mismatch: rx=0x{rx_crc:08X} calc=0x{calc_crc:08X}")
        return None

    return (cmd, payload)

# ---------------------------------------------------------------------------
# OTA sender
# ---------------------------------------------------------------------------

def send_ota(ser: serial.Serial, fw_path: str, version: int) -> bool:
    """Send firmware file to STM32 bootloader."""
    with open(fw_path, 'rb') as f:
        fw_data = f.read()

    fw_size = len(fw_data)
    fw_crc = crc32(fw_data)

    print(f"Firmware: {fw_path}")
    print(f"  Size: {fw_size} bytes ({fw_size / 1024:.1f} KB)")
    print(f"  CRC32: 0x{fw_crc:08X}")
    print(f"  Version: {version}")
    print()

    max_app_size = 54 * 1024  # 54KB
    if fw_size > max_app_size:
        print(f"ERROR: Firmware too large ({fw_size} > {max_app_size} bytes)")
        return False

    # Step 1: Send OTA_BEGIN
    print("[1/3] Sending OTA_BEGIN...")
    begin_payload = struct.pack('<III', fw_size, version, fw_crc)
    ser.write(build_frame(CMD_OTA_BEGIN, begin_payload))

    resp = parse_frame(ser, timeout_ms=5000)
    if resp is None:
        print("  ERROR: No response to OTA_BEGIN")
        return False

    cmd, payload = resp
    if cmd == CMD_NAK:
        err = struct.unpack('<I', payload[4:8])[0] if len(payload) >= 8 else 0
        print(f"  NAK: {ERRORS.get(err, f'0x{err:X}')}")
        return False
    elif cmd != CMD_OTA_BEGIN_ACK:
        print(f"  ERROR: Unexpected response: cmd=0x{cmd:02X}")
        return False

    expected_seq = struct.unpack('<I', payload[:4])[0] if len(payload) >= 4 else 0
    print(f"  ACK: expected_seq={expected_seq}")

    # Step 2: Send chunks
    print(f"[2/3] Sending {((fw_size + FLASH_PAGE_SIZE - 1) // FLASH_PAGE_SIZE)} chunks...")

    seq = 0
    offset = 0
    max_retries = 3
    total_pages = (fw_size + FLASH_PAGE_SIZE - 1) // FLASH_PAGE_SIZE

    while offset < fw_size:
        chunk_len = min(FLASH_PAGE_SIZE, fw_size - offset)
        chunk_data = fw_data[offset:offset + chunk_len]

        # Pad to page size with 0xFF
        if chunk_len < FLASH_PAGE_SIZE:
            chunk_data = chunk_data + b'\xFF' * (FLASH_PAGE_SIZE - chunk_len)

        acked = False
        for retry in range(max_retries):
            # Build chunk: seq(4) + data
            chunk_payload = struct.pack('<I', seq) + chunk_data[:chunk_len]
            ser.write(build_frame(CMD_OTA_CHUNK, chunk_payload))

            resp = parse_frame(ser, timeout_ms=3000)
            if resp is None:
                print(f"  Chunk {seq}: timeout, retry {retry + 1}/{max_retries}")
                continue

            cmd, payload = resp
            if cmd == CMD_CHUNK_ACK:
                ack_seq = struct.unpack('<I', payload[:4])[0] if len(payload) >= 4 else 0xFFFFFFFF
                if ack_seq == seq:
                    acked = True
                    break
                else:
                    print(f"  Chunk {seq}: wrong ACK seq {ack_seq}")
            elif cmd == CMD_NAK:
                err = 0
                exp_seq = 0
                if len(payload) >= 4:
                    exp_seq = struct.unpack('<I', payload[:4])[0]
                if len(payload) >= 8:
                    err = struct.unpack('<I', payload[4:8])[0]
                print(f"  Chunk {seq}: NAK (err={ERRORS.get(err, f'0x{err:X}')}, expected_seq={exp_seq}), retry {retry + 1}/{max_retries}")

        if not acked:
            print(f"  ERROR: Chunk {seq} failed after {max_retries} retries")
            ser.write(build_frame(CMD_OTA_ABORT, b''))
            return False

        seq += 1
        offset += chunk_len

        progress = min(100, offset * 100 // fw_size)
        bar = '#' * (progress // 5) + '-' * (20 - progress // 5)
        print(f"  [{bar}] {progress:3d}%  ({offset}/{fw_size} bytes)", end='\r')

    print()  # newline after progress bar
    print(f"  All {seq} chunks sent successfully.")

    # Step 3: Send OTA_END
    print("[3/3] Verifying...")
    ser.write(build_frame(CMD_OTA_END, b''))

    resp = parse_frame(ser, timeout_ms=15000)
    if resp is None:
        print("  ERROR: No response to OTA_END")
        return False

    cmd, payload = resp
    if cmd != CMD_OTA_RESULT:
        print(f"  ERROR: Unexpected response: cmd=0x{cmd:02X}")
        return False

    result = struct.unpack('<I', payload[:4])[0] if len(payload) >= 4 else 0xFFFFFFFF
    new_version = struct.unpack('<I', payload[4:8])[0] if len(payload) >= 8 else 0

    if result == OTA_RESULT_OK:
        print(f"  SUCCESS! New firmware v{new_version} installed.")
        return True
    else:
        print(f"  FAILED: result={result}, version={new_version}")
        return False


def query_status(ser: serial.Serial) -> None:
    """Send GET_STATUS and print response."""
    ser.write(build_frame(CMD_GET_STATUS, b''))
    resp = parse_frame(ser, timeout_ms=3000)

    if resp is None:
        print("No response from STM32.")
        print("Is the bootloader running? Try pressing reset while BOOT0 is pulled low.")
        return

    cmd, payload = resp
    if cmd == CMD_STATUS_RSP:
        if len(payload) >= 4:
            status = struct.unpack('<I', payload[:4])[0]
            print(f"STM32 Status: 0x{status:08X}")
    else:
        print(f"Unexpected response: cmd=0x{cmd:02X}")


def notify_ota(ser: serial.Serial, version: int) -> bool:
    """Ask the running application to persist the OTA request and reset."""
    payload = struct.pack('<I', version)
    ser.write(build_frame(CMD_OTA_AVAILABLE, payload))
    resp = parse_frame(ser, timeout_ms=2000)
    if resp is None or resp[0] != CMD_OTA_READY:
        print("ERROR: Application did not confirm OTA readiness.")
        return False
    print(f"Application confirmed OTA readiness for version {version}.")
    print("The STM32 is rebooting into the bootloader now.")
    print("After reboot, run: python ota_sender.py <port> <firmware.bin> --version <version>")
    return True


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="OTA Firmware Sender for STM32 Blue Pill Bootloader",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python ota_sender.py /dev/tty.usbserial-XXXX fw_v2.bin --version 2
  python ota_sender.py COM3 fw_v2.bin --version 2 --baud 115200
  python ota_sender.py /dev/tty.usbserial-XXXX --status
  python ota_sender.py /dev/tty.usbserial-XXXX --notify-ota --version 2
        """
    )

    parser.add_argument('port', help='Serial port (e.g., /dev/tty.usbserial-XXXX or COM3)')
    parser.add_argument('firmware', nargs='?', help='Firmware .bin file to send')
    parser.add_argument('--version', type=int, default=1, help='Firmware version number')
    parser.add_argument('--baud', type=int, default=115200, help='Baud rate (default: 115200)')
    parser.add_argument('--status', action='store_true', help='Query STM32 status')
    parser.add_argument('--notify-ota', action='store_true',
                        help='Send OTA_AVAILABLE to running application')

    args = parser.parse_args()

    # Connect
    print(f"Connecting to {args.port} at {args.baud} baud...")
    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.1)
    except serial.SerialException as e:
        print(f"ERROR: Cannot open serial port: {e}")
        sys.exit(1)

    ser.reset_input_buffer()
    ser.reset_output_buffer()

    try:
        if args.status:
            query_status(ser)
        elif args.notify_ota:
            if not notify_ota(ser, args.version):
                sys.exit(1)
        elif args.firmware:
            if not os.path.exists(args.firmware):
                print(f"ERROR: Firmware file not found: {args.firmware}")
                sys.exit(1)
            success = send_ota(ser, args.firmware, args.version)
            sys.exit(0 if success else 1)
        else:
            parser.print_help()
    finally:
        ser.close()


if __name__ == '__main__':
    main()
