#!/usr/bin/env python3
"""DoIP + UDS 诊断交互客户端，用于验证 M2/M3。

用法：
    python3 tools/doip_uds_client.py [host] [port]
    默认连接 127.0.0.1:13400
"""

import socket
import struct
import sys


def doip_frame(payload_type, payload=b""):
    version = 0x02  # ISO 13400-2:2012
    return struct.pack("!BBHI", version, (~version) & 0xFF, payload_type, len(payload)) + payload


def read_doip_frame(sock):
    header = sock.recv(8)
    if len(header) < 8:
        raise RuntimeError("connection closed")
    version, inv, payload_type, length = struct.unpack("!BBHI", header)
    payload = b""
    while len(payload) < length:
        chunk = sock.recv(length - len(payload))
        if not chunk:
            raise RuntimeError("connection closed")
        payload += chunk
    return payload_type, payload


def uds_request(sid, params=b""):
    uds = bytes([sid]) + params
    # DoIP Diagnostic Message: source_addr(2) + target_addr(2) + UDS
    payload = struct.pack("!HH", 0x0E00, 0x0E80) + uds
    return doip_frame(0x8001, payload)


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 13400

    # 1. DoIP Vehicle Identification（UDP 单播）
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp.settimeout(3)
    udp.sendto(doip_frame(0x0001), (host, port))
    data, _ = udp.recvfrom(4096)
    udp.close()
    _, _, pt, _length = struct.unpack("!BBHI", data[:8])
    payload = data[8:]
    print("[1] VehicleIdentification -> type=0x%04x VIN=%s" %
          (pt, payload[0:17].decode("ascii", "ignore")))

    # 2. TCP 建链 + Routing Activation
    sock = socket.create_connection((host, port), timeout=5)
    sock.sendall(doip_frame(0x0005, struct.pack("!HB", 0x0E00, 0x00) + b"\x00" * 8))
    pt, payload = read_doip_frame(sock)
    if pt != 0x0006:
        raise RuntimeError("routing activation failed type=0x%04x" % pt)
    print("[2] RoutingActivation    -> type=0x%04x tester=0x%04x entity=0x%04x code=0x%02x" %
          (pt, struct.unpack("!H", payload[0:2])[0],
           struct.unpack("!H", payload[2:4])[0], payload[4]))

    # 3. UDS 0x3E 00 保持会话
    sock.sendall(uds_request(0x3E, b"\x00"))
    _, payload = read_doip_frame(sock)
    print("[3] UDS 0x3E 00          -> %s" % payload[4:].hex())

    # 4. UDS 0x10 02 进入编程会话
    sock.sendall(uds_request(0x10, b"\x02"))
    _, payload = read_doip_frame(sock)
    print("[4] UDS 0x10 02          -> %s" % payload[4:].hex())

    # 5. UDS 0x22 F190 读软件版本
    sock.sendall(uds_request(0x22, b"\xF1\x90"))
    _, payload = read_doip_frame(sock)
    print("[5] UDS 0x22 F190        -> %s version=%s" %
          (payload[4:].hex(), payload[7:].decode("ascii", "ignore")))

    sock.close()


if __name__ == "__main__":
    main()
