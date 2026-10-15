# Map-server sim tick: measurement and results

NFR (`analysis/rathena/REIMAGINED_ARCHITECTURE.md` §5): **map sim tick p99 ≤ 50 ms at 1,000 CCU per
map server**, as legacy parity. Phase 4's exit criteria include "sim-tick NFR met". This page records
how the tick is measured on the legacy server and what the legacy baseline is. The reimagined map
service must be measured the same way, with the same tool, before Phase 4 can close.

## What is measured

The legacy server is single-threaded. Each turn of its main loop (`src/common/core.cpp`) runs the
due timers (`do_timer`) and then `do_sockets`. `do_sockets` flushes the send queues, **waits** in
`select`/`epoll_wait`, receives, parses every session's input (the packet handlers), and flushes
again. One **sim tick** is the work between two waits: wake up, then receive/parse/send, then the
timers (mob AI, skills, walking, autosave and so on), then the pre-wait flush, then back to sleep.
The idle wait itself is excluded.

- `src/common/tickstats.{hpp,cpp}` keeps a log-linear histogram over microseconds (exact below
  64 µs, then 32 sub-buckets per power of two, so every reported percentile is within about 3%; it
  reports the bucket's upper bound, capped at the true maximum). `do_sockets` calls
  `tickstats_wait_begin()` / `tickstats_wait_end()` around the poll. The map server registers
  `map_usercount` so the summaries can show how many players are online.
- **Off by default.** It is enabled by the environment:
  - `RATHENA_TICKSTATS=<seconds>`: log one line per window and a whole-run line on shutdown:
    `Tick stats (window 5s): 2485 iterations, p50 0.024 ms, p95 0.407 ms, p99 1.023 ms, max 8.671 ms, busy 6.4%, 1000 users`.
    `busy` is the share of wall time the main thread spent working.
  - `RATHENA_TICKSTATS_FILE=<path>`: also append each window's histogram as one JSON line, so a
    tool can merge exactly the windows it wants.
- **Overhead.** When off, the cost is one predictable branch on a global `bool` per loop turn
  (0.3 ns per iteration in a microbenchmark). When on, it is two `steady_clock::now()` calls plus a
  histogram increment, about 60 ns per iteration on the machine below. At about 530 iterations/s
  that is roughly 0.003% of one core. Nothing else in the server changes.

## Tools

- `tools/ci/loadgen.py` (Python 3 stdlib, asyncio, optionally several processes) drives N bots
  through the real protocol (PACKETVER 20211103):
  - login `0x64` → `0xac4` (registration with the `_M` suffix);
  - char `0x65`, create `0xa39`, select `0x66` → `0xac5`;
  - map `0x436`, LoadEndAck `0x7d`; the login warp `0x91` is followed by `0x7d` again.
  - Once on the map, each bot repeats these actions until the run ends:
    - walks (`CZ_REQUEST_MOVE 0x35f`, packed 3-byte x/y) to a random cell within 6 cells, every 2 s ± 50%;
    - talks in public chat (`0xf3`, `"<name> : <text>"`) every 30 s ± 50%;
    - sends the keep-alive tick (`0x360`) every 12 s;
    - optionally attacks (`0x437`) a monster it was shown (`--attack-interval`).
  - Bots drain everything the server sends.
  - Bots log in evenly over `--ramp` seconds. After a `--settle` period comes the measured phase
    (`--duration`). The tool prints `STEADY`/`STEADY_END` timestamps and a JSON summary that
    includes the client's own event-loop lag (to tell a saturated client from a slow server).
- `tools/ci/simtick.sh` runs the whole measurement:
  - builds Release servers into `build-simtick/<re|prere>/` (or takes `--bin`);
  - starts them in `build-simtick/run-<mode>-<scenario>-<ccu>/` on a private configuration. This is
    the shipped `conf/` plus a private `conf/import/` that sets `new_account: yes`, raises
    `allowed_regs`, sets `pincode_enabled: no`, puts `allow: 127.0.0.1` past the DDoS guard, and
    uses a fresh SQLite database. A small NPC warps every player on login to a random walkable cell
    of `--map` (default prontera), or within `--crowd R` cells of `--center`;
  - runs the load generator for each `--ccu` level;
  - merges the map server's window histograms that lie fully inside the measured phase;
  - prints a table and exits non-zero when p99 > `--max-p99-ms` (default 50) or fewer than 95% of
    the bots were online.

```sh
tools/ci/simtick.sh --ccu "100 500 1000" --duration 120              # bots spread over prontera
tools/ci/simtick.sh --ccu "100 500 1000" --crowd 15 --duration 90    # everyone in one 31×31 square
```

## Results (renewal, legacy server, 2026-10-15)

Machine: 4 vCPU Intel Xeon @ 2.10 GHz (model string "Intel(R) Xeon(R) Processor @ 2.10GHz"),
15 GiB RAM, Linux 6.18, gcc 13.3, CMake Release build (`-O2`), select() poller (the default; FD_SETSIZE 1024),
SQLite backend. The sandbox was shared with other build jobs (load average 4–11 during the runs),
so absolute numbers are pessimistic. The load generator ran on the same machine (1 process per
250 bots).

The per-level JSON is in `build-simtick/run-re-<scenario>-<ccu>/result.json`. Measured phase:
60 s (120 s at 1,000 spread, 90 s at 1,000 crowd). The p50–max and busy columns are steady state.
"Login p99" is the whole run including the login ramp.

**Spread**: every bot on a random cell of prontera, so each one sees a handful of others.

| CCU | online | ticks/s | p50 ms | p95 ms | **p99 ms** | max ms | busy | login p99 ms | client lag p99 |
|----:|-------:|--------:|-------:|-------:|-----------:|-------:|-----:|-------------:|---------------:|
| 100 | 100 | 77 | 0.017 | 0.167 | **3.2** | 11.9 | 0.9% | 3.3 | 4.8 ms |
| 250 | 250 | 145 | 0.021 | 0.227 | **2.1** | 12.1 | 1.7% | — | 4.7 ms |
| 500 | 500 | 265 | 0.019 | 0.287 | **1.9** | 11.5 | 2.9% | 2.4 | 4.8 ms |
| 1,000 | 1,000 | 531 | 0.023 | 0.343 | **0.7** | 11.9 | 5.0% | 1.2 | 4.1 ms |

**Crowd** (`--crowd 15`): every bot inside the 31×31 square around (156,191), so every walk and chat
line is broadcast to almost everyone. This is the worst case for area broadcasts.

| CCU | online | ticks/s | p50 ms | p95 ms | **p99 ms** | max ms | busy | login p99 ms | client lag p99 |
|----:|-------:|--------:|-------:|-------:|-----------:|-------:|-----:|-------------:|---------------:|
| 100 | 100 | 81 | 0.079 | 0.527 | **3.5** | 16.0 | 1.6% | 3.5 | 2.1 ms |
| 250 | 250 | 144 | 0.179 | 1.023 | **4.7** | 16.8 | 5.7% | 4.9 | 10.6 ms |
| 500 | 500 | 269 | 0.119 | 1.503 | **4.4** | 14.2 | 10.4% | 4.2 | 9.5 ms |
| 1,000 | 1,000 | 393 | 0.191 | 4.223 | **7.9** | 20.8 | 33.6% | 7.4 | 16.9 ms |

In the 1,000-bot crowd run the bots received 852 MB in about 200 s and used 172 CPU-seconds
(about 0.9 of a core, out of the 4 that client and server shared). The client's event-loop lag
stayed under 34 ms at the worst point, so the generator kept up; it was not the limit.

### Verdict

**The legacy server meets the NFR with a wide margin: p99 7.9 ms at 1,000 CCU in the worst-case
crowd and 0.7 ms spread out, against a 50 ms budget.** The worst steady-state tick was 21 ms. The
main thread was busy 34% of the time in the crowd case, which leaves headroom of about 3×. The
baseline the reimagined map service must match is therefore "p99 well under 10 ms at 1,000 CCU on
this traffic", not just "≤ 50 ms".

Notes:

- **Why p99 falls as CCU rises (spread).** Per-iteration percentiles are diluted by the many cheap
  wake-ups that only parse one packet. The tail comes from timer-heavy iterations: mob AI every
  100 ms, the 1 s player/NPC timers. Those make up a smaller share of all iterations when there is
  more socket traffic. `busy` and max track the load more faithfully. Compare like with like, and
  use the same tool for the reimagined server.
- **Login and logout storms are outside the steady state.** The whole-run maximum at 1,000 CCU was
  670 ms, a single iteration during the mass logout at the end. Every bot quits within about 1 s,
  and the map server saves and frees 1,000 characters in one turn. During the 100 s login ramp
  (10 logins/s) the whole-run p99 stayed at 1.2 ms. The NFR is about the steady state. A
  "1,000 logouts at once" budget would be a separate NFR if one is wanted.
- **Not exercised:**
  - combat at scale. `--attack-interval` exists and works on a field map, but the benchmark map
    is a town;
  - skills, NPC dialogues, trading and vending;
  - several maps per server;
  - pre-renewal (`--prere`).
- **Connection ceiling.** The default select() build has `MAXCONN = FD_SETSIZE = 1024`, so a map
  server built this way cannot hold many more than about 1,000 clients. Beyond that, build with
  `-DENABLE_EXTRA_SOCKET_POLL=ON` (epoll).
- **Shutdown fault seen once.** In an early 1,000-bot run the servers were killed while 1,000
  characters were still logging out. The map server's memory manager then reported `aFree ...
  overflowed pointer` on a write FIFO (`socket.cpp` `realloc_writefifo`) during shutdown. This is a
  legacy issue unrelated to the instrumentation. `simtick.sh` now waits until the map server reports
  0 users before stopping it, and the fault did not recur.

## CI

The `database` job of `.github/workflows/build_servers_database.yml` runs
`tools/ci/simtick.sh --bin . --ccu 50 --duration 30 --max-p99-ms 200` on the binaries it has just
built. It adds about 1.5 minutes. Shared runners are noisy, so the step is `continue-on-error`. Its
logs, `result.json` and window histograms are uploaded with the smoke logs. Run the full matrix
above by hand on quiet hardware before signing off Phase 4.
