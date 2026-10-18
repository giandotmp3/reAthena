# gateway

C++20 scaffold of the rathena-engine gateway (login, tickets, char-select plumbing, PIN, IP ban,
Argon2id pool seam). Follows `REIMAGINED_ARCHITECTURE.md` sections 3 and 4 and the BR-LOGIN / BR-CHAR
behavior contract in `AI_NATIVE_SPEC.md`.

- `include/gateway/types.hpp` domain model; `seams.hpp` SessionDirectory / CharacterStore / WorldBus
  (+ AccountStore, TicketStore, PasswordHasher); `inproc.hpp` in-process implementations.
- `policy.hpp` hooks supplied by `rathena.base` (name rules, login check order); `config.hpp` typed config.
- `services.hpp` API stubs (`LoginService`, `AccountAdmin`, `CharService`); stubs throw `NotImplemented`.
- `rules.hpp` pure rules (implemented). `role.hpp` `--role=all|gateway|world`.
- `tests/` acceptance tests tagged with rule IDs. `PENDING_TEST` = expected failure; an unexpected pass
  fails the run so the marker gets removed. `gateway_tests --list` prints rule coverage.

Build: `cmake -S . -B build -DGATEWAY_SANITIZE=ON && cmake --build build && build/gateway_tests`

Not owned here: BR-LOGIN-025 and BR-CHAR-030 (superseded by in-process seams), BR-LOGIN-036 and
BR-CHAR-050 (persistence/KV), BR-CHAR-033..049, 051, 053, 054 (other services), BR-CHAR-052 (ruleset/base),
BR-LOGIN-020 (LAN subnet, P2, deferred). Secrets come from the environment only (`${DATABASE_URL}`).
