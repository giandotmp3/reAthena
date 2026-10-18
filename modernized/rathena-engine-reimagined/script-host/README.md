# script-host (rathena-engine-reimagined)

Embedded Luau host: per-mod sandboxes, ordered cancellable event bus with
transactional effect queues, coroutine dialogue, budgets, memory accounting,
generational hot reload, namespaced storage API, typed API schema and numeric
helpers. Stack: C++20, CMake, Luau (optional in the scaffold).

Status: scaffold. Domain types and API stubs exist; stubs throw
`NotImplemented`. Behavior is specified by executable tests in `tests/`.

- `include/script_host/` public API and domain model
- `api/engine.yaml` schema that will generate C++ bindings and `engine.d.luau`
- `tests/` acceptance tests; `PENDING(rule, name)` is an expected failure
  tagged with the rule ID, and an unexpected pass (XPASS) fails the run
- `luau/sandbox_probes/` hostile mod snippets for the sandbox suite

Run: `cmake -S . -B build && cmake --build build && ctest --test-dir build`
or `build/tests/script_host_tests --tag BR-RULESET-116`.

Rule tags: BR-RULESET-115/116/117 come from the spec. Rules without a spec ID
(ordering, budgets, sandbox, reload, storage, dialogue, schema) are drawn from
architecture section 5 and tagged with capability IDs CAP-E03 / CAP-E04.
