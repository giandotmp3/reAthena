// Luau-backed behavior: sandbox containment, budgets, hooks through the bus,
// storage and RNG bindings, reload generations with live coroutines.
#include <dirent.h>

#include <algorithm>

#include "fixtures.hpp"
#include "script_host/dialogue.hpp"
#include "script_host/numeric.hpp"

using namespace script_host;
using fx::contains;
using fx::env;

// ---- sandbox probes: every file in luau/sandbox_probes is contained ---------------------
LUAU_TEST("CAP-E03", "sandbox probes: each hostile snippet is contained and the host survives") {
  struct Expect { const char* file; RunStatus status; };
  const Expect expected[] = {
      {"infinite_loop.luau", RunStatus::BudgetExceeded},
      {"memory_bomb.luau", RunStatus::MemoryExceeded},
      {"shared_mutation.luau", RunStatus::Error},
      {"slow_pattern.luau", RunStatus::BudgetExceeded},
      {"metatable_escape.luau", RunStatus::Error},
  };
  // Every probe file in the directory must have an expectation (new probes need one).
  std::vector<std::string> on_disk;
  if (DIR* d = opendir(PROBE_DIR)) {
    while (auto* e = readdir(d)) {
      std::string n = e->d_name;
      if (n.size() > 5 && n.substr(n.size() - 5) == ".luau") on_disk.push_back(n);
    }
    closedir(d);
  }
  CHECK_EQ(on_disk.size(), sizeof(expected) / sizeof(expected[0]));
  ModSandbox neighbour(env("neighbour"));
  for (auto& x : expected) {
    ModSandbox s(env("evil"));
    auto r = s.run(x.file, fx::probe(x.file));
    CHECK_MSG(r.status == x.status, std::string(x.file) + " -> " + r.error);
    CHECK(contains(r.error, "[evil]"));
    // The neighbour mod is unaffected and its string metatable is intact.
    CHECK(neighbour.run("n", "assert(('x'):upper() == 'X' and math.floor(1.5) == 1)").status == RunStatus::Ok);
  }
}

LUAU_TEST("CAP-E03", "sandbox: a budget abort leaves the sandbox usable and pcall cannot swallow it") {
  ModSandbox s(env("m"));
  CHECK(s.run("a", "while true do end").status == RunStatus::BudgetExceeded);
  CHECK(s.run("b", "return 1 + 1").status == RunStatus::Ok);
  auto r = s.run("c", "while true do pcall(function() while true do end end) end");
  CHECK(r.status == RunStatus::BudgetExceeded);
  CHECK(s.run("d", "local t = {} for i = 1, 100 do t[i] = i end assert(#t == 100)").status == RunStatus::Ok);
  CHECK(!s.disabled());
}

LUAU_TEST("CAP-E03", "sandbox: getfenv, setfenv, loadstring, os, io, debug, require and print-to-stdout are gone") {
  ModSandbox s(env("m"));
  auto r = s.run("p", R"(
    assert(getfenv == nil, "getfenv")
    assert(setfenv == nil, "setfenv")
    assert(loadstring == nil, "loadstring")
    assert(os == nil and io == nil and debug == nil and require == nil, "libs")
    assert(dofile == nil and load == nil and newproxy ~= nil)
    assert(not pcall(collectgarbage, "stop"), "collectgarbage stop")
    assert(not pcall(collectgarbage, "collect"), "collectgarbage collect")
    assert(type(collectgarbage("count")) == "number")
  )");
  CHECK_MSG(r.status == RunStatus::Ok, r.error);
}

LUAU_TEST("CAP-E03", "sandbox: shared libraries and metatables are read-only, mod globals are private") {
  ModSandbox a(env("a")), b(env("b"));
  auto r = a.run("p", R"(
    assert(not pcall(function() math.pi = 3 end))
    assert(not pcall(function() string.format = nil end))
    assert(not pcall(function() table.insert = nil end))
    assert(not pcall(function() rawset(math, "floor", nil) end))
    assert(not pcall(function() getmetatable("").__index = nil end))
    assert(not pcall(function() setmetatable(getmetatable(""), {}) end))
    assert(not pcall(function() _G.coroutine = nil end))
    mine = 41
  )");
  CHECK_MSG(r.status == RunStatus::Ok, r.error);
  CHECK(a.run("q", "assert(mine == 41)").status == RunStatus::Ok);       // persists in the same mod
  CHECK(b.run("q", "assert(mine == nil and math.pi > 3.14)").status == RunStatus::Ok);  // invisible to others
}

LUAU_TEST("CAP-E03", "sandbox: table.sort is bounded and its default comparator is interruptible") {
  ModSandbox s(env("m"));
  auto r = s.run("ok", R"(
    local t = {5, 3, 9, 1}
    table.sort(t) assert(t[1] == 1 and t[4] == 9)
    table.sort(t, function(a, b) return a > b end) assert(t[1] == 9 and t[4] == 1)
  )");
  CHECK_MSG(r.status == RunStatus::Ok, r.error);
  // Over the element cap: refused outright.
  auto big = s.run("big", "local t = {} for i = 1, 70000 do t[i] = i end table.sort(t)");
  CHECK_MSG(big.status == RunStatus::Error, big.error);
  CHECK(contains(big.error, "too many elements"));
  // Under the cap but too much comparison work for the step budget: aborted.
  ModSandbox tight(env("tight"));
  auto slow = tight.run("slow", "local t = {} for i = 1, 60000 do t[i] = (i * 7919) % 60013 end table.sort(t)");
  CHECK(slow.status == RunStatus::BudgetExceeded);
}

LUAU_TEST("CAP-E03", "sandbox: string.rep cannot be used to request a huge allocation (counts as memory failure)") {
  ModSandbox s(env("m", 2 << 20));
  CHECK(s.run("a", "return ('ab'):rep(10)").status == RunStatus::Ok);
  CHECK(s.run("b", "local x = string.rep('x', 2^31)").status == RunStatus::MemoryExceeded);
  CHECK(s.disabled());
}

LUAU_TEST("CAP-E03", "sandbox: memory cap disables only the offender; reenable restores it") {
  ModSandbox hog(env("hog", 1 << 20)), good(env("good", 1 << 20));
  auto r = hog.run("bomb", "local t = {} for i = 1, 1e7 do t[i] = {i} end");
  CHECK(r.status == RunStatus::MemoryExceeded);
  CHECK(hog.disabled());
  CHECK(hog.memory_peak() <= (1u << 20));
  CHECK(hog.run("again", "return 1").status == RunStatus::MemoryExceeded);  // refuses to run while disabled
  CHECK(good.run("fine", "local t = {} for i = 1, 1000 do t[i] = i end").status == RunStatus::Ok);
  CHECK(!good.disabled());
  hog.reenable();
  CHECK(hog.run("after", "return 1").status == RunStatus::Ok);
}

LUAU_TEST("CAP-E03", "sandbox: wall-clock budget trips through the injected clock") {
  auto spec = env("m");
  spec.budget.steps_per_dispatch = 100000000;
  spec.budget.wall_us_per_dispatch = 1000;
  ModSandbox s(spec);
  std::uint64_t t = 0;
  s.meter().set_clock([&] { return t += 700; });  // every sample advances 0.7 ms
  auto r = s.run("spin", "local n = 0 for i = 1, 100000 do n = n + i end");
  CHECK(r.status == RunStatus::BudgetExceeded);
  CHECK(contains(r.error, "wall-clock"));
}

LUAU_TEST("CAP-E03", "sandbox: the unbounded_loop permission lifts the step limit but not the wall clock") {
  auto plain = env("plain");
  ModSandbox a(plain);
  CHECK(a.run("l", "local n = 0 for i = 1, 400000 do n = n + 1 end").status == RunStatus::BudgetExceeded);
  auto freeloop = env("free");
  freeloop.granted.insert(Permission::UnboundedLoop);
  freeloop.budget.wall_us_per_tick = 60ull * 1000 * 1000;
  ModSandbox b(freeloop);
  CHECK(b.run("l", "local n = 0 for i = 1, 400000 do n = n + 1 end").status == RunStatus::Ok);
  auto strict_wall = freeloop;
  strict_wall.mod_id = "free2";
  strict_wall.budget.wall_us_per_tick = 2000;
  ModSandbox c(strict_wall);
  std::uint64_t t = 0;
  c.meter().set_clock([&] { return t += 1000; });
  CHECK(c.run("l", "while true do end").status == RunStatus::BudgetExceeded);  // wall clock still applies
}

LUAU_TEST("CAP-E03", "sandbox: errors name the mod and chunk and carry a trace") {
  ModSandbox s(env("my.mod"));
  auto r = s.run("chunk1", "local function f() error('boom') end\nf()");
  CHECK(r.status == RunStatus::Error);
  CHECK(contains(r.error, "[my.mod]"));
  CHECK(contains(r.error, "chunk1"));
  CHECK(contains(r.error, "boom"));
  auto syn = s.run("bad", "this is not luau (");
  CHECK(syn.status == RunStatus::Error);
  CHECK(contains(syn.error, "bad"));
}

LUAU_TEST("CAP-E03", "sandbox: permission failures are typed") {
  ModSandbox s(env("m"));
  StorageApi st;
  s.set_storage(&st);
  auto r = s.run("acct", "storage.account(7):set('k', 1)");
  CHECK(r.status == RunStatus::PermissionDenied);
  auto r2 = s.run("other", "storage.world():mod('someone_else')");
  CHECK(r2.status == RunStatus::PermissionDenied);
}

LUAU_TEST("CAP-E03", "sandbox: registration APIs validate their input and are closed once the mod is running") {
  ModSandbox s(env("m"));
  CHECK(s.run("load", "events.on('a:b', {id='h'}, function(ctx) end)").status == RunStatus::Ok);
  CHECK_EQ(s.take_hooks().size(), std::size_t(1));
  CHECK(s.take_hooks().empty());
  CHECK(s.run("bad1", "events.on('NoNamespace', {id='h'}, function() end)").status == RunStatus::Error);
  CHECK(s.run("bad2", "events.on('a:b', {id='x/y'}, function() end)").status == RunStatus::Error);
  CHECK(s.run("bad3", "events.on('a:b', {id='h', phase='sideways'}, function() end)").status == RunStatus::Error);
  CHECK(s.run("bad4", "events.on('a:b', {id='h', permit_protected=true}, function() end)").status == RunStatus::Error);
  CHECK(s.run("bad5", "events.on('a:b', {id='h', after='notalist'}, function() end)").status == RunStatus::Error);
  CHECK(s.run("bad6", "events.on('a:b', {}, function() end)").status == RunStatus::Error);
  CHECK(s.take_hooks().empty());
}

LUAU_TEST("CAP-E03", "sandbox: events.on from inside a running hook is refused") {
  ReloadManager rm;
  fx::load(rm, "m", "events.on('x:y', { id = 'sneaky' }, function() events.on('x:y', { id = 'late' }, function() end) end)");
  HookContext ctx;
  auto r = rm.bus().dispatch("x:y", ctx);
  CHECK_EQ(r.errors.size(), std::size_t(1));
  CHECK(contains(r.errors[0].message, "only available while the mod chunk loads"));
  CHECK_EQ(rm.bus().size(), std::size_t(1));
}

// ---- Lua hooks through the reload manager and the bus ---------------------------------------
LUAU_TEST("BR-RULESET-115", "ctx int64 methods from Luau: exact past 2^53, truncating idiv, overflow is a script error") {
  ReloadManager rm;
  fx::load(rm, "m", R"(
    events.on('calc:go', { id = 'm1', mode = 'modify' }, function(ctx)
      ctx:mul_pct(100)
      ctx:add(2)
      ctx:sub(1)
    end)
    events.on('calc:neg', { id = 'm2', mode = 'modify' }, function(ctx)
      ctx:idiv(2)
      ctx:min1()
    end)
    events.on('calc:over', { id = 'm3', mode = 'modify' }, function(ctx) ctx:mul(4) end)
    events.on('calc:str', { id = 'm4', mode = 'modify' }, function(ctx) ctx:f32_step(1.0) assert(ctx:get_str() == '16777216') end)
  )");
  HookContext c1;
  c1.num.set(9007199254740993LL);
  rm.bus().dispatch("calc:go", c1);
  CHECK_EQ(c1.num.value(), std::int64_t(9007199254740994LL));  // +2 -1, no double rounding
  HookContext c2;
  c2.num.set(-7);
  rm.bus().dispatch("calc:neg", c2);
  CHECK_EQ(c2.num.value(), std::int64_t(1));  // -3 then min1
  HookContext c3;
  c3.num.set(INT64_MAX / 2);
  auto r = rm.bus().dispatch("calc:over", c3);
  CHECK_EQ(r.errors.size(), std::size_t(1));
  CHECK_EQ(c3.num.value(), INT64_MAX / 2);  // rolled back
  HookContext c4;
  c4.num.set(16777217);
  auto r4 = rm.bus().dispatch("calc:str", c4);
  CHECK_MSG(r4.errors.empty(), r4.errors.empty() ? "" : r4.errors[0].message);
}

LUAU_TEST("BR-RULESET-116", "Luau pre hook cancel discards the effects queued so far; later hooks do not run") {
  ReloadManager rm;
  fx::load(rm, "m", R"(
    events.on('battle:hit', { id = 'queue', phase = 'pre', priority = 0 }, function(ctx) ctx:enqueue('m:poison', 'target1', '{"n":1}') end)
    events.on('battle:hit', { id = 'veto', phase = 'pre', priority = 1, mode = 'modify' }, function(ctx) ctx:cancel('immune') end)
    events.on('battle:hit', { id = 'after', phase = 'on', priority = 0 }, function(ctx) ctx:enqueue('m:never', 't') end)
  )");
  EffectQueue q;
  HookContext ctx;
  ctx.effects = &q;
  auto r = rm.bus().dispatch("battle:hit", ctx);
  CHECK(r.cancelled);
  CHECK_EQ(r.cancel_reason, std::string("immune"));
  CHECK(r.committed_effects.empty());
  CHECK(q.empty());
  CHECK_EQ(r.ran.size(), std::size_t(2));
}

LUAU_TEST("BR-RULESET-116", "Luau effects commit in hook order; observe hooks may enqueue but not change ctx") {
  ReloadManager rm;
  fx::load(rm, "m", R"(
    events.on('battle:hit', { id = 'b', priority = 2, mode = 'observe' }, function(ctx) ctx:enqueue('m:second', 't') end)
    events.on('battle:hit', { id = 'a', priority = 1, mode = 'observe' }, function(ctx) ctx:enqueue('m:first', 't') end)
    events.on('battle:hit', { id = 'c', priority = 3, mode = 'observe' }, function(ctx) ctx:set(1) end)
  )");
  HookContext ctx;
  ctx.num.set(50);
  auto r = rm.bus().dispatch("battle:hit", ctx);
  CHECK_EQ(r.committed_effects.size(), std::size_t(2));
  CHECK_EQ(r.committed_effects[0].kind, std::string("m:first"));
  CHECK_EQ(r.committed_effects[1].kind, std::string("m:second"));
  CHECK_EQ(ctx.num.value(), std::int64_t(50));
  CHECK_EQ(r.errors.size(), std::size_t(1));          // the read-only violation is a script error
  CHECK(contains(r.errors[0].message, "read-only"));
}

LUAU_TEST("CAP-E03", "a stale ctx handle captured by a hook cannot be used later") {
  ReloadManager rm;
  fx::load(rm, "m", R"(
    events.on('x:y', { id = 'grab', mode = 'modify' }, function(ctx) kept = ctx end)
    events.on('x:z', { id = 'use', mode = 'modify' }, function(ctx) kept:add(1) end)
  )");
  HookContext c1; rm.bus().dispatch("x:y", c1);
  HookContext c2;
  auto r = rm.bus().dispatch("x:z", c2);
  CHECK_EQ(r.errors.size(), std::size_t(1));
  CHECK(contains(r.errors[0].message, "stale"));
  CHECK_EQ(c2.num.value(), std::int64_t(0));
}

LUAU_TEST("CAP-E03", "a runaway Luau hook aborts only itself; three strikes auto-disable it; neighbours keep working") {
  ReloadManager rm;
  fx::load(rm, "evil", "events.on('battle:hit', { id = 'spin', priority = 0 }, function(ctx) while true do end end)");
  fx::load(rm, "good", "events.on('battle:hit', { id = 'ok', priority = 1, mode = 'modify' }, function(ctx) ctx:add(5) end)");
  for (int i = 0; i < 3; ++i) {
    rm.bus().set_tick(10 + i);
    HookContext ctx;
    auto r = rm.bus().dispatch("battle:hit", ctx);
    CHECK_EQ(r.errors.size(), std::size_t(1));
    CHECK_EQ(r.errors[0].kind, std::string("budget"));
    CHECK_EQ(ctx.num.value(), std::int64_t(5));  // good's hook ran after evil's failure
  }
  CHECK(rm.bus().is_disabled("evil/spin"));
  HookContext ctx;
  CHECK(rm.bus().dispatch("battle:hit", ctx).errors.empty());
}

LUAU_TEST("CAP-E03", "a memory-bomb hook disables its mod; other mods keep their hooks") {
  ReloadManager rm;
  auto hog = env("hog", 1 << 20);
  rm.set_environment(hog);
  fx::load(rm, "hog", "events.on('battle:hit', { id = 'bomb' }, function(ctx) local t = {} for i = 1, 1e7 do t[i] = {i} end end)");
  fx::load(rm, "good", "events.on('battle:hit', { id = 'ok', priority = 1, mode = 'modify' }, function(ctx) ctx:add(1) end)");
  HookContext ctx;
  auto r = rm.bus().dispatch("battle:hit", ctx);
  CHECK_EQ(r.errors.size(), std::size_t(1));
  CHECK_EQ(r.errors[0].kind, std::string("memory"));
  CHECK_EQ(r.auto_disabled.size(), std::size_t(1));
  CHECK_EQ(ctx.num.value(), std::int64_t(1));
}

// ---- RNG streams ------------------------------------------------------------------------------
LUAU_TEST("BR-RULESET-115", "hook RNG: derived per-hook streams leave the main stream untouched; strict_parity rejects main") {
  RngStreams rng(1234);
  ReloadManager rm;
  rm.set_rng(&rng);
  fx::load(rm, "m", R"(
    events.on('rng:go', { id = 'own' }, function(ctx) seen = rng.roll(1000000) ctx:enqueue('m:roll', tostring(seen)) end)
    events.on('rng:main', { id = 'greedy' }, function(ctx) rng.main(10) end)
  )");
  HookContext ctx;
  auto r1 = rm.bus().dispatch("rng:go", ctx);
  CHECK_EQ(r1.committed_effects.size(), std::size_t(1));
  CHECK_EQ(rng.main_draw_count(), std::uint64_t(0));
  RngStreams ref(1234);
  CHECK_EQ(r1.committed_effects[0].target, std::to_string(ref.hook_next("m", "own") % 1000000));
  rng.set_strict_parity(true);
  HookContext c2;
  auto r2 = rm.bus().dispatch("rng:main", c2);
  CHECK_EQ(r2.errors.size(), std::size_t(1));
  CHECK(contains(r2.errors[0].message, "strict_parity"));
  CHECK_EQ(rng.main_draw_count(), std::uint64_t(0));
  rng.set_strict_parity(false);
  HookContext c3;
  CHECK(rm.bus().dispatch("rng:main", c3).errors.empty());
  CHECK_EQ(rng.main_draw_count(), std::uint64_t(1));
}

LUAU_TEST("BR-RULESET-115", "strict_parity makes reload refuse a hook that declares main-stream RNG") {
  ReloadManager rm;
  rm.bus().set_strict_parity(true);
  auto r = rm.reload("m", "events.on('a:b', { id = 'g', draws_main_rng = true }, function() end)");
  CHECK(!r.ok);
  CHECK(contains(r.error, "strict_parity"));
  CHECK_EQ(rm.active_generation("m"), std::uint64_t(0));
}

// ---- reload generations ---------------------------------------------------------------------------
LUAU_TEST("CAP-E03", "reload swaps hooks atomically; a failed or conflicting reload keeps the old generation") {
  ReloadManager rm;
  fx::load(rm, "m", "events.on('calc:go', { id = 'h', mode = 'modify' }, function(ctx) ctx:set(100) end)");
  HookContext c1; rm.bus().dispatch("calc:go", c1);
  CHECK_EQ(c1.num.value(), std::int64_t(100));
  auto r2 = fx::load(rm, "m", "events.on('calc:go', { id = 'h', mode = 'modify' }, function(ctx) ctx:set(200) end)");
  CHECK_EQ(r2.active_generation, std::uint64_t(2));
  HookContext c2; rm.bus().dispatch("calc:go", c2);
  CHECK_EQ(c2.num.value(), std::int64_t(200));
  CHECK_EQ(rm.bus().size(), std::size_t(1));  // old hook gone, not doubled

  auto bad_rt = rm.reload("m", "events.on('calc:go', { id = 'h', mode = 'modify' }, function(ctx) ctx:set(300) end) error('boom at load')");
  CHECK(!bad_rt.ok);
  CHECK(contains(bad_rt.error, "boom at load"));
  CHECK_EQ(rm.active_generation("m"), std::uint64_t(2));
  HookContext c3; rm.bus().dispatch("calc:go", c3);
  CHECK_EQ(c3.num.value(), std::int64_t(200));  // v2 still serving

  fx::load(rm, "other", "events.on('calc:go', { id = 'r', mode = 'replace' }, function(ctx) end)");
  auto conflict = rm.reload("m", "events.on('calc:go', { id = 'r2', mode = 'replace' }, function(ctx) end)");
  CHECK(!conflict.ok);
  CHECK(contains(conflict.error, "m/r2") && contains(conflict.error, "other/r"));
  CHECK_EQ(rm.active_generation("m"), std::uint64_t(2));
  HookContext c4; rm.bus().dispatch("calc:go", c4);
  CHECK_EQ(c4.num.value(), std::int64_t(200));
}

LUAU_TEST("CAP-E03", "definition shape change at reload is refused with needs_restart and a precise reason") {
  ReloadManager rm;
  fx::load(rm, "m", "definitions.declare('m:burn', {'duration', 'tick'})");
  auto same = rm.reload("m", "definitions.declare('m:burn', {'tick', 'duration'})");  // order-insensitive
  CHECK(same.ok);
  auto changed = rm.reload("m", "definitions.declare('m:burn', {'duration', 'tick', 'stacks'})");
  CHECK(!changed.ok);
  CHECK(changed.needs_restart);
  CHECK(contains(changed.error, "m:burn"));
  CHECK_EQ(rm.active_generation("m"), std::uint64_t(2));
}

LUAU_TEST("CAP-E03", "timers are owned by (mod, generation): reload cancels the old generation's timers") {
  ReloadManager rm;
  fx::load(rm, "m", R"(
    ticks = 0
    events.on('t:x', { id = 'h' }, function() end)
    timers.after(100, function() ctx_log = (ctx_log or 0) + 1 end)
  )");
  int native = 0;
  rm.schedule("m", 50, [&] { ++native; });
  CHECK_EQ(rm.live_registrations("m", 1), std::size_t(3));  // hook + lua timer + native timer
  rm.advance_game_time(60);
  CHECK_EQ(native, 1);
  fx::load(rm, "m", "timers.after(10, function() fired_v2 = true end)");
  CHECK_EQ(rm.live_registrations("m", 1), std::size_t(0));  // v1 hook + lua timer cancelled
  CHECK_EQ(rm.live_registrations("m", 2), std::size_t(1));
  rm.advance_game_time(1000);
  CHECK_EQ(rm.live_registrations("m", 2), std::size_t(0));
  // v1's 100 ms timer never ran; v2's did exactly once.
  auto sb = rm.active_sandbox("m");
  CHECK(sb->run("check", "assert(fired_v2 == true and ctx_log == nil)").status == RunStatus::Ok);
}

LUAU_TEST("CAP-E03", "coroutines in flight finish in the old generation; timeout cancels them; new dialogues use the new code") {
  ReloadConfig cfg;
  cfg.drain_timeout_ms = 60000;
  ReloadManager rm(cfg);
  fx::load(rm, "npc", "version = 'old'");
  DialogueManager dm({}, &rm);
  auto s1 = dm.start(1, "npc", "say(version); say(version .. ' second')");
  auto s2 = dm.start(2, "npc", "say(version)");
  fx::load(rm, "npc", "version = 'new'");
  // Player 1's open dialogue keeps running old code.
  auto r1 = dm.resume(1, s1.token, {});
  CHECK_EQ(std::get<SayRequest>(r1.pending).text, std::string("old second"));
  // A new dialogue gets the new generation.
  auto s3 = dm.start(3, "npc", "say(version)");
  CHECK_EQ(std::get<SayRequest>(s3.pending).text, std::string("new"));
  CHECK_EQ(rm.live_registrations("npc", 1), std::size_t(2));  // players 1 and 2
  rm.advance_game_time(59999);
  CHECK(dm.has_dialogue(2));
  rm.advance_game_time(1);  // timeout reached: old generation's coroutines are cancelled
  CHECK(!dm.has_dialogue(1));
  CHECK(!dm.has_dialogue(2));
  CHECK(dm.has_dialogue(3));
  CHECK_EQ(rm.live_registrations("npc", 1), std::size_t(0));
  (void)s2;
}

LUAU_TEST("CAP-E03", "a finished old-generation dialogue releases the generation before the timeout") {
  ReloadManager rm;
  fx::load(rm, "npc", "x = 1");
  DialogueManager dm({}, &rm);
  auto s = dm.start(1, "npc", "say('hi')");
  fx::load(rm, "npc", "x = 2");
  CHECK_EQ(rm.live_registrations("npc", 1), std::size_t(1));
  auto done = dm.resume(1, s.token, {});
  CHECK(done.finished);
  CHECK_EQ(rm.live_registrations("npc", 1), std::size_t(0));
}

// ---- storage through Luau -----------------------------------------------------------------------------
LUAU_TEST("CAP-E04", "Luau storage: typed values, nested tables and arrays round-trip; keys are namespaced per mod") {
  StorageApi st;
  ReloadManager rm;
  rm.set_storage(&st);
  fx::load(rm, "a", R"(
    local w = storage.world()
    w:set('n', 42)
    w:set('f', 2.5)
    w:set('s', 'str')
    w:set('b', true)
    w:set('t', { name = 'x', list = { 10, 20, 30 }, nested = { deep = true } })
    local t = w:get('t')
    assert(t.name == 'x' and #t.list == 3 and t.list[2] == 20 and t.nested.deep == true)
    assert(w:get('n') == 42 and w:get('f') == 2.5 and w:get('s') == 'str' and w:get('b') == true)
    assert(w:get('missing') == nil)
    w:set('n', nil)
    assert(w:get('n') == nil)
    local ks = w:keys() table.sort(ks)
    assert(table.concat(ks, ',') == 'b,f,s,t', table.concat(ks, ','))
    storage.character(77):set('hp', 9007199254740991)
  )");
  auto v = st.get(Scope::World, "world", "a", "a", "n");
  CHECK(!v.has_value());
  auto hp = st.get(Scope::Character, "77", "a", "a", "hp");
  CHECK(hp.has_value());
  CHECK_EQ(std::get<std::int64_t>(*hp), std::int64_t(9007199254740991LL));
  // Same key in another mod is a different row.
  fx::load(rm, "b", "storage.world():set('s', 'mine')");
  CHECK_EQ(std::get<std::string>(*st.get(Scope::World, "world", "a", "a", "s")), std::string("str"));
  CHECK_EQ(std::get<std::string>(*st.get(Scope::World, "world", "b", "b", "s")), std::string("mine"));
}

LUAU_TEST("CAP-E04", "Luau storage: cross-mod read needs the permission and a grant; writes to others never work") {
  StorageApi st;
  ReloadManager rm;
  rm.set_storage(&st);
  fx::load(rm, "owner", "storage.world():set('secret', 'x')");
  auto nosperm = env("snoop");
  rm.set_environment(nosperm);
  auto r = rm.reload("snoop", "assert(storage.world():mod('owner'):get('secret') == nil)");
  CHECK(!r.ok);                                   // no StorageReadOther permission
  auto withperm = env("snoop2");
  withperm.granted.insert(Permission::StorageReadOther);
  rm.set_environment(withperm);
  auto r2 = rm.reload("snoop2", "local v = storage.world():mod('owner'):get('secret')");
  CHECK(!r2.ok);                                  // permission but no grant_read from the host
  CHECK(contains(r2.error, "storage.read:owner"));
  st.grant_read("snoop2", "owner");
  fx::load(rm, "snoop2", "assert(storage.world():mod('owner'):get('secret') == 'x')");
  auto w = rm.reload("snoop2", "storage.world():mod('owner'):set('secret', 'pwned')");
  CHECK(!w.ok);
  CHECK_EQ(std::get<std::string>(*st.get(Scope::World, "world", "owner", "owner", "secret")), std::string("x"));
}

LUAU_TEST("CAP-E04", "Luau storage: the per-mod quota stops abuse and is reported as a script error") {
  StorageApi st;
  st.set_quota("greedy", 2048);
  ReloadManager rm;
  rm.set_storage(&st);
  auto r = rm.reload("greedy", "local w = storage.world() for i = 1, 100 do w:set('k' .. i, string.rep('x', 100)) end");
  CHECK(!r.ok);
  CHECK(contains(r.error, "quota"));
  CHECK(st.usage("greedy") <= 2048);
  fx::load(rm, "polite", "storage.world():set('k', 'v')");
}

LUAU_TEST("CAP-E04", "Luau storage.migrate: stored rows are migrated step by step on read") {
  StorageApi st;
  ReloadManager rm;
  rm.set_storage(&st);
  fx::load(rm, "m", "storage.world():set('cfg', { gold = 5 })");   // stored at version 1
  CHECK_EQ(st.current_version("m"), std::uint32_t(1));
  fx::load(rm, "m", R"(
    storage.migrate(3, function(from, value)
      if from == 1 then value.gems = 0 return 2, value end
      if from == 2 then value.version_note = 'v3' return 3, value end
      error('unknown version ' .. from)
    end)
  )");
  CHECK_EQ(st.current_version("m"), std::uint32_t(3));
  st.check_loadable("m", 1);   // has a path
  st.check_loadable("m", 2);
  CHECK_THROWS(st.check_loadable("m", 4));  // downgrade
  auto row = st.get_row(Scope::World, "world", "m", "cfg");
  CHECK(row.has_value());
  CHECK_EQ(row->version, std::uint32_t(3));
  auto& t = std::get<std::shared_ptr<ValueTable>>(row->value);
  CHECK_EQ(std::get<std::int64_t>(t->entries.at("gold")), std::int64_t(5));
  CHECK_EQ(std::get<std::int64_t>(t->entries.at("gems")), std::int64_t(0));
  CHECK_EQ(std::get<std::string>(t->entries.at("version_note")), std::string("v3"));
}
