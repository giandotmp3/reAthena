# M0 spikes (measurement gate for the architecture forks)

Real Luau (tag 0.690) embedded the way `script-host` will embed it. Results: `analysis/rathena-engine/M0_RESULTS.md`.

Build Luau, then the host (paths are examples):

```sh
git clone --depth 1 --branch 0.690 https://github.com/luau-lang/luau.git luau && cd luau
cmake -S . -B b -DCMAKE_BUILD_TYPE=Release -DLUAU_BUILD_TESTS=OFF -DLUAU_BUILD_WEB=OFF
cmake --build b --target Luau.Repl.CLI Luau.Analyze.CLI -j8 && cd ..
L=luau; g++ -std=c++17 -O2 -I$L/VM/include -I$L/Compiler/include host.cpp \
  $L/b/libLuau.Compiler.a $L/b/libLuau.Ast.a $L/b/libLuau.VM.a -o m0host
g++ -std=c++17 -O2 -I$L/VM/include -I$L/Compiler/include states.cpp \
  $L/b/libLuau.Compiler.a $L/b/libLuau.Ast.a $L/b/libLuau.VM.a -o states
./run_m0.sh ./m0host ./states
```

| Spike | Files |
|---|---|
| M0(a) damage cost | `m0a_damage.luau`, `m0a_heavy.luau` |
| M0(c) hooks, sandbox, memory | `m0c_hooks.luau`, `m0c_sandbox.luau`, `m0c_loop.luau`, `m0c_native.luau`, `states.cpp` |
| M0(d) numeric fidelity | `m0d_numeric.luau` |
| M0(b) legacy NPC interpreter | `legacy_interp/` (see its RESULTS.md) |
