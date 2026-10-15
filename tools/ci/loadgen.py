#!/usr/bin/env python3
"""Load generator for the map-server sim-tick benchmark (tools/ci/simtick.sh).

Drives N concurrent bots against a running login/char/map server set. Each bot:
  1. logs in, registering its account through the `_M` suffix (needs new_account: yes),
  2. creates a character and selects it,
  3. enters the map server and finishes loading (LoadEndAck),
  4. follows a warp to its home cell when the server sends one (simtick.sh loads an NPC that
     warps every player to a random cell of the benchmark map on login; LoadEndAck again),
  5. plays for the rest of the run: walks to a random nearby cell every --walk-interval
     seconds, says something in public chat every --chat-interval seconds, sends the
     keep-alive tick every --tick-interval seconds, optionally attacks (--attack-interval)
     the nearest monster it has been told about, and drains everything the server sends.
Bots start evenly spread over --ramp seconds; the steady phase lasts --duration seconds after
the last bot is in. The bots then quit and the run ends.

Packet layouts are those of the default build (PACKETVER 20211103, renewal), as in
tools/ci/smoke_client.py; no packet obfuscation, so the char server must run with
`pincode_enabled: no`. Packets used:
  login  CA_LOGIN 0x64                        -> AC_ACCEPT_LOGIN 0xac4
  char   CH_ENTER 0x65, CH_MAKE_CHAR 0xa39,   -> HC_ACCEPT_MAKECHAR 0xb6f,
         CH_SELECT_CHAR 0x66                  -> HC_NOTIFY_ZONESVR 0xac5
  map    CZ_ENTER 0x436 (23 bytes), CZ_NOTIFY_ACTORINIT (LoadEndAck) 0x7d,
         CZ_REQUEST_MOVE 0x35f (5: packed x/y/dir), CZ_REQUEST_TIME 0x360 (6),
         CZ_REQUEST_CHAT 0xf3 (variable: "<name> : <text>"),
         CZ_REQUEST_ACT 0x437 (7: target id, action 0), CZ_REQUEST_QUIT 0x18a
  watched from the server: ZC_NPCACK_MAPMOVE 0x91 (the home warp), ZC_NOTIFY_*ENTRY / STANDENTRY
  with a monster object type (target candidates for --attack-interval)

Prints progress lines, a `STEADY <unix time>` line when every bot that will make it is on the
map, a `STEADY_END <unix time>` line when the measured phase ends, and a final JSON summary
(`SUMMARY {...}`) with per-phase failures, packets sent, bytes received, and the client's own
event-loop lag (the generator is the bottleneck when that lag gets large).

    tools/ci/loadgen.py --bots 200 --ramp 60 --duration 120
"""
from __future__ import annotations

import argparse
import asyncio
import json
import multiprocessing
import os
import random
import struct
import sys
import time

PACKETVER = 20211103


def fixed(s: str, n: int) -> bytes:
    return s.encode("latin1")[: n - 1].ljust(n, b"\0")


def pack_pos(x: int, y: int, d: int = 0) -> bytes:
    # WBUFPOS: 10 bits x, 10 bits y, 4 bits direction, big-endian bit order
    return bytes(((x >> 2) & 0xFF, ((x << 6) | ((y >> 4) & 0x3F)) & 0xFF, ((y << 4) | (d & 0xF)) & 0xFF))


class Stats:
    def __init__(self) -> None:
        self.ok = 0
        self.failed: dict[str, int] = {}
        self.sent = {"walk": 0, "chat": 0, "tick": 0, "attack": 0}
        self.bytes_in = 0
        self.disconnects = 0
        self.lag_ms: list[float] = []

    def fail(self, phase: str) -> None:
        self.failed[phase] = self.failed.get(phase, 0) + 1


async def read_until(reader: asyncio.StreamReader, pred, timeout: float) -> bytes:
    """Reads until pred(data) is true; returns everything read (raises on timeout/EOF)."""
    data = b""
    end = time.monotonic() + timeout
    while not pred(data):
        left = end - time.monotonic()
        if left <= 0:
            raise TimeoutError
        chunk = await asyncio.wait_for(reader.read(65536), left)
        if not chunk:
            raise ConnectionError("closed")
        data += chunk
    return data


def opcode_at(data: bytes, opcode: int) -> int:
    return data.find(struct.pack("<H", opcode))


class Bot:
    def __init__(self, idx: int, args, stats: Stats, stop_at: list) -> None:
        self.idx = idx
        self.args = args
        self.stats = stats
        self.stop_at = stop_at
        self.user = f"{args.prefix}{idx:05d}"
        self.name = f"{args.prefix.capitalize()}{idx:05d}"
        self.x = self.y = 0
        self.monsters: dict[int, float] = {}

    async def run(self) -> None:
        a = self.args
        phase = "login"
        try:
            # 1. login ------------------------------------------------------------------
            r, w = await asyncio.open_connection(a.host, a.login_port)
            w.write(struct.pack("<HI", 0x64, PACKETVER) + fixed(self.user + "_M", 24) + fixed(a.password, 24) + b"\x00")
            data = await read_until(r, lambda d: (i := opcode_at(d, 0xAC4)) >= 0 and len(d) >= i + 4 + 64 + 6 or opcode_at(d, 0x83E) >= 0, a.timeout)
            w.close()
            i = opcode_at(data, 0xAC4)
            if i < 0:
                raise RuntimeError("login refused")
            login_id1, aid, login_id2 = struct.unpack_from("<III", data, i + 4)
            sex = data[i + 46]
            char_ip_port = struct.unpack_from("<H", data, i + 64 + 4)[0]

            # 2. char server ------------------------------------------------------------
            phase = "char"
            r, w = await asyncio.open_connection(a.host, char_ip_port)
            w.write(struct.pack("<HIIIHB", 0x65, aid, login_id1, login_id2, 0, sex))
            await read_until(r, lambda d: len(d) >= 4, a.timeout)
            await asyncio.sleep(0.3)  # let the character list arrive
            phase = "create"
            w.write(struct.pack("<H", 0xA39) + fixed(self.name, 24) + struct.pack("<BHHIB", 0, 1, 1, 0, 1))
            await read_until(r, lambda d: opcode_at(d, 0xB6F) >= 0 or opcode_at(d, 0x6E) >= 0, a.timeout)
            phase = "select"
            w.write(struct.pack("<HB", 0x66, 0))
            data = await read_until(r, lambda d: (i := opcode_at(d, 0xAC5)) >= 0 and len(d) >= i + 28, a.timeout)
            i = opcode_at(data, 0xAC5)
            cid = struct.unpack_from("<I", data, i + 2)[0]
            map_port = struct.unpack_from("<H", data, i + 26)[0]
            w.close()

            # 3. map server -------------------------------------------------------------
            phase = "map"
            r, w = await asyncio.open_connection(a.host, map_port)
            w.write(struct.pack("<HIIIIIB", 0x436, aid, cid, login_id1, int(time.time()) & 0xFFFFFFFF, 0, sex))
            data = await read_until(r, lambda d: len(d) >= 2, a.timeout)
            if struct.unpack_from("<H", data, 0)[0] == 0x81:
                raise RuntimeError("map refused")
            await asyncio.sleep(0.5)
            w.write(struct.pack("<H", 0x7D))
            if a.expect_warp:
                phase = "warp"
                data = await read_until(r, lambda d: (i := opcode_at(d, 0x91)) >= 0 and len(d) >= i + 22, a.timeout)
                i = opcode_at(data, 0x91)
                self.x, self.y = struct.unpack_from("<HH", data, i + 18)
                w.write(struct.pack("<H", 0x7D))
            self.stats.ok += 1
        except Exception:
            self.stats.fail(phase)
            return

        # 4. play -----------------------------------------------------------------------
        drain = asyncio.ensure_future(self.drain(r))
        try:
            await self.play(w)
        except Exception:
            self.stats.disconnects += 1
        finally:
            try:
                w.write(struct.pack("<HH", 0x18A, 0))
                await asyncio.sleep(1.0)
                w.close()
            except Exception:
                pass
            drain.cancel()

    async def drain(self, r: asyncio.StreamReader) -> None:
        buf = b""
        while True:
            chunk = await r.read(65536)
            if not chunk:
                return
            self.stats.bytes_in += len(chunk)
            if self.args.attack_interval > 0:
                # Remember monster ids the server shows us. ZC_NOTIFY_STANDENTRY/NEWENTRY/MOVEENTRY
                # (0x9fe/0x9ff/0x9fd at PACKETVER 20211103) start with length(2), objecttype(1),
                # AID(4); objecttype 5 is a monster. A cheap scan, good enough for picking targets.
                buf = (buf + chunk)[-8192:]
                for op in (b"\xfd\x09", b"\xfe\x09", b"\xff\x09"):
                    j = buf.find(op)
                    while 0 <= j <= len(buf) - 9:
                        if buf[j + 4] == 5:
                            self.monsters[struct.unpack_from("<I", buf, j + 5)[0]] = time.monotonic()
                        j = buf.find(op, j + 2)
                if len(self.monsters) > 64:
                    for k in sorted(self.monsters, key=self.monsters.get)[:32]:
                        del self.monsters[k]

    async def play(self, w: asyncio.StreamWriter) -> None:
        a = self.args
        now = time.monotonic()
        rnd = random.Random(self.idx)
        nxt = {
            "walk": now + rnd.uniform(0, a.walk_interval),
            "chat": now + rnd.uniform(0, a.chat_interval) if a.chat_interval > 0 else float("inf"),
            "tick": now + rnd.uniform(0, a.tick_interval),
            "attack": now + rnd.uniform(0, a.attack_interval) if a.attack_interval > 0 else float("inf"),
        }
        while time.time() < self.stop_at[0]:
            kind = min(nxt, key=nxt.get)
            delay = nxt[kind] - time.monotonic()
            if delay > 0:
                await asyncio.sleep(min(delay, max(0.0, self.stop_at[0] - time.time())))
                if time.time() >= self.stop_at[0]:
                    break
            if w.is_closing():
                raise ConnectionError("closed")
            if kind == "walk":
                if self.x and self.y:
                    tx = max(1, self.x + rnd.randint(-a.walk_radius, a.walk_radius))
                    ty = max(1, self.y + rnd.randint(-a.walk_radius, a.walk_radius))
                else:
                    tx, ty = rnd.randint(20, 280), rnd.randint(20, 360)
                w.write(struct.pack("<H", 0x35F) + pack_pos(tx, ty))
                nxt[kind] += a.walk_interval * rnd.uniform(0.5, 1.5)
            elif kind == "chat":
                text = f"{self.name} : hello from bot {self.idx} at {int(time.time()) % 100000}".encode("latin1")
                w.write(struct.pack("<HH", 0xF3, 4 + len(text)) + text)
                nxt[kind] += a.chat_interval * rnd.uniform(0.5, 1.5)
            elif kind == "tick":
                w.write(struct.pack("<HI", 0x360, int(time.time() * 1000) & 0xFFFFFFFF))
                nxt[kind] += a.tick_interval
            elif kind == "attack":
                if self.monsters:
                    target = max(self.monsters, key=self.monsters.get)
                    w.write(struct.pack("<HIB", 0x437, target, 0))
                    self.stats.sent["attack"] += 1
                nxt[kind] += a.attack_interval * rnd.uniform(0.5, 1.5)
                continue
            self.stats.sent[kind] += 1
            await w.drain()


async def lag_probe(stats: Stats, stop_at: list) -> None:
    while time.time() < stop_at[0] + 2:
        t = time.monotonic()
        await asyncio.sleep(0.1)
        stats.lag_ms.append((time.monotonic() - t - 0.1) * 1000)


async def run_slice(args, indices: list[int], start: float, stop_at: list, steady_at: float) -> dict:
    stats = Stats()
    probe = asyncio.ensure_future(lag_probe(stats, stop_at))
    tasks = []
    for n, idx in enumerate(indices):
        at = start + (idx - args.first) * args.ramp / max(1, args.bots)
        delay = at - time.time()
        if delay > 0:
            await asyncio.sleep(delay)
        tasks.append(asyncio.ensure_future(Bot(idx, args, stats, stop_at).run()))
    await asyncio.gather(*tasks)
    probe.cancel()
    lag = sorted(stats.lag_ms) or [0.0]
    return {
        "ok": stats.ok,
        "failed": stats.failed,
        "sent": stats.sent,
        "bytes_in": stats.bytes_in,
        "disconnects": stats.disconnects,
        "lag_p50_ms": lag[len(lag) // 2],
        "lag_p99_ms": lag[min(len(lag) - 1, int(len(lag) * 0.99))],
        "lag_max_ms": lag[-1],
        "cpu_s": sum(os.times()[:2]),
    }


def slice_main(args, indices, start, stop_at, steady_at, queue) -> None:
    queue.put(asyncio.run(run_slice(args, indices, start, [stop_at], steady_at)))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--login-port", type=int, default=6900)
    ap.add_argument("--bots", type=int, default=50)
    ap.add_argument("--first", type=int, default=1, help="index of the first bot (account/char names)")
    ap.add_argument("--prefix", default="simbot", help="account name prefix (character: capitalized)")
    ap.add_argument("--password", default="botpass1", help="fake test password shared by the bots")
    ap.add_argument("--ramp", type=float, default=30.0, help="seconds over which the bots log in")
    ap.add_argument("--settle", type=float, default=10.0, help="seconds after the ramp before the measured phase")
    ap.add_argument("--duration", type=float, default=60.0, help="seconds of the measured (steady) phase")
    ap.add_argument("--procs", type=int, default=1, help="client processes (spread bots over cores)")
    ap.add_argument("--walk-interval", type=float, default=2.0)
    ap.add_argument("--walk-radius", type=int, default=6)
    ap.add_argument("--chat-interval", type=float, default=30.0, help="0 disables chat")
    ap.add_argument("--tick-interval", type=float, default=12.0)
    ap.add_argument("--attack-interval", type=float, default=0.0, help="0 disables attacking")
    ap.add_argument("--no-warp", dest="expect_warp", action="store_false",
                    help="do not wait for the login warp to the benchmark map (no simtick NPC loaded)")
    ap.add_argument("--timeout", type=float, default=30.0, help="per-step timeout while logging in")
    args = ap.parse_args()

    start = time.time() + 1.0
    steady_at = start + args.ramp + args.settle
    stop_at = steady_at + args.duration
    indices = list(range(args.first, args.first + args.bots))
    procs = max(1, min(args.procs, args.bots))
    slices = [indices[i::procs] for i in range(procs)]
    print(f"loadgen: {args.bots} bots over {args.ramp:.0f}s, steady at {steady_at:.0f}, until {stop_at:.0f}, {procs} process(es)", flush=True)

    ctx = multiprocessing.get_context("fork")
    queue = ctx.Queue()
    workers = [ctx.Process(target=slice_main, args=(args, s, start, stop_at, steady_at, queue)) for s in slices]
    for p in workers:
        p.start()
    t0 = time.time()
    while time.time() < steady_at:
        time.sleep(min(10.0, max(0.0, steady_at - time.time())))
        print(f"loadgen: t+{time.time() - t0:.0f}s", flush=True)
    print(f"STEADY {time.time():.3f}", flush=True)
    time.sleep(max(0.0, stop_at - time.time()))
    print(f"STEADY_END {time.time():.3f}", flush=True)
    results = [queue.get() for _ in workers]
    for p in workers:
        p.join()

    summary = {
        "bots": args.bots,
        "ok": sum(r["ok"] for r in results),
        "failed": {},
        "sent": {k: sum(r["sent"][k] for r in results) for k in results[0]["sent"]},
        "bytes_in": sum(r["bytes_in"] for r in results),
        "disconnects": sum(r["disconnects"] for r in results),
        "lag_p50_ms": max(r["lag_p50_ms"] for r in results),
        "lag_p99_ms": max(r["lag_p99_ms"] for r in results),
        "lag_max_ms": max(r["lag_max_ms"] for r in results),
        "client_cpu_s": sum(r["cpu_s"] for r in results),
        "procs": procs,
    }
    for r in results:
        for k, v in r["failed"].items():
            summary["failed"][k] = summary["failed"].get(k, 0) + v
    print("SUMMARY " + json.dumps(summary), flush=True)
    return 0 if summary["ok"] == args.bots and summary["disconnects"] == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
