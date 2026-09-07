#!/usr/bin/env python3
"""DoIP UDP Discovery 客户端。

用法：
    python3 tools/doip_discovery_client.py [host] [port]
    默认 127.0.0.1:13400

覆盖：
    - 0x0001 Vehicle Identification Request
    - 0x4001 DoIP Entity Status Request
    - 0x4003 Diagnostic Power Mode Information Request
"""

import socket
import struct
import sys


def doip_frame(payload_type, payload=b""):
    version = 0x02
    return struct.pack("!BBHI", version, (~version) & 0xFF, payload_type, len(payload)) + payload


def request(sock, addr, payload_type, payload=b""):
    sock.sendto(doip_frame(payload_type, payload), addr)
    data, _ = sock.recvfrom(4096)
    if len(data) < 8:
        raise RuntimeError("short DoIP response")
    _, _, resp_type, length = struct.unpack("!BBHI", data[0:8])
    resp_payload = data[8:8 + length]
    return resp_type, resp_payload


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 13400
    addr = (host, port)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(3)

    resp_type, payload = request(sock, addr, 0x0001)
    print("[1] VehicleIdentification -> type=0x%04x VIN=%s logical=0x%04x" %
          (resp_type, payload[0:17].decode("ascii", "ignore"),
           struct.unpack("!H", payload[17:19])[0]))

    resp_type, payload = request(sock, addr, 0x4001)
    print("[2] EntityStatus          -> type=0x%04x node=0x%02x max_sockets=%u current=%u max_data=%u" %
          (resp_type, payload[0], payload[1], payload[2],
           struct.unpack("!I", payload[3:7])[0]))

    resp_type, payload = request(sock, addr, 0x4003)
    print("[3] PowerMode             -> type=0x%04x mode=0x%02x" % (resp_type, payload[0]))


if __name__ == "__main__":
    main()
