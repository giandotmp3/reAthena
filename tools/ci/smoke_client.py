#!/usr/bin/env python3
"""Minimal Ragnarok client for end-to-end smoke tests of the servers and their database.

Drives one full session against a running login/char/map server set:
  1. login (registers the account through the `_M` suffix; needs new_account: yes)
  2. char server: create a character, select it
  3. map server: enter, finish loading, quit (the map server saves the character)

Packet layouts are those of the default build (PACKETVER 20211103, renewal); packet
obfuscation keys are zero for clients after 2018-03-07, so no encryption is needed. The char
server must run with `pincode_enabled: no`.

Exit code 0 when every step succeeded. Usage:
    tools/ci/smoke_client.py --user smoke1 --password smokepw1 --char Smokey
"""
from __future__ import annotations

import argparse
import socket
import struct
import sys
import time


def recv_for(sock: socket.socket, seconds: float) -> bytes:
    """Collects everything the server sends within `seconds`."""
    sock.settimeout(0.2)
    data = b""
    end = time.time() + seconds
    while time.time() < end:
        try:
            chunk = sock.recv(65536)
            if not chunk:
                break
            data += chunk
        except socket.timeout:
            continue
    return data


def find(data: bytes, opcode: int) -> int:
    return data.find(struct.pack("<H", opcode))


def fixed(s: str, n: int) -> bytes:
    return s.encode("latin1")[: n - 1].ljust(n, b"\0")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--login-port", type=int, default=6900)
    ap.add_argument("--user", default="smoke1")
    ap.add_argument("--password", default="smokepw1")
    ap.add_argument("--char", default="Smokey")
    ap.add_argument("--register", action="store_true", help="append _M to the user id (auto-register)")
    ap.add_argument("--no-create", action="store_true", help="select the existing character in slot 0")
    ap.add_argument("--stay", type=float, default=0, help="stay on the map this many seconds before quitting")
    ap.add_argument("--until-file", help="with --stay: leave as soon as this file contains --until-text")
    ap.add_argument("--until-text", default="")
    args = ap.parse_args()

    # 1. login ------------------------------------------------------------------------------
    userid = args.user + ("_M" if args.register else "")
    s = socket.create_connection((args.host, args.login_port), timeout=10)
    s.sendall(struct.pack("<HI", 0x64, 20211103) + fixed(userid, 24) + fixed(args.password, 24) + b"\x00")
    data = recv_for(s, 2.0)
    s.close()
    i = find(data, 0x0AC4)
    if i < 0:
        j = find(data, 0x083E)
        code = struct.unpack_from("<I", data, j + 2)[0] if j >= 0 else None
        print(f"login: refused (code {code}), raw={data[:32].hex()}")
        return 1
    login_id1, aid, login_id2 = struct.unpack_from("<III", data, i + 4)
    sex = data[i + 46]
    char_port = struct.unpack_from("<H", data, i + 64 + 4)[0]
    print(f"login: ok aid={aid} sex={sex} char_port={char_port}")

    # 2. char server ------------------------------------------------------------------------
    c = socket.create_connection((args.host, char_port), timeout=10)
    c.sendall(struct.pack("<HIIIHB", 0x65, aid, login_id1, login_id2, 0, sex))
    data = recv_for(c, 2.0)
    if len(data) < 4 or struct.unpack_from("<I", data, 0)[0] != aid:
        print(f"char: not accepted, raw={data[:32].hex()}")
        return 1
    print("char: entered")

    if not args.no_create:
        # create (0xa39): name, slot, hair_color, hair_style, job, sex
        c.sendall(struct.pack("<H", 0xA39) + fixed(args.char, 24) + struct.pack("<BHHIB", 0, 1, 1, 0, 1))
        data = recv_for(c, 2.0)
        i = find(data, 0x0B6F)
        if i < 0:
            j = find(data, 0x006E)
            print(f"char: create refused (code {data[j + 2] if j >= 0 else None}), raw={data[:32].hex()}")
            return 1
        char_id = struct.unpack_from("<I", data, i + 2)[0]
        print(f"char: created char_id={char_id}")

    # select slot 0 (0x66) -> zone server (0xac5)
    c.sendall(struct.pack("<HB", 0x66, 0))
    data = recv_for(c, 2.0)
    i = find(data, 0x0AC5)
    if i < 0:
        print(f"char: select failed, raw={data[:48].hex()}")
        return 1
    cid = struct.unpack_from("<I", data, i + 2)[0]
    mapname = data[i + 6 : i + 22].split(b"\0")[0].decode("latin1")
    map_port = struct.unpack_from("<H", data, i + 26)[0]
    print(f"char: selected char_id={cid} map={mapname} map_port={map_port}")
    c.close()

    # 3. map server -------------------------------------------------------------------------
    m = socket.create_connection((args.host, map_port), timeout=10)
    # CZ_ENTER 0x436 (23 bytes): aid, cid, login_id1, client_tick, unknown(4), sex
    m.sendall(struct.pack("<HIIIIIB", 0x436, aid, cid, login_id1, int(time.time()) & 0xFFFFFFFF, 0, sex))
    data = recv_for(m, 3.0)
    if not data or find(data, 0x0081) == 0:
        print(f"map: refused, raw={data[:16].hex()}")
        return 1
    print(f"map: entered ({len(data)} bytes received)")
    m.sendall(struct.pack("<H", 0x7D))  # LoadEndAck: the character becomes active
    recv_for(m, 2.0)
    end = time.time() + args.stay
    while time.time() < end:
        m.sendall(struct.pack("<HI", 0x360, int(time.time() * 1000) & 0xFFFFFFFF))  # keep-alive tick
        if not recv_for(m, 5.0) and m.fileno() < 0:
            break
        if args.until_file:
            try:
                with open(args.until_file, encoding="latin1") as f:
                    if args.until_text in f.read():
                        break
            except OSError:
                pass
    m.sendall(struct.pack("<HH", 0x18A, 0))  # quit
    data = recv_for(m, 2.0)
    m.close()
    print("map: quit" + (" acknowledged" if find(data, 0x018B) >= 0 else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
