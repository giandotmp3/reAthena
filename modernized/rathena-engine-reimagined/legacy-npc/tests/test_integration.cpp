// legacy-npc against script-host's real API: the package loaded as a mod, dialogue through the DialogueManager, storage through the
// StorageApi with its permissions, timers through the ReloadManager, the generated command list.
#include <algorithm>
#include <cstdio>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "legacy_npc/commands.hpp"
#include "legacy_npc/package.hpp"
#include "legacy_npc/report.hpp"
#include "mini_test.hpp"

using namespace legacy_npc;
using namespace script_host;

void print_supported_commands() {
  auto& host = shared_host();
  host.clear_log();
  host.run_ok("names", "for _, n in LEGACY.command_names() do if string.sub(n, -7) ~= '_unused' then print('@name\\t' .. n) end end");
  std::printf("// GENERATED from the Luau command tables (luau/legacy.luau command_names(), names ending in _unused omitted). Regenerate:\n"
              "//   build/legacy_npc_tests --print-commands > src/supported_commands.inc     (a test fails when this file and the tables differ)\n");
  for (auto& r : host.records("@name")) std::printf("X(%s)\n", r[1].c_str());
}

namespace {
std::string slurp(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  std::ostringstream o;
  o << f.rdbuf();
  return o.str();
}

// a dialogue of the package: the NPC text goes into a runtime created inside the dialogue coroutine, using the host's storage and dialogue
std::string talk_source(const std::string& npc_file, const std::string& npc, int rid, const std::string& extra_cfg = "") {
  return "local rt = LEGACY.new{ " + extra_cfg + "host = { storage = storage, dialogue = { say = say, choose = choose, input = input, wait = wait }, log = function() end } }\n"
         "rt:addFile('t.txt', " + long_string(npc_file) + ")\n"
         "LEGACY.emit_result(rt:run('" + npc + "', " + std::to_string(rid) + "))\n";
}

std::string last_run_status(PackageHost& h) {
  std::string s;
  for (auto& r : h.records("@run")) s = r[1] + ":" + r[3];
  return s;
}
}  // namespace

ACCEPT_LUAU("LNPC-PKG-001", package_bundle_loads_as_a_mod_with_no_permissions) {
  PackageHost host;  // ModEnvironmentSpec.granted is empty
  CHECK(host.sandbox().spec().granted.empty());
  host.clear_log();
  host.run_ok("v", "print('@v\\t' .. LEGACY.version)");
  CHECK(host.records("@v").at(0)[1] == "0.1.0");
  // the bundle never touches what the sandbox removed (os, io, debug, require)
  CHECK(host.sandbox().memory_used() > 0);
  // mod.toml: id and no permissions
  auto toml = slurp(std::string(LEGACY_NPC_LUAU_DIR) + "/../mod.toml");
  CHECK(toml.find("id = \"legacy.npc\"") != std::string::npos);
  CHECK(toml.find("permissions = []") != std::string::npos);
}

ACCEPT_LUAU("LNPC-PKG-002", dialogue_runs_through_the_dialogue_manager_and_persists_variables_in_storage) {
  PackageHost host;
  std::string npc = "-\tscript\tGreeter\t-1,{\n visits += 1; $total += 1;\n mes \"Hello \" + visits; next;\n .@c = select(\"Yes:No\");\n mes \"you said \" + .@c; close;\n}\n";
  auto& d = host.dialogue();
  host.clear_log();
  auto s = d.start(42, kPackageId, talk_source(npc, "Greeter", 42));
  CHECK(!s.finished && std::holds_alternative<SayRequest>(s.pending));
  CHECK(std::get<SayRequest>(s.pending).text == "Hello 1");
  auto s2 = d.resume(42, s.token, {});
  CHECK(std::holds_alternative<ChooseRequest>(s2.pending));
  CHECK(std::get<ChooseRequest>(s2.pending).options == (std::vector<std::string>{"Yes", "No"}));
  DialogueResponse pick; pick.choice = 2;
  auto s3 = d.resume(42, s2.token, pick);
  CHECK(std::get<SayRequest>(s3.pending).text == "you said 2");
  auto s4 = d.resume(42, s3.token, {});
  CHECK(s4.finished && !s4.failed);
  CHECK(last_run_status(host) == "end:");
  // the variables live in script-host storage under the mod's namespace
  auto visits = host.storage().get(Scope::Character, "42", kPackageId, kPackageId, "legacy:char/visits");
  CHECK(visits && std::get<std::int64_t>(*visits) == 1);
  auto total = host.storage().get(Scope::World, "world", kPackageId, kPackageId, "legacy:world/total");
  CHECK(total && std::get<std::int64_t>(*total) == 1);
  // a second visit (new runtime, same storage) continues the count
  auto t = d.start(42, kPackageId, talk_source(npc, "Greeter", 42));
  CHECK(std::get<SayRequest>(t.pending).text == "Hello 2");
  d.on_disconnect(42);
}

ACCEPT_LUAU("LNPC-PKG-007", sandbox_host_wires_storage_timers_events_and_dialogue_from_the_mod_globals) {
  PackageHost host;
  std::string src =
      "local rt = LEGACY.new{ host = LEGACY.sandbox_host() }\n"
      "rt:addFile('t.txt', '-\\tscript\\tG\\t-1,{\\n total += 1; mes \"hi\"; close;\\n}\\n')\n"
      "LEGACY.emit_result(rt:run('G', 9))\n";
  auto s = host.dialogue().start(9, kPackageId, src);
  CHECK(std::holds_alternative<SayRequest>(s.pending) && std::get<SayRequest>(s.pending).text == "hi");
  host.dialogue().resume(9, s.token, {});
  auto v = host.storage().get(Scope::Character, "9", kPackageId, kPackageId, "legacy:char/total");
  CHECK(v && std::get<std::int64_t>(*v) == 1);
}

ACCEPT_LUAU("LNPC-PKG-003", disconnect_cancels_and_keeps_writes_made_before_the_blocking_command) {
  PackageHost host;
  std::string npc = "-\tscript\tC\t-1,{\n step = 1; mes \"x\"; next; step = 2; end;\n}\n";
  auto s = host.dialogue().start(7, kPackageId, talk_source(npc, "C", 7));
  CHECK(host.dialogue().has_dialogue(7));
  host.dialogue().on_disconnect(7);
  CHECK(!host.dialogue().has_dialogue(7));
  auto step = host.storage().get(Scope::Character, "7", kPackageId, kPackageId, "legacy:char/step");
  CHECK(step && std::get<std::int64_t>(*step) == 1);
  CHECK_THROWS(host.dialogue().resume(7, s.token, {}));
}

ACCEPT_LUAU("LNPC-PKG-004", sleep_waits_on_game_time_not_a_packet) {
  PackageHost host;
  std::string npc = "-\tscript\tS\t-1,{\n mes \"a\"; next; sleep 500; mes \"b\"; close;\n}\n";
  auto s = host.dialogue().start(1, kPackageId, talk_source(npc, "S", 1));
  auto s2 = host.dialogue().resume(1, s.token, {});
  CHECK(std::holds_alternative<WaitRequest>(s2.pending) && std::get<WaitRequest>(s2.pending).ms == 500);
  CHECK_THROWS(host.dialogue().resume(1, s2.token, {}));  // a packet cannot end a wait
  auto wakes = host.dialogue().advance_time(500);
  CHECK(wakes.size() == 1 && std::get<SayRequest>(wakes[0].state.pending).text == "b");
  host.dialogue().on_disconnect(1);
}

ACCEPT_LUAU("LNPC-PKG-005", account_variables_need_the_storage_account_permission) {
  std::string npc = "-\tscript\tA\t-1,{\n #acc = 5; ok = 1; end;\n}\n";
  {
    PackageHost host;  // nothing granted
    auto s = host.dialogue().start(1, kPackageId, talk_source(npc, "A", 1));
    CHECK(s.finished);
    CHECK(last_run_status(host) == "error:PERMISSION_DENIED");
  }
  {
    PackageOptions o;
    o.granted = {script_host::Permission::StorageAccount};
    PackageHost host(o);
    host.dialogue().start(1, kPackageId, talk_source(npc, "A", 1));
    CHECK(last_run_status(host) == "end:");
    auto v = host.storage().get(Scope::Account, "2000001", kPackageId, kPackageId, "legacy:account/acc");
    CHECK(v && std::get<std::int64_t>(*v) == 5);
  }
}

ACCEPT_LUAU("LNPC-PKG-006", timers_run_on_the_script_host_timer_ownership_model) {
  ReloadManager rm;
  StorageApi storage;
  ModEnvironmentSpec spec;
  spec.mod_id = kPackageId;
  spec.memory_cap_bytes = 256ull << 20;
  spec.budget.steps_per_dispatch = 500000000;
  spec.budget.wall_us_per_dispatch = 0;
  spec.budget.wall_us_per_tick = 0;
  rm.set_environment(spec);
  rm.set_storage(&storage);
  storage.set_quota(kPackageId, 64ull << 20);
  std::string npcs =
      "-\tscript\tTm\t-1,{\n initnpctimer; end;\nOnTimer1000:\n $o$ = $o$ + \"a\"; end;\nOnTimer3000:\n $o$ = $o$ + \"b\"; end;\n}\n";
  std::string boot = bundle_package(package_dir()) +
      "\nlocal rt = LEGACY.new{ host = { storage = storage, timers = timers, events = events, dialogue = LEGACY.blocking.scripted({}) } }\n"
      "rt:addFile('tm.txt', " + long_string(npcs) + ")\nLEGACY.rt = rt\ntimers.after(10, function() rt:run('Tm', 1) end)\n";
  auto r = rm.reload(kPackageId, boot);
  CHECK_MSG(r.ok, r.error);
  auto world = [&] {
    auto v = storage.get(Scope::World, "world", kPackageId, kPackageId, "legacy:world/o$");
    return v ? std::get<std::string>(*v) : std::string();
  };
  rm.advance_game_time(10);       // starts the NPC timer
  rm.advance_game_time(999);
  CHECK(world().empty());
  rm.advance_game_time(1);        // 1000 ms after the start
  CHECK(world() == "a");
  rm.advance_game_time(2000);
  CHECK(world() == "ab");
  // hot reload: timers are owned by (mod, generation) and die with the generation
  auto r2 = rm.reload(kPackageId, boot);
  CHECK_MSG(r2.ok, r2.error);
  rm.advance_game_time(10);
  rm.advance_game_time(500);
  rm.reload(kPackageId, "-- empty generation\n");
  rm.advance_game_time(10000);
  CHECK(world() == "ab");
}

ACCEPT_LUAU("LNPC-DEGRADE-004", unsupported_report_prints_file_npc_commands) {
  PackageHost host;
  std::string a = "-\tscript\tA1\t-1,{\n bg_warp 1, \"x\", 1, 1;\n}\n-\tscript\tA2\t-1,{\n mes \"ok\"; close;\n}\n";
  std::string b = "-\tscript\tB1\t-1,{\n bg_desert; bg_warp 1, \"y\", 2, 2;\n}\n";
  host.clear_log();
  host.run_ok("report",
      "local rt = LEGACY.new{}\nrt:addFile('a.txt', " + long_string(a) + ")\nrt:addFile('b.txt', " + long_string(b) + ")\n"
      "for line in string.gmatch(LEGACY.report.format(rt:report()), '[^\\n]+') do print('@r\\t' .. line) end");
  std::vector<std::string> lines;
  for (auto& r : host.records("@r")) lines.push_back(r[1] + "\t" + (r.size() > 2 ? r[2] : "") + "\t" + (r.size() > 3 ? r[3] : "") + "\t" + (r.size() > 4 ? r[4] : ""));
  CHECK(lines.size() >= 4);
  CHECK(lines[0].rfind("file\tline\tnpc\tunsupported commands", 0) == 0);
  CHECK(lines[1].rfind("a.txt\t1\tA1\tbg_warp", 0) == 0);
  CHECK(lines[2].rfind("b.txt\t1\tB1\tbg_desert,bg_warp", 0) == 0);
  CHECK(lines.back().rfind("# 3 NPCs, 2 disabled, 2 rows", 0) == 0);
}

// the generated list and the Luau tables must not drift apart
ACCEPT_LUAU("LNPC-DEGRADE-005", supported_commands_inc_matches_the_luau_tables) {
  auto& host = shared_host();
  host.clear_log();
  host.run_ok("names", "for _, n in LEGACY.command_names() do if string.sub(n, -7) ~= '_unused' then print('@name\\t' .. n) end end");
  std::vector<std::string> luau;
  for (auto& r : host.records("@name")) luau.push_back(r[1]);
  std::vector<std::string> inc;
  std::istringstream in(slurp(std::string(LEGACY_NPC_LUAU_DIR) + "/../src/supported_commands.inc"));
  for (std::string line; std::getline(in, line);)
    if (line.rfind("X(", 0) == 0) inc.push_back(line.substr(2, line.size() - 3));
  CHECK_MSG(luau == inc, "src/supported_commands.inc is stale: run legacy_npc_tests --print-commands");
}

// The renewal load set indexed inside a script-host sandbox (the limiting allocator's accounting, which is what the per-mod memory cap sees).
// Skipped when the repository's npc/ tree is not next to this unit.
namespace {
void collect_conf(const std::filesystem::path& root, const std::string& conf, std::vector<std::string>& out, int depth = 0) {
  if (depth > 32) throw std::runtime_error("import loop at " + conf);
  std::ifstream f(root / conf);
  for (std::string line; std::getline(f, line);) {
    auto c = line.find("//");
    if (c != std::string::npos) line.resize(c);
    auto a = line.find_first_not_of(" \t\r");
    if (a == std::string::npos) continue;
    auto colon = line.find(':', a);
    if (colon == std::string::npos) continue;
    std::string key = line.substr(a, colon - a), val = line.substr(colon + 1);
    while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val.erase(val.begin());
    while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\r')) val.pop_back();
    if (key == "npc") out.push_back(val);
    else if (key == "import") collect_conf(root, val, out, depth + 1);
  }
}
}  // namespace

ACCEPT_LUAU("LNPC-LAZY-010", renewal_set_indexed_in_a_sandbox_stays_small) {
  std::filesystem::path root = std::filesystem::path(LEGACY_NPC_LUAU_DIR) / ".." / ".." / ".." / "..";
  if (!std::filesystem::exists(root / "npc" / "re" / "scripts_main.conf")) throw mt::Skip("repository npc/ tree not found next to the unit");
  std::vector<std::string> files;
  collect_conf(root, "npc/re/scripts_main.conf", files);
  PackageOptions o;
  o.memory_cap_bytes = 512ull << 20;
  PackageHost host(o);
  host.run_ok("rt", "LEGACY_RT = LEGACY.new{}");
  auto before = host.sandbox().memory_used();
  std::size_t loaded = 0, text_bytes = 0;
  for (auto& f : files) {
    std::ifstream in(root / f, std::ios::binary);
    if (!in) continue;
    std::ostringstream buf;
    buf << in.rdbuf();
    std::string text = buf.str();
    text_bytes += text.size();
    host.run_ok("add", "LEGACY_RT:addFile(" + long_string(f) + ", " + long_string(text) + ")");
    ++loaded;
  }
  auto after = host.sandbox().memory_used();
  host.clear_log();
  host.run_ok("count", "print('@n\t' .. #LEGACY_RT.loader.npcList .. '\t' .. LEGACY_RT.loader.stats.parsed .. '\t' .. LEGACY_RT.loader.stats.functions)");
  auto n = host.records("@n").at(0);
  double mb = static_cast<double>(after - before) / (1 << 20);
  std::printf("      [renewal set in a sandbox: %zu files, %.1f MB text, %s NPC entries, %s parsed, %s functions; sandbox memory +%.1f MB]\n",
              loaded, static_cast<double>(text_bytes) / (1 << 20), n[1].c_str(), n[2].c_str(), n[3].c_str(), mb);
  CHECK(loaded > 800);
  CHECK(std::stoul(n[1]) > 19000);
  CHECK(n[2] == "0");  // nothing parsed by indexing
  CHECK_MSG(mb < 160.0, "sandbox memory grew by " + std::to_string(mb) + " MB (text retained + index)");
}

// Hooks for every On* label of the renewal set (the engine loads the corpus and registers during the mod's load chunk).
ACCEPT_LUAU("LNPC-EVT-013", renewal_set_registers_its_event_hooks_on_the_real_bus) {
  std::filesystem::path root = std::filesystem::path(LEGACY_NPC_LUAU_DIR) / ".." / ".." / ".." / "..";
  if (!std::filesystem::exists(root / "npc" / "re" / "scripts_main.conf")) throw mt::Skip("repository npc/ tree not found next to the unit");
  std::vector<std::string> files;
  collect_conf(root, "npc/re/scripts_main.conf", files);
  script_host::StorageApi storage;
  script_host::ReloadManager rm;
  script_host::ModEnvironmentSpec spec;
  spec.mod_id = kPackageId;
  spec.memory_cap_bytes = 1024ull << 20;
  spec.budget.steps_per_dispatch = 4000000000ull;
  spec.budget.wall_us_per_dispatch = 0;
  spec.budget.wall_us_per_tick = 0;
  rm.set_environment(spec);
  rm.set_storage(&storage);
  std::string boot = bundle_package(package_dir()) + "\nlocal rt = LEGACY.new{ host = { storage = storage, timers = timers, events = events } }\n";
  for (auto& f : files) {
    std::ifstream in(root / f, std::ios::binary);
    if (!in) continue;
    std::ostringstream buf;
    buf << in.rdbuf();
    boot += "rt:addFile(" + long_string(f) + ", " + long_string(buf.str()) + ")\n";
  }
  boot += "LEGACY.hooks = rt:registerEvents()\nLEGACY.rt = rt\n";
  auto t0 = std::chrono::steady_clock::now();
  auto r = rm.reload(kPackageId, boot);
  double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  CHECK_MSG(r.ok, r.error);
  std::printf("      [renewal set: %zu hooks on the bus, load + registration + bus validation %.1f s]\n", rm.bus().size(), secs);
  CHECK(rm.bus().size() > 3000);
  CHECK(rm.bus().order("legacy:OnInit").size() > 500);
  CHECK(!rm.bus().order("legacy:OnTouch").empty());
  CHECK(!rm.bus().order("legacy:OnTimer1000").empty());
}

// ---- known gaps, kept as expected failures with the reason (they fail for real: the assertion is the behaviour, not a placeholder) ----------------------

// npc.cpp npc_timerevent_quit (:1594-1650): when a player with an NPC timer attached logs out, the NPC's OnTimerQuit label runs once for that player.
ACCEPT_LUAU("LNPC-TIMER-005", on_timer_quit_runs_when_the_attached_player_logs_out) {
  auto& host = shared_host();
  host.clear_log();
  host.run_ok("quit", R"(
    local rt = LEGACY.new{}
    rt:addFile("t.txt", "-\tscript\tTm\t-1,{\n end;\nOnTimer5000:\n end;\nOnTimerQuit:\n $quit = $quit + 1; end;\n}\n")
    local p = rt:player(1, {})
    rt:timerStart(rt.loader.npcs.Tm, p)
    rt:dropPlayer(1)
    print("@quit\t" .. tostring(rt.backend.storage.world():get("legacy:world/quit")))
  )");
  CHECK(host.records("@quit").at(0)[1] == "1");
}

// the Luau flat ops are cached across restarts like the C++ ParsedFile (architecture 7: "parse once, cache as binary, invalidate by file hash")
ACCEPT_LUAU("LNPC-PARSE-007", flat_op_cache_roundtrip_skips_the_parse) {
  auto& host = shared_host();
  host.clear_log();
  host.run_ok("cache", R"(
    local L = LEGACY.loader.new{}
    L:addFile("t.txt", "-\tscript\tn\t-1,{\n mes \"x\"; close;\n}\n")
    local prog = L:program(L.npcs.n)
    local blob = LEGACY.cache.encode(prog, 1234)
    local back = LEGACY.cache.decode(blob, 1234)
    print("@cache\t" .. tostring(#back.code == #prog.code) .. "\t" .. tostring(LEGACY.cache.decode(blob, 1) == nil))
  )");
  auto r = host.records("@cache").at(0);
  CHECK(r[1] == "true" && r[2] == "true");
}

// instance variables (') exist while the script runs inside an instance (env.instanceId set by the instance layer) and are refused outside
ACCEPT_LUAU("LNPC-VAR-018", instance_variables_are_writable_inside_an_instance_only) {
  auto& host = shared_host();
  host.clear_log();
  host.run_ok("inst", R"(
    local rt = LEGACY.new{}
    rt:addFile("t.txt", "-\tscript\tn\t-1,{\n 'a = 5; $r = 'a; end;\n}\n")
    rt:runLabel(rt.loader.npcs.n, nil, nil)
    local outside = tostring(rt.backend.storage.world():get("legacy:world/r"))
    rt.env.instanceId = 7
    rt:runLabel(rt.loader.npcs.n, nil, nil)
    print("@inst\t" .. outside .. "\t" .. tostring(rt.backend.storage.world():get("legacy:world/r")))
  )");
  auto r = host.records("@inst").at(0);
  CHECK(r[1] == "nil" && r[2] == "5");
}
