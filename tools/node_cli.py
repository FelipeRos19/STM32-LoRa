#!/usr/bin/env python3
"""Bench tool that plays the role of the Android app (docs/protocolo_serial.md).

It is also a reference implementation of the serial protocol for the app team.

Examples:
  tools/node_cli.py -p /dev/ttyUSB0 monitor
  tools/node_cli.py -p /dev/ttyUSB0 config --role beacon --user-id 0x0A1B2C3D --sf 9
  tools/node_cli.py -p /dev/ttyUSB0 start --lat -27.81 --lon -50.32 --status 2 --msg "perdido"
  tools/node_cli.py -p /dev/ttyUSB0 update --lat -27.81 --lon -50.32 --status 4 --msg "fogo"
  tools/node_cli.py -p /dev/ttyUSB0 stop
  tools/node_cli.py -p /dev/ttyUSB0 config --role sniffer --follow
  tools/node_cli.py -p /dev/ttyUSB0 selftest
  tools/node_cli.py -p /dev/ttyUSB0 bootloader

Requires pyserial (pip install pyserial).
"""

import argparse
import struct
import sys
import time

SOF = 0x7E
MAX_PAYLOAD = 250
PROTO_VERSION = 1

CONFIG, ALERT_START, UPDATE, ALERT_STOP, GET_STATUS, ENTER_BOOTLOADER = 0x01, 0x02, 0x03, 0x04, 0x05, 0x0F
RX_ALERT, STATUS, LOG = 0x81, 0x82, 0x83

FRAME_NAMES = {
    CONFIG: "CONFIG", ALERT_START: "ALERT_START", UPDATE: "UPDATE", ALERT_STOP: "ALERT_STOP",
    GET_STATUS: "GET_STATUS", ENTER_BOOTLOADER: "ENTER_BOOTLOADER", RX_ALERT: "RX_ALERT",
    STATUS: "STATUS", LOG: "LOG", 0x00: "EVENT",
}
RESULTS = ["OK", "E_CRC", "E_LEN", "E_PARAM", "E_STATE", "E_TOA", "E_FLASH", "E_TYPE", "E_BUSY", "E_RADIO"]
STATES = ["WAIT_CONFIG", "BEACON_IDLE", "BEACON_ALERT", "SNIFFER_RX"]
ROLES = ["NONE", "BEACON", "SNIFFER"]
RESET_CAUSES = ["UNKNOWN", "POWER_ON", "PIN", "SOFTWARE", "IWDG", "WWDG", "LOW_POWER", "BROWNOUT"]
ERR_FLAGS = ["SERIAL_CRC", "SERIAL_OVERFLOW", "NVM_READ", "NVM_WRITE", "RADIO_TX_TIMEOUT",
             "RADIO_RESET", "CAD_BUSY", "SERIAL_TX_DROP", "LSE_FALLBACK"]
USER_STATUS = ["SEGURO", "PERIGO", "PERDIDO", "FERIDO", "INCENDIO", "ILHADO", "SEM_SUPRIMENTOS"]
BW_CODES = {125: 0, 250: 1, 500: 2}


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode_frame(ftype: int, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too long")
    body = bytes([ftype, len(payload)]) + payload
    return bytes([SOF]) + body + crc16(body).to_bytes(2, "big")


class FrameDecoder:
    """Accumulates bytes and returns complete (type, payload) frames.

    Same rules as the firmware: on a bad CRC or length only the SOF byte is
    dropped and the rest is rescanned; if no byte arrives for BYTE_TIMEOUT
    in the middle of a frame, the pending bytes are rescanned and any
    incomplete candidate is discarded (a stray 0x7E with a large length
    would otherwise hold back the real frames behind it)."""

    BYTE_TIMEOUT = 0.1

    def __init__(self):
        self.buf = bytearray()
        self.errors = 0
        self.last = 0.0

    def feed(self, data: bytes, now: float = None):
        now = time.monotonic() if now is None else now
        frames = self.poll(now)
        self.last = now
        self.buf += data
        return frames + self._drain(stale=False)

    def poll(self, now: float = None):
        """Call periodically while no data arrives."""
        now = time.monotonic() if now is None else now
        if self.buf and (now - self.last) > self.BYTE_TIMEOUT:
            return self._drain(stale=True)
        return []

    def _drain(self, stale: bool):
        frames = []
        while True:
            start = self.buf.find(SOF)
            if start < 0:
                self.buf.clear()
                break
            del self.buf[:start]
            length = self.buf[2] if len(self.buf) >= 3 else 0
            if length > MAX_PAYLOAD:
                self.errors += 1
                del self.buf[0]
                continue
            total = length + 5
            if len(self.buf) < max(total, 3):
                if stale:
                    self.errors += 1
                    del self.buf[0]
                    continue
                break
            body = bytes(self.buf[1:3 + length])
            if crc16(body) == int.from_bytes(self.buf[3 + length:total], "big"):
                frames.append((body[0], body[2:]))
                del self.buf[:total]
            else:
                self.errors += 1
                del self.buf[0]
        return frames


def decode_status(p: bytes) -> dict:
    ack, result, state, role, v1, v2, v3, proto, max_msg, last_seq, err, reset = struct.unpack(">BBBBBBBBBHHB", p[:14])
    return {
        "ack": FRAME_NAMES.get(ack, hex(ack)),
        "result": RESULTS[result] if result < len(RESULTS) else result,
        "state": STATES[state] if state < len(STATES) else state,
        "role": ROLES[role] if role < len(ROLES) else role,
        "fw": f"{v1}.{v2}.{v3}",
        "proto": proto,
        "max_msg": max_msg,
        "last_seq": last_seq,
        "errors": [name for bit, name in enumerate(ERR_FLAGS) if err & (1 << bit)],
        "reset": RESET_CAUSES[reset] if reset < len(RESET_CAUSES) else reset,
        "_result_code": result,
        "_ack_code": ack,
    }


def decode_rx_alert(p: bytes) -> dict:
    user_id, seq, lat, lon, status, battery, rssi, snr = struct.unpack(">IHiiBBhb", p[:19])
    return {
        "user_id": f"0x{user_id:08X}",
        "seq": seq,
        "lat": lat / 1e7,
        "lon": lon / 1e7,
        "status": USER_STATUS[status] if status < len(USER_STATUS) else status,
        "battery": "?" if battery == 0xFF else f"{battery}%",
        "rssi": rssi,
        "snr": snr,
        "msg": p[19:].decode("utf-8", errors="replace"),
    }


def describe(ftype: int, payload: bytes) -> str:
    stamp = time.strftime("%H:%M:%S")
    if ftype == STATUS and len(payload) >= 14:
        s = decode_status(payload)
        shown = {k: v for k, v in s.items() if not k.startswith("_")}
        return f"[{stamp}] STATUS {shown}"
    if ftype == RX_ALERT and len(payload) >= 19:
        return f"[{stamp}] RX_ALERT {decode_rx_alert(payload)}"
    if ftype == LOG:
        return f"[{stamp}] LOG {payload.decode('ascii', errors='replace')}"
    return f"[{stamp}] {FRAME_NAMES.get(ftype, hex(ftype))} {payload.hex(' ')}"


class Node:
    def __init__(self, port: str, baud: int):
        import serial  # imported here so --help works without pyserial

        self.ser = serial.Serial(port, baud, timeout=0.05)
        self.dec = FrameDecoder()
        self.pending = []   # frames decoded but not yet consumed

    def read_frames(self, duration: float):
        end = time.monotonic() + duration
        while time.monotonic() < end:
            if not self.pending:
                data = self.ser.read(256)
                self.pending.extend(self.dec.feed(data) if data else self.dec.poll())
            while self.pending:
                yield self.pending.pop(0)

    def request(self, ftype: int, payload: bytes = b"", retries: int = 3, timeout: float = 0.5):
        """Sends a command and waits for its STATUS acknowledgement."""
        for attempt in range(retries):
            self.ser.write(encode_frame(ftype, payload))
            for rtype, rpayload in self.read_frames(timeout):
                if rtype == STATUS and len(rpayload) >= 14 and rpayload[0] == ftype:
                    print(describe(rtype, rpayload))
                    return decode_status(rpayload)
                print(describe(rtype, rpayload))
            print(f"  (no ACK, attempt {attempt + 1}/{retries})")
        return None

    def monitor(self, duration: float = float("inf")):
        try:
            for rtype, rpayload in self.read_frames(duration):
                print(describe(rtype, rpayload), flush=True)
        except KeyboardInterrupt:
            pass


def alert_payload(args) -> bytes:
    msg = args.msg.encode("utf-8")
    battery = 0xFF if args.battery is None else args.battery
    return struct.pack(">iiBB", round(args.lat * 1e7), round(args.lon * 1e7), args.status, battery) + msg


def cmd_config(node: Node, args):
    cr = args.cr - 4 if args.cr >= 5 else args.cr   # accepts 5..8 (4/x) or 1..4
    role = {"beacon": 1, "sniffer": 2}[args.role]
    payload = struct.pack(">BBIHBBBbHB", PROTO_VERSION, role, args.user_id, args.interval, args.sf,
                          BW_CODES[args.bw], cr, args.power, args.preamble, 0)
    return node.request(CONFIG, payload)


def cmd_selftest(node: Node, _args):
    """Robustness check of the node's frame decoder (serial side only)."""
    ok = True

    def expect(name, status, result="OK"):
        nonlocal ok
        good = status is not None and status["result"] == result
        ok &= good
        print(f"  {'PASS' if good else 'FAIL'}: {name}")

    print("1) random bytes followed by GET_STATUS")
    node.ser.write(bytes(range(256)))
    expect("resync after noise", node.request(GET_STATUS))

    print("2) corrupted frame followed by a valid one in the same write")
    bad = bytearray(encode_frame(GET_STATUS))
    bad[-1] ^= 0xFF
    node.ser.write(bytes(bad))
    expect("valid frame after CRC error", node.request(GET_STATUS))

    print("3) stray SOF + huge length stalls the parser (100 ms timeout must recover it)")
    node.ser.write(bytes([SOF, 0x01, 0xF0]))
    time.sleep(0.05)
    expect("frame hidden behind stalled candidate", node.request(GET_STATUS, retries=1, timeout=0.6))

    print("4) unknown frame type")
    expect("unknown type answered", node.request(0x42), result="E_TYPE")

    print("5) invalid CONFIG (power 30 dBm)")
    payload = struct.pack(">BBIHBBBbHB", PROTO_VERSION, 1, 1, 300, 9, 0, 1, 30, 8, 0)
    expect("parameter validation", node.request(CONFIG, payload), result="E_PARAM")

    print("6) CONFIG at SF11 exceeds the 400 ms dwell limit")
    payload = struct.pack(">BBIHBBBbHB", PROTO_VERSION, 1, 1, 300, 11, 0, 1, 14, 8, 0)
    expect("dwell-time validation", node.request(CONFIG, payload), result="E_TOA")

    print("ALL PASS" if ok else "SOME TESTS FAILED")
    return ok


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-p", "--port", default="/dev/ttyUSB0")
    parser.add_argument("-b", "--baud", type=int, default=115200)
    parser.add_argument("--follow", action="store_true", help="keep printing frames after the command")
    # --follow is also accepted after the subcommand
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--follow", action="store_true", default=argparse.SUPPRESS,
                        help="keep printing frames after the command")
    sub = parser.add_subparsers(dest="cmd", required=True)
    add = sub.add_parser

    def add_parser(name, **kw):
        return add(name, parents=[common], **kw)

    sub.add_parser = add_parser

    sub.add_parser("monitor", help="print every frame from the node")
    sub.add_parser("status", help="GET_STATUS")
    sub.add_parser("stop", help="ALERT_STOP")
    sub.add_parser("bootloader", help="ENTER_BOOTLOADER")
    sub.add_parser("selftest", help="serial robustness checks (changes no state except two rejected CONFIGs)")

    cfg = sub.add_parser("config", help="CONFIG")
    cfg.add_argument("--role", choices=["beacon", "sniffer"], required=True)
    cfg.add_argument("--user-id", type=lambda v: int(v, 0), default=0)
    cfg.add_argument("--interval", type=int, default=300, help="seconds (30..3600)")
    cfg.add_argument("--sf", type=int, default=9)
    cfg.add_argument("--bw", type=int, choices=sorted(BW_CODES), default=125)
    cfg.add_argument("--cr", type=int, default=5, help="coding rate 4/x: 5..8")
    cfg.add_argument("--power", type=int, default=14, help="dBm (-9..22)")
    cfg.add_argument("--preamble", type=int, default=8)

    for name in ("start", "update"):
        p = sub.add_parser(name, help="ALERT_START" if name == "start" else "UPDATE")
        p.add_argument("--lat", type=float, required=True)
        p.add_argument("--lon", type=float, required=True)
        p.add_argument("--status", type=int, default=1, help="0..6, see docs/protocolo_serial.md")
        p.add_argument("--battery", type=int, default=None, help="0..100, omitted = unknown")
        p.add_argument("--msg", default="")

    args = parser.parse_args()
    node = Node(args.port, args.baud)

    result = True
    if args.cmd == "monitor":
        node.monitor()
        return 0
    if args.cmd == "status":
        result = node.request(GET_STATUS)
    elif args.cmd == "stop":
        result = node.request(ALERT_STOP)
    elif args.cmd == "bootloader":
        result = node.request(ENTER_BOOTLOADER, b"BOOT")
    elif args.cmd == "config":
        result = cmd_config(node, args)
    elif args.cmd == "start":
        result = node.request(ALERT_START, alert_payload(args))
    elif args.cmd == "update":
        result = node.request(UPDATE, alert_payload(args))
    elif args.cmd == "selftest":
        return 0 if cmd_selftest(node, args) else 1

    if args.follow:
        node.monitor()
    if isinstance(result, dict):
        return 0 if result["_result_code"] == 0 else 1
    return 1


if __name__ == "__main__":
    sys.exit(main())
