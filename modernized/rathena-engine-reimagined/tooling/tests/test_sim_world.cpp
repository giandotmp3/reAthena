// The sim world: mods from a mods directory through the real loader, virtual time, seeded RNG streams,
// persistence, hooks on engine-core movement. Tags: TL-SIM-06x (tooling-local), BR-RULESET-115/116.
#include <chrono>

#include "sim_fixture.hpp"
#include "tooling/sim_cli.hpp"

using namespace tooling::sim;
namespace fs = std::filesystem;
using simtest::TempDir;

namespace {

int64_t world_int(Sim& s, const std::string& mod, const std::string& key) {
  auto v = s.storage().get(script_host::Scope::World, "world", mod, mod, key);
  return v ? std::get<int64_t>(*v) : 0;
}

}  // namespace

TEST("TL-SIM-060", the_chosen_mods_load_through_the_real_loader_one_sandboxed_state_each) {
  auto sim = simtest::make_sim();  // rathena.base + ruleset.prerenewal + legacy.npc, found under the engine tree
  const auto& order = sim->load_order();
  CHECK(order.size() == 3);
  auto pos = [&](const std::string& id) { return std::find(order.begin(), order.end(), id) - order.begin(); };
  CHECK(pos("rathena.base") < pos("ruleset.prerenewal"));  // dependency order from the resolver
  CHECK(sim->ruleset_id() == "ruleset.prerenewal");
  for (auto& id : order) {
    CHECK(sim->loader().state(id) == mods::ModState::Active);
    CHECK(sim->generation(id) == 1);
    CHECK(sim->host().sandbox(id) != nullptr);
    CHECK(sim->host().stats(id).memory_used > 0);
  }
  // one lua_State per mod: a global of one is invisible to the others
  sim->eval("rathena.base", "MARK = 1");
  sim->eval("legacy.npc", "assert(MARK == nil and SIM_MOD == 'legacy.npc')");
  sim->eval("ruleset.prerenewal", "assert(MARK == nil and SIM_MOD == 'ruleset.prerenewal')");
  // the two rulesets exclude each other (the resolver says so, naming both)
  try {
    simtest::make_sim({"rathena.base", "ruleset.prerenewal", "ruleset.renewal"});
    CHECK(false);
  } catch (const SimError& e) {
    std::string w = e.what();
    CHECK(w.find("ruleset.prerenewal") != std::string::npos && w.find("ruleset.renewal") != std::string::npos);
  }
  // an unknown mod is named
  try {
    simtest::make_sim({"no.such.mod"});
    CHECK(false);
  } catch (const SimError& e) {
    CHECK(std::string(e.what()).find("no.such.mod") != std::string::npos);
  }
}

TEST("TL-SIM-061", mods_come_from_a_mods_directory_with_dependencies_and_grants_applied) {
  simtest::need_luau();
  simtest::TempDir d("mods");
  simtest::write_mod(d, "tt.lib", "", "LIBLOADED = true\n");
  simtest::write_mod(d, "tt.user", "[depends]\n\"tt.lib\" = \"^1\"\n", "USERLOADED = true\n");
  simtest::write_mod(d, "tt.bank", "permissions = [\"storage.account\"]\n",
            "local a = storage.account(1)\na:set('balance', 5)\nGRANTED = true\n");
  {
    SimConfig c;
    c.mods = {"tt.user"};
    c.mod_dirs = {d.path};
    Sim sim(c);  // the dependency tt.lib is added by the sim, ordered before tt.user by the loader
    CHECK(sim.load_order().size() == 2 && sim.load_order()[0] == "tt.lib" && sim.load_order()[1] == "tt.user");
  }
  {
    SimConfig c;
    c.mods = {"tt.bank"};
    c.mod_dirs = {d.path};
    try {  // storage.account is requested but not granted in mods.lock: the mod cannot load
      Sim sim(c);
      CHECK(false);
    } catch (const SimError& e) {
      CHECK(std::string(e.what()).find("storage.account") != std::string::npos);
    }
    c.grants["tt.bank"] = {"storage.account"};
    Sim sim(c);
    auto v = sim.storage().get(script_host::Scope::Account, "1", "tt.bank", "tt.bank", "balance");
    CHECK(v && std::get<int64_t>(*v) == 5);
  }
}

TEST("TL-SIM-062", virtual_time_moves_only_when_advanced_and_engine_and_script_timers_move_together) {
  auto sim = simtest::make_sim({"sim.status.tick"});
  const auto wall0 = std::chrono::steady_clock::now();
  CHECK(sim->now_ms() == 0);
  CHECK(world_int(*sim, "sim.status.tick", "ticks") == 0);  // nothing runs without a step
  sim->run_for(999);
  CHECK(sim->now_ms() == 999 && world_int(*sim, "sim.status.tick", "ticks") == 0);
  sim->step();                                              // 20 ms tick: now 1019, the 1000 ms timer fired
  CHECK(sim->now_ms() == 1019 && world_int(*sim, "sim.status.tick", "ticks") == 1);
  // a walk and a script timer share the clock
  auto& p = simtest::add(*sim, "w", 100, 100);
  const int64_t t0 = sim->now_ms();
  p.walk(110, 100);
  p.expect_none("never", 0);
  CHECK(p.wait_for("stop", 3000).has_value());
  CHECK(sim->now_ms() - t0 >= 1500);
  CHECK(world_int(*sim, "sim.status.tick", "ticks") >= 2);
  // an hour of virtual time costs a small fraction of that in wall time
  sim->run_for(3'600'000);
  const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count();
  CHECK(world_int(*sim, "sim.status.tick", "ticks") >= 3600);
  CHECK(wall < 30.0);
}

TEST("BR-RULESET-115", same_seed_gives_an_identical_event_and_rng_trace_different_seeds_do_not) {
  simtest::need_luau();
  auto run = [](uint64_t seed) {
    return run_load(24, 6000, seed, {"rathena.base", "sim.hook.rng.a", "sim.hook.rng.b"});
  };
  LoadReport a = run(11), b = run(11), c = run(12);
  CHECK(a.error.empty() && b.error.empty() && c.error.empty());
  CHECK(a.trace_hash == b.trace_hash && a.trace_events == b.trace_events);        // every wire byte in and out, in order
  CHECK(a.frames_received == b.frames_received && a.bytes_received == b.bytes_received);
  CHECK(a.native_rng_draws == b.native_rng_draws && a.walks == b.walks && a.chats == b.chats);
  CHECK(a.trace_hash != c.trace_hash);                                              // the seed is what drives the run
  CHECK(a.native_rng_draws > 0);
}

TEST("BR-RULESET-115", script_rng_streams_are_per_hook_seeded_and_independent_of_other_hooks) {
  auto roll_a = [](std::vector<std::string> mods, uint64_t seed) {
    auto sim = simtest::make_sim(std::move(mods), seed);
    auto r1 = sim->dispatch("sim:roll", 0);
    auto r2 = sim->dispatch("sim:roll", 0);
    std::vector<std::string> a;
    for (auto* r : {&r1, &r2})
      for (auto& e : r->effects)
        if (e.kind == "rng.a:value") a.push_back(e.payload_json);
    CHECK(sim->rng_report().main_draws == 0);  // hooks never touch the main stream
    return a;
  };
  auto alone = roll_a({"sim.hook.rng.a"}, 5);
  auto with_b = roll_a({"sim.hook.rng.a", "sim.hook.rng.b"}, 5);
  CHECK(alone.size() == 2 && alone == with_b);        // adding another hook does not shift hook a's numbers
  CHECK(roll_a({"sim.hook.rng.a"}, 5) == alone);      // reproducible
  CHECK(roll_a({"sim.hook.rng.a"}, 6) != alone);      // seeded
  CHECK(alone[0] != alone[1]);                        // and it advances
}

TEST("BR-RULESET-115", strict_parity_rejects_a_hook_that_draws_from_the_main_stream) {
  auto sim = simtest::make_sim({"sim.hook.rng.a", "sim.hook.main"});
  auto r = sim->dispatch("sim:roll", 0);
  // the cheating hook fails (and is rolled back); the honest hook still ran and queued its effect
  CHECK(r.errors.size() == 1 && r.errors[0].find("sim.hook.main/cheat") != std::string::npos &&
        r.errors[0].find("strict_parity") != std::string::npos);
  CHECK(r.effects.size() == 1 && r.effects[0].kind == "rng.a:value");
  CHECK(sim->rng_report().main_draws == 0);
  // with strict parity off the same hook draws, and the draw is counted
  auto lax = simtest::make_sim({"sim.hook.main"}, 1, [](SimConfig& c) { c.strict_parity = false; });
  auto r2 = lax->dispatch("sim:roll", 0);
  CHECK(r2.errors.empty() && r2.effects.size() == 1);
  CHECK(lax->rng_report().main_draws == 1);
}

TEST("BR-RULESET-116", effects_are_queued_by_hooks_and_discarded_when_a_pre_hook_cancels) {
  auto sim = simtest::make_sim({"sim.hook.alpha", "sim.hook.beta", "sim.hook.gamma"});
  auto ok = sim->dispatch("sim:stage", 100);
  CHECK(!ok.cancelled && ok.value == 101 && ok.effects.size() == 3);
  CHECK(ok.effects[0].kind == "beta:pre" && ok.effects[1].kind == "alpha:fx" && ok.effects[2].kind == "gamma:post");
  auto no = sim->dispatch("sim:stage", -1);
  CHECK(no.cancelled && no.cancelled_by == "sim.hook.beta/guard" && no.effects.empty() && no.ran.size() == 1 && no.value == -1);
}

TEST("TL-SIM-063", sqlite_is_the_default_backend_and_a_file_database_survives_a_new_sim) {
  simtest::need_luau();
  simtest::TempDir d("db");
  const std::string db = (d.path / "world.sqlite").string();
  auto cfgf = [&](bool sqlite) {
    return [&, sqlite](SimConfig& c) {
      c.sqlite = sqlite;
      c.sqlite_path = db;
      c.map_w = c.map_h = 200;
    };
  };
  uint32_t aid = 0;
  {
    auto sim = simtest::make_sim({"rathena.base"}, 1, cfgf(true));
    CHECK(sim->storage_backend_name() == "sqlite");
    Sim::PlayerSpec ps;
    ps.name = "neo";
    ps.x = 60; ps.y = 60;
    auto& p = sim->add_player(ps);
    aid = p.aid();
    p.walk(70, 60);
    CHECK(p.wait_for("stop", 3000).has_value());
    p.disconnect();  // writes the position through script-host's StorageApi into SQLite
  }
  {
    // a brand-new sim (a "restart") on the same database file sees the saved character
    auto sim = simtest::make_sim({"rathena.base"}, 1, cfgf(true));
    Sim::PlayerSpec ps;
    ps.name = "neo";
    ps.aid = aid;
    ps.x = 5; ps.y = 5;
    auto& p = sim->add_player(ps);
    CHECK(p.position()->x == 70 && p.position()->y == 60);
  }
  {
    // the in-memory script-host backend has no such memory
    auto sim = simtest::make_sim({"rathena.base"}, 1, cfgf(false));
    CHECK(sim->storage_backend_name() == "memory");
    Sim::PlayerSpec ps;
    ps.name = "neo";
    ps.aid = aid;
    ps.x = 5; ps.y = 5;
    CHECK(sim->add_player(ps).position()->x == 5);
  }
}

TEST("TL-SIM-064", a_mod_hook_on_the_movement_events_can_veto_a_walk_and_the_kill_switch_lifts_it) {
  simtest::need_luau();
  simtest::TempDir d("veto");
  simtest::write_mod(d, "tt.wall", "",
            "events.on('unit:before_move', { id = 'wall', phase = 'pre', mode = 'observe' }, function(ctx) ctx:cancel('wall') end)\n"
            "events.on('unit:moved', { id = 'count', phase = 'on', mode = 'observe' }, function(ctx)\n"
            "  local w = storage.world(); w:set('moved', (w:get('moved') or 0) + 1) end)\n");
  SimConfig c;
  c.mods = {"rathena.base", "tt.wall"};
  c.mod_dirs = {d.path};
  c.map_w = c.map_h = 200;
  Sim sim(c);
  auto& p = simtest::add(sim, "walker", 100, 100);
  p.walk(105, 100);
  sim.run_for(1500);
  CHECK(p.position()->x == 100);                // engine-core stopped the walk: the on-move hook vetoed the first step
  CHECK(world_int(sim, "tt.wall", "moved") == 0);
  CHECK(p.wait_for("stop", 200).has_value());  // the vetoed walk ended where it stood (ZC_STOPMOVE)
  sim.disable_mod("tt.wall");                   // operator kill switch
  p.drain_received();
  p.walk(105, 100);
  CHECK(p.wait_for("stop", 3000).has_value());
  CHECK(p.position()->x == 105);
}

TEST("TL-SIM-065", two_hundred_players_walk_and_chat_and_the_wall_time_per_virtual_second_is_reported) {
  simtest::need_luau();
  LoadReport r = run_load(200, 10000, 1);
  CHECK(r.error.empty());
  CHECK(r.players == 200 && r.virtual_s == 10.0);
  CHECK(r.walks > 200 && r.frames_received > 10000);
  std::printf("      200 players: %.3f wall s per virtual s (%.2f wall s for %.0f virtual s, setup %.2f s), %llu walks, %llu chats, %llu frames / %llu bytes delivered\n",
              r.wall_per_virtual_s, r.wall_s, r.virtual_s, r.setup_wall_s, static_cast<unsigned long long>(r.walks),
              static_cast<unsigned long long>(r.chats), static_cast<unsigned long long>(r.frames_received),
              static_cast<unsigned long long>(r.bytes_received));
  CHECK(r.wall_per_virtual_s < 2.0);  // generous: the machine and build type vary; the number above is the measurement
}

TEST("TL-SIM-066", the_sim_stages_into_a_directory_of_its_own_and_never_deletes_the_one_it_was_given) {
  simtest::need_luau();
  simtest::TempDir d("workdir");
  d.write("keep/me.txt", "precious");
  fs::path staged;
  {
    SimConfig c;
    c.mods = {"rathena.base"};
    c.work_dir = d.path / "keep";
    Sim sim(c);
    staged = sim.work_dir();
    CHECK(staged.parent_path() == d.path / "keep");
    CHECK(fs::exists(staged / "stage" / "rathena.base" / "scripts"));
    // real files, not links into the package (the bundler resolves symlinks when naming modules)
    CHECK(!fs::is_symlink(staged / "stage" / "rathena.base" / "scripts"));
  }
  CHECK(!fs::exists(staged));                                  // own directory: removed
  CHECK(fs::exists(d.path / "keep" / "me.txt"));               // the caller's directory: untouched
  // a constructor that fails leaves nothing behind either
  SimConfig bad;
  bad.mods = {"no.such.mod"};
  bad.work_dir = d.path / "keep";
  CHECK_THROWS(Sim(bad));
  size_t entries = 0;
  for (auto& e : fs::directory_iterator(d.path / "keep")) { (void)e; ++entries; }
  CHECK(entries == 1);
}
