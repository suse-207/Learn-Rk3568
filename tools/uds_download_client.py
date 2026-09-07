#!/usr/bin/env python3
"""DoIP + UDS 下载客户端（0x34 / 0x36 / 0x37）。

用法：
    python3 tools/uds_download_client.py [host] [port] [文件]
"""

import socket
import struct
import sys


def doip_frame(payload_type, payload=b""):
    version = 0x02
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
    payload = struct.pack("!HH", 0x0E00, 0x0E80) + uds
    return doip_frame(0x8001, payload)


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 13400
    filepath = sys.argv[3] if len(sys.argv) > 3 else "sample.bin"

    data = open(filepath, "rb").read()
    size = len(data)

    sock = socket.create_connection((host, port), timeout=10)

    # DoIP Vehicle Identification + Routing Activation
    sock.sendall(doip_frame(0x0001))
    read_doip_frame(sock)
    sock.sendall(doip_frame(0x0005, struct.pack("!HB", 0x0E00, 0x00) + b"\x00" * 8))
    read_doip_frame(sock)

    # 0x10 02 进入编程会话
    sock.sendall(uds_request(0x10, b"\x02"))
    read_doip_frame(sock)

    # 0x34 RequestDownload：DataFormat=0x00, LengthFormat=0x44(4+4), 地址=0, 大小
    req34 = b"\x00\x44" + b"\x00\x00\x00\x00" + struct.pack("!I", size)
    sock.sendall(uds_request(0x34, req34))
    _, payload = read_doip_frame(sock)
    print("[0x34] RequestDownload resp: %s" % payload[4:].hex())

    # 0x36 TransferData：按 4096 分块，块序号从 1 递增
    block = 0
    offset = 0
    while offset < size:
        block = (block + 1) & 0xFF
        chunk = data[offset:offset + 4096]
        offset += len(chunk)
        sock.sendall(uds_request(0x36, bytes([block]) + chunk))
        _, payload = read_doip_frame(sock)
        resp = payload[4:]
        if resp[0] != 0x76:
            print("[0x36] NEG block=%d resp=%s" % (block, resp.hex()))
            break

    # 0x37 RequestTransferExit
    sock.sendall(uds_request(0x37, b""))
    _, payload = read_doip_frame(sock)
    print("[0x37] TransferExit resp: %s" % payload[4:].hex())

    print("sent %d bytes in %d blocks" % (size, block))
    sock.close()


if __name__ == "__main__":
    main()
