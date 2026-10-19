# gateway

C++20 implementation of the rathena-engine gateway: login, one-time tickets, char-select and character
lifecycle, PIN, IP ban, password hashing, the three architecture seams and the packet handlers for the
login, char-select and world-connect phases. It follows `REIMAGINED_ARCHITECTURE.md` sections 3 and 3.1 and
the BR-LOGIN / BR-CHAR behavior contract in `AI_NATIVE_SPEC.md`. The gateway is mechanism only: slot counts,
name rules, starting data, PIN complexity, cleanup order and login-check order sit behind `PolicyHooks`
(`policy.hpp`); `DefaultNamePolicy` is the default policy FIXTURE with the legacy numbers and is the only
place that holds them (production binds `rathena.base` there).

## Layout

| Path | What |
|---|---|
| `include/gateway/types.hpp`, `seams.hpp` | domain model; seams `SessionDirectory`, `CharacterStore`, `WorldBus` plus `AccountStore`, `TicketStore`, `PasswordHasher`, `DnsblResolver`, `AuditSink`, `AccountRegistryPurger` |
| `inproc.hpp`, `loopback.hpp` | in-process implementations; `InProcWorldBus` is the recording test double, `LoopbackWorldBus` the real bounded-mailbox transport (gateway posts, the sim thread drains) |
| `persist_adapter.hpp` | `PersistAccountStore`, `PersistCharacterStore`, `PersistRegistryPurger` over `persistence/` (reads through `RecordStore`/`KvStore`, writes through `WriteBehindService` and its bounded WAL) |
| `services.hpp` | `LoginService`, `AccountAdmin`, `CharService` |
| `hasher.hpp`, `crypto.hpp` | `Argon2idHasher`, `Pbkdf2Sha256Hasher`, `LayeredHasher`, bounded `HashPool` / `PooledHasher`; MD5, SHA-256, HMAC, PBKDF2, base64 |
| `wire.hpp`, `packet_handlers.hpp` | `PacketCodec`: a seam over netproto's `CodecSet` and its full 1,186-packet catalog (no interpreter of its own); `LoginConnection`, `CharConnection`, `WorldConnection` |
| `runtime.hpp`, `role.hpp`, `config.hpp` | gateway-owned IO queue and threads, rate limiter, strand; `--role` and config parsing |
| `rules.hpp` | pure rules (unchanged from the scaffold) |

## Build and test

    cmake -S . -B build && cmake --build build && build/gateway_tests          # -Wall -Wextra -Wpedantic -Werror
    cmake -S . -B build-asan -DGATEWAY_SANITIZE=ON      ...                      # ASan + UBSan
    cmake -S . -B build-tsan -DGATEWAY_SANITIZE=thread  ...                      # TSan
    build/gateway_tests --list | --only=<rule or test name substring>

Needs: C++20 compiler, system SQLite (persistence), Python 3 + PyYAML (netproto's codegen runs into the build
tree). netproto and persistence are compiled read-only from their units (`GATEWAY_NETPROTO_DIR`,
`GATEWAY_PERSISTENCE_DIR`). `libargon2.so.1` is optional and loaded with `dlopen` at runtime.
Test status: `N passed, 0 failed, 0 expected-failures, 0 unexpected passes`; the harness still fails the run
on an unexpected pass of a `PENDING_TEST`, but no test is pending any more.

## Password hashing: what is and is not Argon2id

* `Argon2idHasher` is real Argon2id: libargon2 through `dlopen` (`argon2id_hash_encoded` / `argon2id_verify`).
  Known answers are checked against an independent implementation (Python `cryptography`).
* `Pbkdf2Sha256Hasher` is NOT Argon2id. It is the always-available fallback (PBKDF2-HMAC-SHA256, RFC 8018,
  checked against the RFC 7914 vectors), labelled `pbkdf2-sha256 (NOT argon2id)` by `algorithm_name()`.
* `LayeredHasher` (what `make_default_hasher` returns) uses Argon2id when the library loads, else PBKDF2. It
  verifies either PHC string by prefix, verifies legacy plaintext / MD5 rows, and reports `needs_rehash` for
  them and for weaker parameters (a PBKDF2 row is upgraded once Argon2id is available). A row whose prefix is
  `$argon2id$` cannot be verified without the library: it fails closed.
* `PasswordScheme::Argon2id` therefore means "a current PHC hash string", not "produced by libargon2".
* Lazy rehash: the first successful login of a `Plaintext` / `Md5` row writes a PHC hash. A failed login, or a
  refusal after the password check, leaves the row untouched.
* Hashing runs on `HashPool` (bounded workers, bounded queue); a full queue throws `HashOverloaded` at once and
  the login is refused with `Reason::Overloaded` (client code 1). The strand is released while hashing.

## Failure modes (architecture 3.1)

| Failure | Behavior | Test |
|---|---|---|
| Database down | reads throw `StoreUnavailable`; logins are refused with code 1 and `Reason::DatabaseDown`; char-select work is refused (packet layer answers `SC_NOTIFY_BAN 1` and disconnects); writes already accepted spill to the WAL and replay exactly once; WAL full => writes refused loudly, the gateway stays closed until it drains | `ARCH-FAIL-DB` |
| Gateway overload | per-address rate limit, bounded IO queue, bounded hash queue, bounded world mailbox: all refuse at admission (never grow, never block the sim) | `SEC-OVERLOAD` |
| Sim isolation | the sim tick shares no lock with the gateway; a login flood with real hashing, and a 250 ms strand hold, leave a fake 20 ms tick within 40 ms worst case and 10 ms p99 | `ARCH-ISOLATION` |
| Duplicate-login races | concurrent logins: later login wins, exactly one valid ticket, counters exact; concurrent char enters: one winner, the rest get 8 | `BR-LOGIN-018` |

## Behaviors intentionally dropped or changed

Dropped (replaced by the in-process seams):
* BR-LOGIN-025 / BR-CHAR-030: char-server and map-server registration with `${INTER_SERVER_USER}` / `${INTER_SERVER_PASS}`, slot tables,
  server list upkeep, `0x2710`..`0x2742` framing. There is nothing to register: login, char and world are one
  process, and a future split is a new implementation of `WorldBus`/`SessionDirectory`.
* BR-LOGIN-036: the `##` registry relay belongs to persistence (`GlobalAccount` KV scope); the gateway only
  purges it on account deletion (BR-LOGIN-037) through `AccountRegistryPurger`.
* BR-LOGIN-004: `use_MD5_passwords` is removed (spec section 3). Legacy MD5 rows are verified and upgraded.

Changed on purpose (each has a test):
* Registration flood window opens at the first registration (the legacy tick comparison never opened it for the first one).
* IP failure counter counts login results 0 and 1 itself, independent of any log (BR-LOGIN-023 defect fix).
* Web-token retry loop is capped at 20 and the login proceeds without a token after that (BR-LOGIN-034).
* Stale-online cleanup skips connected entries and continues (BR-CHAR-029).
* GM operations re-check the requester's group in the gateway (BR-CHAR-055).
* A refused login (banned, duplicate, group gate, world down) leaves the account row untouched; legacy had already rotated ids and bumped `logincount`.
* A `Login`-stage ticket is rejected at the world hop and a `World`-stage ticket at the char hop; the world ticket is bound to the selected character. A client cannot present login ids to the world and skip the PIN.
* The PIN failure counter is per account (survives reconnects, forgotten after `pin_failure_window_s`), the first PIN can only be set while none exists, and a stored PIN hash can be salted with a server secret from `${GATEWAY_PIN_PEPPER}`.
* VIP slots follow `vip_time` at read time; the legacy `+/- slots` bookkeeping on the stored column (which double counts) is not repeated.
* `-2` online entries are purged by age (`online_lost_purge_s`), not by a global timer tick.
* Name verdicts follow the spec/test (short, control chars, `#`, reserved = -1; letter policy = -2); legacy `char_check_char_name` returns -2 for short names, control characters and `#`.

## Known limits (not done here)

No socket listener (accept loop, TLS, per-connection read/write): the handlers take bytes and return frames.
`main.cpp` is a bring-up skeleton. IP bans are in memory. The DNSBL resolver and the login/char audit sink are
interfaces with test doubles only. The packet layer targets `re/20211103`; main flavour (19-byte `0x0436`),
the `0x0841` accessible-map flow and pre-2011 PIN-less clients are not covered. There is no private codec: `PacketCodec`
is a seam over netproto's `CodecSet` and its full catalog (see "netproto findings").
The world-side executor of the delete cleanup plan and of `AccountEvent` broadcasts does not exist yet; the
gateway hands both over through `WorldBus`.

## netproto findings and how the gateway settled them

The gateway's former private codec was proven against netproto's by byte-identity tests, which also listed where the two
disagreed. The legacy headers decide each case:

| Finding | Verdict | In the gateway |
|---|---|---|
| Refusal of an invalid connect packet was `ZC_REFUSE_ENTER` on 0x006a, 3 bytes | wrong both ways: `clif_parse_WantToConnection` (`clif.cpp:10685`) writes opcode 0x006a with `packet_len(0x6a)` = 23 bytes and the code at byte 2; `ZC_REFUSE_ENTER` is 0x0074 / 3 bytes and the legacy server never sends it; `clif_authfail_fd` (`clif.cpp:829`) sends `SC_NOTIFY_BAN` 0x0081 / 3 bytes | the refusal is the 23-byte 0x006a (the catalog lists it as the length-only `OPAQUE_006A`, `wire::kConnectRefusal`; unused bytes are zero, the legacy left them unset), auth failure stays `SC_NOTIFY_BAN` (`errorCode`) |
| `CHARACTER_INFO.level`, `.bIsChangedCharName` read unsigned | `int16` in the struct | signed `i16` from the catalog |
| `CA_SSO_LOGIN_REQ.token` raw | `char token[]`, text | string tail (decode cuts at the NUL; `safestrncpy` to 31 bytes is kept) |
| `AC_ACK_HASH.salt` raw | `char salt[]`, **but** `loginclif.cpp:363-367` sends `4 + md5keylen` bytes with no terminator, while netproto's string tails always end in a NUL | kept as a raw tail (no NUL) through `kRawFields`; with a NUL a client that sizes the key by `packetLength` would hash a 21-byte salt |
| `PACKET_TC_RESULT` fixed(34) | has a `packetLength` word | variable (header 34), length written |
| `encode` silently cut long strings; `memcpy(dst, nullptr, 0)` for an empty raw tail (wire.cpp:401); `array_len` ignored | defects | netproto's encode refuses a string that does not fit with its NUL, an embedded NUL, an unknown field name; empty tails encode without `memcpy`; arrays are honoured. Handler-owned display text (server name 20, map names 16, character names 24, dates 20) is cut by `wire::fit`, never client input |
| Field names (`auth_code`, `result`, `error`, `new`) | the legacy struct members are `login_id1`, `errorCode`, `new_`, `character.*` | renamed everywhere |

`kRawFields` (in `wire.cpp`) is the only difference from netproto's catalog: seven fixed `char[N]` members the legacy code
copies as raw, unterminated bytes are typed `raw`, not `string`: `CA_EXE_HASHCHECK.hash` (an MD5 digest; a NUL byte would cut it,
and a string-typed hash would reject about 6% of real clients), the PIN fields and `CH_DELETE_CHAR3.birthdate` (4 and 6 bytes with
no room for a NUL, so a string encode of a full field is refused), and `AC_ACK_HASH.salt`. A test proves the catalog is netproto's
except exactly these seven, and the gateway fails to start if netproto drops one of them. The fix belongs in netproto's catalog.

Gating: the gate of a frame is the `gate` column of the catalog's client table (`peek_frame` -> `FrameEntry::gate` ->
`gate_packet(session phase, gate)`), nothing else. Each listener starts in `PreSession`, where only the connect packet of
its server is accepted; the char listener moves to `PreMapLoad` after a good `CH_ENTER`; the world listener moves to
`PreMapLoad` after a good `CZ_ENTER` and to `InWorld` after `CZ_NOTIFY_ACTORINIT` (`HandlerResult::map_loaded`). A packet in
the wrong phase, an opcode that is not a client packet of the catalog (including server packets and length-only frames) and a
bad declared length each disconnect. Tests walk all 338 client opcodes through all three listeners.
