#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "rathena/persist/script_host_adapter.hpp"
#include "rathena/persist/service.hpp"
#include "rathena/persist/sqlite_connection.hpp"
#include "generated/catalog_client_packets.hpp"
#include "tooling/sim_player.hpp"
#include "tooling/sim_world.hpp"

#ifndef TOOLING_SOURCE_DIR
#error "TOOLING_SOURCE_DIR must be defined for the tooling library"
#endif

namespace tooling::sim {

namespace fs = std::filesystem;

struct Sim::PersistImpl {
  std::unique_ptr<rathena::persist::SqliteConnection> conn;
  std::unique_ptr<rathena::persist::Quarantine> quarantine;
  std::unique_ptr<rathena::persist::KvStore> kv;
};

namespace {

std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) throw SimError("cannot read " + p.string());
  std::ostringstream o;
  o << in.rdbuf();
  return o.str();
}
void spit(const fs::path& p, const std::string& s) {
  fs::create_directories(p.parent_path());
  std::ofstream out(p, std::ios::binary);
  if (!out) throw SimError("cannot write " + p.string());
  out << s;
}

std::string lua_quote(const std::string& s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += static_cast<char>(c); }
    else if (c == '\n') o += "\\n";
    else if (c < 0x20 || c == 0x7F) { char b[8]; std::snprintf(b, sizeof b, "\\%03d", c); o += b; }
    else o += static_cast<char>(c);
  }
  return o + "\"";
}

// A Luau long string that holds `s` exactly (the first newline after the opening bracket is skipped).
std::string lua_long(const std::string& s) {
  for (size_t n = 0;; ++n) {
    std::string close = "]" + std::string(n, '=') + "]";
    if (s.find(close) == std::string::npos) return "[" + std::string(n, '=') + "[\n" + s + close;
  }
}

std::string shell_quote(const std::string& s) {
  std::string o = "'";
  for (char c : s) {
    if (c == '\'') o += "'\\''";
    else o += c;
  }
  return o + "'";
}

bool starts_with(const std::string& s, const std::string& p) { return s.rfind(p, 0) == 0; }

const char* kSkipDirs[] = {"build", ".git", "node_modules", "__pycache__", "3rdparty", "out", "tests", "test", "m0", "docs", "tools"};

void discover(const fs::path& root, int depth, std::map<std::string, fs::path>& out, std::vector<std::string>& warnings) {
  std::error_code ec;
  if (!fs::is_directory(root, ec)) return;
  if (fs::exists(root / "mod.toml", ec)) {
    try {
      mods::Manifest m = mods::load_manifest_file(root / "mod.toml");
      auto [it, inserted] = out.emplace(m.id, root);
      if (!inserted && it->second != root) warnings.push_back("mod '" + m.id + "' found twice: " + it->second.string() + " and " + root.string());
    } catch (const std::exception& e) {
      warnings.push_back(root.string() + ": " + e.what());
    }
    return;
  }
  if (depth <= 0) return;
  std::vector<fs::path> kids;
  for (auto& e : fs::directory_iterator(root, ec)) {
    if (!e.is_directory(ec)) continue;
    const std::string n = e.path().filename().string();
    if (n.empty() || n[0] == '.') continue;
    if (std::any_of(std::begin(kSkipDirs), std::end(kSkipDirs), [&](const char* s) { return n == s; })) continue;
    kids.push_back(e.path());
  }
  std::sort(kids.begin(), kids.end());
  for (auto& k : kids) discover(k, depth - 1, out, warnings);
}

// The mods bundler names modules with fs::relative(), which resolves symlinks: a staged package must be
// real files. Directories are copied (a few MB for the largest package; the work dir is removed on exit).
void copy_tree(const fs::path& src, const fs::path& dst) {
  fs::create_directories(dst.parent_path());
  fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
}

std::string category_of(const std::string& line) {
  auto p = line.find("cat=\"");
  if (p == std::string::npos) return {};
  auto q = line.find('"', p + 5);
  return q == std::string::npos ? std::string() : line.substr(p + 5, q - p - 5);
}

// Writes a golden module holding the header, a few cases per category and every worked example.
void write_golden_subset(const fs::path& full, const fs::path& out, size_t per_cat) {
  std::ifstream in(full, std::ios::binary);
  if (!in) throw SimError("cannot read " + full.string());
  std::string header, line, body;
  std::map<std::string, size_t> taken;
  bool in_data = false;
  while (std::getline(in, line)) {
    if (!in_data) {
      header += line + "\n";
      if (line.rfind("return {", 0) == 0) in_data = true;
      continue;
    }
    if (line == "}") break;
    std::string cat = category_of(line);
    if (cat == "worked_example" || taken[cat]++ < per_cat) body += line + "\n";
  }
  spit(out, header + body + "}\n");
}

fs::path expand_golden_with_python(const fs::path& pkg_dir, const fs::path& cache_dir) {
  const fs::path script = fs::path(TOOLING_SOURCE_DIR) / "sim" / "expand_golden.py";
  std::string cmd = "python3 -I " + shell_quote(script.string()) + " " + shell_quote(pkg_dir.string()) + " " + shell_quote(cache_dir.string()) + " 1>&2";
  if (std::system(cmd.c_str()) != 0) throw SimError("could not expand the golden data of " + pkg_dir.string() + " (" + cmd + ")");
  return cache_dir;
}

uint64_t fnv(uint64_t h, const std::string& s) {
  for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
  h ^= 0xff; h *= 1099511628211ull;
  return h;
}

std::atomic<int> g_work_counter{0};

}  // namespace

// -------------------------------------------------------------------------------------------------
Sim::Sim(SimConfig cfg) : cfg_(std::move(cfg)), rng_(cfg_.seed) {
  if (cfg_.engine_root.empty()) cfg_.engine_root = fs::path(TOOLING_SOURCE_DIR) / "..";
  cfg_.engine_root = fs::weakly_canonical(cfg_.engine_root);
  if (cfg_.mods.empty()) cfg_.mods = {"rathena.base", "ruleset.prerenewal", "legacy.npc"};
  // Every Sim stages its packages into a directory of its own, created fresh under SimConfig::work_dir
  // (default: the system temp dir) and removed on exit. A directory the caller names is never deleted itself.
  {
    const fs::path base = cfg_.work_dir.empty() ? fs::temp_directory_path() : cfg_.work_dir;
    work_dir_ = base / ("rathena-sim-" + std::to_string(::getpid()) + "-" + std::to_string(g_work_counter++));
    own_work_dir_ = true;
    std::error_code ec;
    fs::remove_all(work_dir_, ec);
    fs::create_directories(work_dir_);
  }
  streams_ = std::make_unique<script_host::RngStreams>(cfg_.seed);
  streams_->set_strict_parity(cfg_.strict_parity);
  timers_ = std::make_unique<engine::TimerQueue>(clock_);

  if (!mods::HostRuntime::vm_available()) throw SimError("this build has no Luau VM (set RATHENA_LUAU_ROOT / RATHENA_LUAU_LIB_DIR)");
  struct Cleanup {  // a constructor that throws must not leave its staging directory behind
    Sim& self;
    bool done = false;
    ~Cleanup() {
      if (!done && self.own_work_dir_) {
        std::error_code ec;
        fs::remove_all(self.work_dir_, ec);
      }
    }
  } cleanup{*this};
  build_persistence();
  build_mods();
  build_world();
  cleanup.done = true;
  trace("sim seed=" + std::to_string(cfg_.seed) + " mods=" + [&] { std::string s; for (auto& m : order_) s += (s.empty() ? "" : ",") + m; return s; }());
}

Sim::~Sim() {
  // Players hold references into the world: destroy them first. The dialogue manager and the
  // host runtime go before the persistence layer they write into.
  players_.clear();
  dialogues_.reset();
  movement_.reset();
  loader_.reset();
  host_.reset();
  backend_.reset();
  persist_.reset();
  if (own_work_dir_) {
    std::error_code ec;
    fs::remove_all(work_dir_, ec);
  }
}

void Sim::build_persistence() {
  if (!cfg_.sqlite) {
    backend_ = std::make_shared<script_host::MemoryStorageBackend>();
    return;
  }
  persist_ = std::make_unique<PersistImpl>();
  persist_->conn = std::make_unique<rathena::persist::SqliteConnection>(cfg_.sqlite_path);
  persist_->conn->connect();
  persist_->quarantine = std::make_unique<rathena::persist::Quarantine>(*persist_->conn);
  rathena::persist::KvPolicy pol;
  pol.mod_quota_bytes = 8ull << 20;
  persist_->kv = std::make_unique<rathena::persist::KvStore>(*persist_->conn, *persist_->quarantine, pol);
  backend_ = std::make_shared<rathena::persist::KvStorageBackend>(*persist_->kv);
}

void Sim::stage_all() {
  const fs::path stage_root = work_dir_ / "stage";
  for (const auto& [id, real] : real_dir_) {
    const mods::Manifest& m = manifests_.at(id);
    const bool ruleset = std::find(m.provides.begin(), m.provides.end(), "ruleset") != m.provides.end();
    const fs::path st = stage_root / id;
    stage_dir_[id] = st;
    fs::create_directories(st);
    std::vector<std::string> module_dirs;
    for (const char* d : {"scripts", "data", "luau"}) {
      if (fs::is_directory(real / d)) {
        copy_tree(real / d, st / d);
        module_dirs.push_back(d);
      }
    }
    std::set<std::string> exclude{"tests", "test", "build", "conformance", "tools", "types"};
    if (ruleset) {
      if (cfg_.stage_package_tests) {
        for (const char* d : {"tests", "conformance"})
          if (fs::is_directory(real / d)) { copy_tree(real / d, st / d); module_dirs.push_back(d); }
        exclude.clear();
      } else if (cfg_.stage_test_support && fs::is_directory(real / "tests" / "support")) {
        fs::create_directories(st / "tests");
        copy_tree(real / "tests" / "support", st / "tests" / "support");
        module_dirs.push_back("tests/support");
        exclude.clear();
      }
      if (cfg_.golden != GoldenStage::None) {
        // golden_weapon_<pre|re>.luau (and the defense vectors the package suite reads)
        const fs::path bdir = real / "build";
        std::vector<std::string> wanted = {"golden_weapon_pre.luau", "golden_weapon_re.luau"};
        if (cfg_.stage_package_tests) wanted.push_back("golden_defense_vectors.luau");
        bool any = false;
        for (const auto& fn : wanted) {
          fs::path src = bdir / fn;
          const std::string stem = fn.substr(0, fn.size() - 5);
          // does this package own a golden source of that name?
          const bool weapon = starts_with(fn, "golden_weapon_");
          if (weapon) {
            const std::string suffix = stem.substr(std::string("golden_weapon_").size());
            if (!fs::exists(real / "conformance" / "golden" / ("weapon_" + suffix + ".jsonl.gz"))) continue;
          } else if (!fs::is_directory(real / "conformance" / "golden" / "mapvectors")) {
            continue;
          }
          if (!fs::exists(src)) {
            fs::path cache = work_dir_ / "golden-cache" / id;
            if (!fs::exists(cache / fn)) expand_golden_with_python(real, cache);
            src = cache / fn;
          }
          if (!fs::exists(src)) continue;
          fs::create_directories(st / "build");
          if (weapon && cfg_.golden == GoldenStage::Subset) write_golden_subset(src, st / "build" / fn, cfg_.golden_subset_per_category);
          else fs::copy_file(src, st / "build" / fn, fs::copy_options::overwrite_existing);
          any = true;
        }
        if (any) module_dirs.push_back("build");
        exclude.clear();
      }
    } else {
      exclude = {"tests", "test", "build", "conformance", "tools", "types"};
    }
    // the sim's export module: gives the bundled package's require/import to eval() and, for legacy.npc,
    // the LEGACY global that the mods bundler does not define
    std::string exp = "-- generated by rathena-sim\nSIM_REQUIRE = require\nSIM_IMPORT = import\nSIM_MOD = " + lua_quote(id) + "\n";
    if (fs::exists(real / "luau" / "legacy.luau")) exp += "LEGACY = require(\"luau/legacy\")\n";
    spit(st / "sim" / "export.luau", exp);
    module_dirs.push_back("sim");
    if (auto sf = cfg_.stage_files.find(id); sf != cfg_.stage_files.end())
      for (auto& [rel, text] : sf->second) spit(st / rel, text);

    mods::BundleOptions bo;
    bo.module_dirs = module_dirs;
    bo.exclude_dirs = exclude;
    if (!m.entry.empty()) {
      bo.entry = m.entry;
      bo.entry.push_back("sim/export");
    } else if (fs::exists(st / "scripts" / "init.luau")) {
      bo.entry = {"scripts/init", "sim/export"};
    } else if (fs::exists(real / "luau" / "legacy.luau")) {
      bo.entry = {"luau/legacy", "sim/export"};
    }  // else: every module, which includes sim/export
    if (auto ex = cfg_.extra_entries.find(id); ex != cfg_.extra_entries.end()) {
      if (bo.entry.empty()) throw SimError("extra entries for '" + id + "' need an explicit entry list");
      for (auto& e : ex->second) bo.entry.push_back(e);
    }
    bundle_opts_[id] = std::move(bo);
  }
}

void Sim::build_mods() {
  std::map<std::string, fs::path> found;
  std::vector<std::string> warnings;
  discover(cfg_.engine_root, 4, found, warnings);
  for (auto& d : cfg_.mod_dirs) discover(d, 5, found, warnings);
  // chosen mods and the closure of their dependencies (a missing dependency is left to the resolver)
  std::vector<std::string> todo = cfg_.mods;
  while (!todo.empty()) {
    std::string id = todo.back();
    todo.pop_back();
    if (manifests_.count(id)) continue;
    auto it = found.find(id);
    if (it == found.end()) {
      if (std::find(cfg_.mods.begin(), cfg_.mods.end(), id) != cfg_.mods.end()) {
        std::string where = cfg_.engine_root.string();
        for (auto& d : cfg_.mod_dirs) where += ", " + d.string();
        throw SimError("mod '" + id + "' not found under " + where);
      }
      continue;
    }
    manifests_[id] = mods::load_manifest_file(it->second / "mod.toml");
    real_dir_[id] = it->second;
    for (auto& [dep, range] : manifests_[id].depends) todo.push_back(dep);
  }
  stage_all();

  mods::HostConfig hc;
  hc.storage_backend = backend_;
  hc.rng = streams_.get();
  hc.default_memory_cap = cfg_.memory_cap_mb << 20;
  hc.budget.steps_per_dispatch = cfg_.steps_per_dispatch;
  hc.budget.steps_per_slice = cfg_.steps_per_dispatch;
  for (auto& [id, bo] : bundle_opts_) hc.bundle_for[id] = bo;
  host_ = std::make_unique<mods::HostRuntime>(std::move(hc));

  ids_ = std::make_unique<mods::IdMap>(std::map<std::string, mods::KindRange>{
      {"skill", {100000, 109999}}, {"status", {100000, 109999}}, {"item", {500000, 599999}}});
  loader_ = std::make_unique<mods::ModLoader>(*ids_);
  loader_->set_activator(host_.get());

  mods::LoadRequest req;
  for (auto& [id, m] : manifests_) {
    req.manifests.push_back(m);
    req.packages[id] = stage_dir_.at(id);
  }
  for (auto& [id, perms] : cfg_.grants) {
    for (auto& p : perms) {
      auto pp = mods::parse_permission(p);
      if (!pp) throw SimError("bad grant '" + p + "' for " + id);
      req.lock.entries[id].granted.insert(*pp);
    }
  }
  if (cfg_.require_ruleset) req.resolve.required_provides = {"ruleset"};
  try {
    loader_->load(req);
  } catch (const mods::LoadError& e) {
    throw SimError(std::string("mod load refused [") + e.code() + "]: " + e.what());
  }
  order_ = loader_->load_order();
  for (auto& id : order_) install_hosts(id);
}

void Sim::install_hosts(const std::string& mod) {
  auto sb = host_->sandbox(mod);
  if (!sb) return;
  auto& rm = host_->reload_manager();
  const uint64_t gen = rm.active_generation(mod);
  // ReloadManager installs a host with a timer hook and no log. Rebuild the same timer hook from its
  // public API and add the print() sink, so eval() can read what a chunk prints.
  sb->set_host({[this, mod, gen, wsb = std::weak_ptr<script_host::ModSandbox>(sb)](uint64_t ms, int ref) {
                  auto& rmm = host_->reload_manager();
                  if (rmm.active_generation(mod) != gen) return;  // retired generations cannot start timers
                  auto sbp = wsb.lock();
                  if (!sbp) return;
                  rmm.schedule(mod, ms, [sbp, mod, ref] { sbp->call_ref(ref, mod + "/timer"); });
                },
                [this](const std::string& line) { log_capture_.push_back(line); }});
  bus_cache_.clear();
}

void Sim::build_world() {
  grid_ = std::make_unique<engine::MapGrid>(static_cast<uint16_t>(cfg_.map_w), static_cast<uint16_t>(cfg_.map_h));
  for (int y = 0; y < cfg_.map_h; ++y)
    for (int x = 0; x < cfg_.map_w; ++x) grid_->set_terrain(x, y, engine::cell::Walkable | engine::cell::Shootable);
  spatial_ = std::make_unique<engine::SpatialIndex>(*grid_);
  engine::MoveHooks hooks;
  hooks.before_move = [this](engine::EntityId e, engine::Point, engine::Point) {
    if (!bus_has_hooks("unit:before_move")) return engine::MoveVerdict::Allow;
    auto it = gid_of_ent_.find((uint64_t(e.generation) << 32) | e.index);
    if (it == gid_of_ent_.end()) return engine::MoveVerdict::Allow;
    HookResult r = dispatch("unit:before_move", it->second);
    return r.cancelled ? engine::MoveVerdict::Veto : engine::MoveVerdict::Allow;
  };
  hooks.moved = [this](engine::EntityId e, engine::Point from, engine::Point to) { unit_moved(e, from, to); };
  movement_ = std::make_unique<engine::MovementSystem>(reg_, *grid_, *spatial_, *timers_, std::move(hooks));

  // netproto: the codec set of the real spec for the default client build
  codec_ = std::make_unique<netproto::CodecSet>(
      netproto::CodecSet::build(netproto::generated::client_packets_catalog(), {20211103, netproto::Flavour::Re}));
  c2s_ = wire::frame_table(wire::Dir::C2S, *codec_);
  s2c_ = wire::frame_table(wire::Dir::S2C, *codec_);

  script_host::DialogueConfig dc;
  dc.token_seed = cfg_.seed ? cfg_.seed : 1;
  dialogues_ = std::make_unique<script_host::DialogueManager>(dc, &host_->reload_manager());
}

bool Sim::has_mod(const std::string& id) const { return manifests_.count(id) != 0; }
const mods::Manifest& Sim::manifest(const std::string& id) const {
  auto it = manifests_.find(id);
  if (it == manifests_.end()) throw SimError("mod '" + id + "' is not part of this sim");
  return it->second;
}
std::string Sim::ruleset_id() const {
  for (auto& id : order_) {
    const auto& p = manifests_.at(id).provides;
    if (std::find(p.begin(), p.end(), "ruleset") != p.end()) return id;
  }
  return {};
}
uint64_t Sim::generation(const std::string& mod) const { return host_->reload_manager().active_generation(mod); }
std::filesystem::path Sim::stage_dir(const std::string& mod) const {
  auto it = stage_dir_.find(mod);
  if (it == stage_dir_.end()) throw SimError("mod '" + mod + "' is not part of this sim");
  return it->second;
}

// ---- trace ------------------------------------------------------------------------------------------
void Sim::trace(const std::string& line) {
  trace_hash_ = fnv(trace_hash_, line);
  ++trace_count_;
  if (cfg_.keep_trace && trace_lines_.size() < cfg_.trace_cap) trace_lines_.push_back(std::to_string(clock_.now()) + " " + line);
}

// ---- time --------------------------------------------------------------------------------------------
void Sim::step() { run_for(cfg_.tick_ms); }

void Sim::run_for(int64_t ms) {
  int64_t left = ms;
  while (left > 0) {
    const int64_t dt = std::min<int64_t>(left, cfg_.tick_ms);
    left -= dt;
    clock_.advance(dt);
    ++steps_;
    for (auto& s : sessions_) {
      s->budget.begin_cycle();
      process_inbound(*s);
    }
    timers_->run_due();
    host_->reload_manager().advance_game_time(static_cast<uint64_t>(dt));
    wake_dialogues();
    after_tick_walk_stops();
    for (auto& p : players_) p->pump();
  }
}

bool Sim::run_until(const std::function<bool()>& done, int64_t timeout_ms) {
  if (done()) return true;
  int64_t spent = 0;
  while (spent < timeout_ms) {
    const int64_t dt = std::min<int64_t>(cfg_.tick_ms, timeout_ms - spent);
    run_for(dt);
    spent += dt;
    if (done()) return true;
  }
  return false;
}

// ---- hooks / eval / reload ---------------------------------------------------------------------------------
bool Sim::bus_has_hooks(const std::string& event) {
  auto it = bus_cache_.find(event);
  if (it != bus_cache_.end()) return it->second;
  bool has = !host_->reload_manager().bus().order(event).empty();
  bus_cache_[event] = has;
  return has;
}

HookResult Sim::dispatch(const std::string& event, int64_t value) {
  script_host::EffectQueue q;
  script_host::HookContext ctx;
  ctx.num.set(value);
  ctx.effects = &q;
  host_->reload_manager().bus().set_tick(static_cast<uint64_t>(steps_));
  script_host::DispatchResult dr = host_->dispatch(event, ctx);
  HookResult r;
  r.cancelled = dr.cancelled;
  r.reason = dr.cancel_reason;
  r.cancelled_by = dr.cancelled_by;
  r.replaced_by = dr.replaced_by;
  r.ran = dr.ran;
  r.effects = dr.committed_effects;
  for (auto& e : dr.errors) r.errors.push_back(e.hook + ": " + e.kind + ": " + e.message);
  r.diagnostics = dr.diagnostics;
  r.value = ctx.num.value();
  std::string t = "hook " + event + " v=" + std::to_string(value) + " ->" + std::to_string(r.value) + (r.cancelled ? " cancelled by " + r.cancelled_by : "") + " ran=";
  for (auto& h : r.ran) t += h + ",";
  t += " effects=" + std::to_string(r.effects.size()) + " errors=" + std::to_string(r.errors.size());
  trace(t);
  return r;
}

RunReport Sim::eval(const std::string& mod, const std::string& source) {
  log_capture_.clear();
  script_host::RunResult r = host_->run(mod, "eval", source);
  RunReport rep;
  rep.lines = std::move(log_capture_);
  log_capture_.clear();
  if (r.status != script_host::RunStatus::Ok) throw SimError(r.error);
  return rep;
}

ReloadOutcome Sim::reload(const std::string& mod, const std::optional<std::string>& init_source) {
  if (!manifests_.count(mod)) throw SimError("mod '" + mod + "' is not part of this sim");
  if (init_source) {
    spit(stage_dir_.at(mod) / "scripts" / "init.luau", *init_source);
  }
  std::string chunk = mods::bundle_package(stage_dir_.at(mod), bundle_opts_.at(mod));
  script_host::ReloadResult r = host_->reload_mod(mod, chunk);
  ReloadOutcome o;
  o.ok = r.ok;
  o.needs_restart = r.needs_restart;
  o.generation = r.active_generation;
  o.error = r.error;
  if (r.ok) install_hosts(mod);
  trace("reload " + mod + (r.ok ? " ok gen=" + std::to_string(r.active_generation) : " refused: " + r.error));
  return o;
}

void Sim::disable_mod(const std::string& id) {
  loader_->disable(id);
  for (auto& m : order_) install_hosts(m);
  trace("mod disable " + id);
}
void Sim::enable_mod(const std::string& id) {
  loader_->enable(id);
  for (auto& m : order_) install_hosts(m);
  trace("mod enable " + id);
}

// ---- legacy NPC glue ---------------------------------------------------------------------------------------
void Sim::ensure_npc_runtime(const std::string& mod) {
  const uint64_t gen = generation(mod);
  auto it = npc_runtime_gen_.find(mod);
  if (it != npc_runtime_gen_.end() && it->second == gen) return;
  std::string src =
      "SIM_LNPC = LEGACY.new{ host = LEGACY.sandbox_host() }\n";
  for (auto& [path, text] : npc_files_) src += "SIM_LNPC:addFile(" + lua_quote(path) + ", " + lua_long(text) + ")\n";
  src += "for _, e in SIM_LNPC.loader.npcList do print('@npc\\t' .. e.exname .. '\\t' .. tostring(e.map) .. '\\t' .. tostring(e.x) .. '\\t' .. tostring(e.y)) end\n";
  eval(mod, src);
  npc_runtime_gen_[mod] = gen;
}

std::vector<std::string> Sim::load_npc_file(const fs::path& file) {
  if (!has_mod("legacy.npc")) throw SimError("legacy.npc is not loaded; cannot load an NPC file");
  std::string text = slurp(file);
  const std::string key = file.generic_string();
  npc_files_.emplace_back(key, text);
  npc_runtime_gen_.erase("legacy.npc");  // rebuild with the new file
  ensure_npc_runtime("legacy.npc");
  // list the names this file declared (the eval printed the whole corpus; ask again, cheap)
  log_capture_.clear();
  RunReport rep = eval("legacy.npc", "for _, e in SIM_LNPC.loader.npcList do if e.path == " + lua_quote(key) + " then print(e.exname) end end");
  for (auto& n : rep.lines) npc_index_[n] = key;
  return rep.lines;
}

// ---- combat -------------------------------------------------------------------------------------------------
Sim::AttackResult Sim::basic_attack(uint32_t attacker, uint32_t target) {
  AttackResult res;
  const std::string rs = ruleset_id();
  if (rs.empty()) { res.error = "no ruleset mod is loaded"; return res; }
  Unit* a = unit_of_gid(attacker);
  Unit* t = unit_of_gid(target);
  if (!a || !t) { res.error = "unknown unit"; return res; }
  if (a->unit_src.empty() || t->unit_src.empty()) { res.error = "both units need a combat snapshot (set_unit)"; return res; }
  const std::string src =
      "local weapon = SIM_IMPORT('scripts/damage/weapon')\n"
      "local unit = SIM_IMPORT('scripts/damage/unit')\n"
      "local defaults = SIM_IMPORT('data/damage_cfg')\n"
      "local fake_ctx = SIM_IMPORT('tests/support/fake_ctx')\n"
      "local A, T = " + a->unit_src + ", " + t->unit_src + "\n"
      "local drng = { used = 0 }\n"
      "function drng.rnd() drng.used += 1 return rng.main(2147483648) end\n"
      "function drng.value(lo, hi) drng.used += 1 return lo + rng.main(hi - lo + 1) end\n"
      "local cfg = {}\n"
      "for k, v in pairs(defaults) do cfg[k] = v end\n"
      "local ok, r = pcall(weapon.calc_attack, fake_ctx.new(drng), cfg, unit.normalize(A), unit.normalize(T), { map = 'normal' })\n"
      "if ok then print('@atk', r.damage, r.damage2 or 0, r.div or 1, r.flag or 0, r.type or 0, drng.used)\n"
      "else print('@err', type(r) == 'table' and ((r.code or 'ERROR') .. ': ' .. tostring(r.msg)) or tostring(r)) end\n";
  const bool strict = streams_->strict_parity();
  streams_->set_strict_parity(false);  // the default pipeline draws from the main stream (BR-RULESET-115)
  struct Restore { script_host::RngStreams* s; bool v; ~Restore() { s->set_strict_parity(v); } } restore{streams_.get(), strict};
  try {
    RunReport rep = eval(rs, src);
    for (auto& l : rep.lines) {
      std::vector<std::string> f;
      std::stringstream ss(l);
      std::string tok;
      while (std::getline(ss, tok, '\t')) f.push_back(tok);
      if (f.empty()) continue;
      if (f[0] == "@atk" && f.size() >= 7) {
        res.ok = true;
        res.damage = std::stoll(f[1]);
        res.damage2 = std::stoll(f[2]);
        res.div = std::stoll(f[3]);
        res.flag = std::stoll(f[4]);
        res.type = std::stoll(f[5]);
        res.draws = std::stoull(f[6]);
      } else if (f[0] == "@err") {
        res.error = f.size() > 1 ? f[1] : "error";
      }
    }
  } catch (const SimError& e) {
    res.error = e.what();
  }
  if (res.ok) t->last_damage = res.damage;
  trace("attack " + std::to_string(attacker) + "->" + std::to_string(target) + (res.ok ? " dmg=" + std::to_string(res.damage) + " draws=" + std::to_string(res.draws) : " error " + res.error));
  return res;
}

std::optional<int64_t> Sim::last_damage(uint32_t gid) const {
  const Unit* u = unit_of_gid(gid);
  return u ? u->last_damage : std::nullopt;
}

void Sim::block_cell(int x, int y) { grid_->set_terrain(x, y, 0); }

}  // namespace tooling::sim

namespace tooling::sim {
uint16_t Sim::skill_client_id(const std::string& skill_id) {
  const std::string rs = ruleset_id();
  if (rs.empty()) throw SimError("no ruleset mod is loaded");
  RunReport rep = eval(rs, "for _, s in ipairs(SIM_IMPORT('data/skills')) do if s.id == " + lua_quote(skill_id) + " then print(s.client_id) end end");
  if (rep.lines.empty()) throw SimError("skill '" + skill_id + "' is not defined by " + rs);
  return static_cast<uint16_t>(std::stoul(rep.lines[0]));
}
}  // namespace tooling::sim
