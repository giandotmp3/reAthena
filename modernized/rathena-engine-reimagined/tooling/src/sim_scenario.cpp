#include "tooling/sim_scenario.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <sstream>

#include "script_host/sandbox.hpp"
#include "tooling/sim_player.hpp"

namespace tooling::sim {

namespace fs = std::filesystem;

const char* scenario_prelude_source();  // generated (embedded sim/scenario_prelude.luau)

namespace {

std::string slurp(const fs::path& p) {
  std::ifstream in(p, std::ios::binary);
  if (!in) throw SimError("cannot read " + p.string());
  std::ostringstream o;
  o << in.rdbuf();
  return o.str();
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == sep) { if (!cur.empty()) out.push_back(cur); cur.clear(); }
    else cur += c;
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r");
  if (a == std::string::npos) return {};
  size_t b = s.find_last_not_of(" \t\r");
  return s.substr(a, b - a + 1);
}

Val event_val(const wire::Packet& e) {
  VMap m;
  m["name"] = e.name;
  m["opcode"] = static_cast<double>(e.opcode);
  m["at"] = static_cast<double>(e.at_ms);
  m["bytes"] = static_cast<double>(e.raw.size());
  for (const auto& [k, v] : e.fields) {
    if (auto* i = std::get_if<int64_t>(&v)) m[k] = static_cast<double>(*i);
    else m[k] = std::get<std::string>(v);
  }
  return Val(std::move(m));
}

Val storage_val(const script_host::Value& v) {
  if (std::holds_alternative<std::monostate>(v)) return Val();
  if (auto* i = std::get_if<int64_t>(&v)) return Val(static_cast<double>(*i));
  if (auto* d = std::get_if<double>(&v)) return Val(*d);
  if (auto* b = std::get_if<bool>(&v)) return Val(*b);
  if (auto* s = std::get_if<std::string>(&v)) return Val(*s);
  auto& t = *std::get<std::shared_ptr<script_host::ValueTable>>(v);
  bool dense = t.entries.empty() && !t.array.empty();
  int64_t expect = 1;
  for (auto& [k, _] : t.array) { if (k != expect++) { dense = false; break; } }
  if (dense) {
    VArr a;
    for (auto& [_, e] : t.array) a.push_back(storage_val(e));
    return Val(std::move(a));
  }
  VMap m;
  for (auto& [k, e] : t.entries) m[k] = storage_val(e);
  for (auto& [k, e] : t.array) m[std::to_string(k)] = storage_val(e);
  return Val(std::move(m));
}

script_host::Scope scope_of(const std::string& s) {
  if (s == "character") return script_host::Scope::Character;
  if (s == "account") return script_host::Scope::Account;
  if (s == "world") return script_host::Scope::World;
  throw SimError("unknown storage scope '" + s + "' (character | account | world)");
}

Val hook_val(const HookResult& r) {
  VMap m;
  m["cancelled"] = r.cancelled;
  m["reason"] = r.reason;
  m["cancelled_by"] = r.cancelled_by;
  m["replaced_by"] = r.replaced_by;
  m["value"] = static_cast<double>(r.value);
  VArr ran, errors, diags, effects;
  for (auto& s : r.ran) ran.push_back(s);
  for (auto& s : r.errors) errors.push_back(s);
  for (auto& s : r.diagnostics) diags.push_back(s);
  for (auto& e : r.effects) {
    VMap em;
    em["kind"] = e.kind;
    em["target"] = e.target;
    em["payload"] = e.payload_json;
    effects.push_back(Val(std::move(em)));
  }
  m["ran"] = Val(std::move(ran));
  m["errors"] = Val(std::move(errors));
  m["diagnostics"] = Val(std::move(diags));
  m["effects"] = Val(std::move(effects));
  return Val(std::move(m));
}

int hex_digit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  throw SimError("bad hex digit");
}

// ---- host side of the RPC channel --------------------------------------------------------------
struct Host {
  Sim& sim;
  ScenarioResult& result;
  std::chrono::steady_clock::time_point wall0 = std::chrono::steady_clock::now();
  std::string req, reply_rest;
  std::map<std::string, std::function<Val(const VArr&)>> cmd;

  SimPlayer& P(const Val& id) { return sim.player(static_cast<size_t>(id.i64())); }
  static Val info_of(const SimPlayer& p) {
    VMap m;
    m["id"] = 0.0;
    m["gid"] = static_cast<double>(p.gid());
    m["aid"] = static_cast<double>(p.aid());
    m["name"] = p.name();
    return Val(std::move(m));
  }
  Val player_val(size_t index) {
    SimPlayer& p = sim.player(index);
    Val v = info_of(p);
    v.map_mut()["id"] = static_cast<double>(index);
    return v;
  }
  size_t index_of(const SimPlayer* p) {
    for (size_t i = 0; i < sim.player_count(); ++i)
      if (&sim.player(i) == p) return i;
    return 0;
  }

  Host(Sim& s, ScenarioResult& r) : sim(s), result(r) {
    cmd["now"] = [this](const VArr&) { return Val(sim.now_ms()); };
    cmd["step"] = [this](const VArr& a) {
      for (int64_t i = 0, n = a.at(0).i64(); i < n; ++i) sim.step();
      return Val();
    };
    cmd["run_for"] = [this](const VArr& a) { sim.run_for(a.at(0).i64()); return Val(); };
    cmd["seed"] = [this](const VArr&) { return Val(sim.config().seed); };
    cmd["tag"] = [this](const VArr& a) {
      const std::string& id = a.at(0).str();
      if (std::find(result.rule_tags.begin(), result.rule_tags.end(), id) == result.rule_tags.end()) result.rule_tags.push_back(id);
      return Val();
    };
    cmd["mods"] = [this](const VArr&) {
      VArr out;
      for (auto& m : sim.load_order()) out.push_back(m);
      return Val(std::move(out));
    };
    cmd["assert"] = [this](const VArr&) { ++result.assertions; return Val(); };

    cmd["player_new"] = [this](const VArr& a) {
      const Val& s = a.at(0);
      Sim::PlayerSpec ps;
      ps.name = s.str_or("name", "");
      ps.x = static_cast<int>(s.num_or("x", 100));
      ps.y = static_cast<int>(s.num_or("y", 100));
      ps.job = s.str_or("job", "novice");
      ps.level = static_cast<int>(s.num_or("level", 1));
      ps.unit = s.str_or("unit", "");
      ps.aid = static_cast<uint32_t>(s.num_or("aid", 0));
      ps.connect = s.bool_or("connect", true);
      SimPlayer& p = sim.add_player(ps);
      return player_val(index_of(&p));
    };
    cmd["crowd_new"] = [this](const VArr& a) {
      const Val& s = a.at(0);
      const int count = static_cast<int>(s.num_or("count", 0));
      const std::string prefix = s.str_or("prefix", "p");
      const int x0 = static_cast<int>(s.num_or("x0", 10)), y0 = static_cast<int>(s.num_or("y0", 10));
      const int cols = std::max(1, static_cast<int>(s.num_or("columns", 20)));
      const int spacing = std::max(1, static_cast<int>(s.num_or("spacing", 3)));
      std::vector<SimPlayer*> ps;
      for (int i = 0; i < count; ++i) {
        Sim::PlayerSpec spec;
        spec.name = prefix + std::to_string(i);
        spec.x = x0 + (i % cols) * spacing;
        spec.y = y0 + (i / cols) * spacing;
        spec.connect = false;
        ps.push_back(&sim.add_player(spec));
        ps.back()->set_history_cap(static_cast<size_t>(s.num_or("history", 256)));
      }
      SimPlayer::connect_all(sim, ps);
      VArr out;
      for (auto* p : ps) out.push_back(player_val(index_of(p)));
      return Val(std::move(out));
    };
    cmd["p_walk"] = [this](const VArr& a) { P(a.at(0)).walk(static_cast<int>(a.at(1).i64()), static_cast<int>(a.at(2).i64())); return Val(); };
    cmd["p_say"] = [this](const VArr& a) { P(a.at(0)).say(a.at(1).str()); return Val(); };
    cmd["p_say_raw"] = [this](const VArr& a) { P(a.at(0)).send_raw(wire::cz_request_chat(a.at(1).str())); return Val(); };
    cmd["p_click_npc"] = [this](const VArr& a) { P(a.at(0)).click_npc(a.at(1).str()); return Val(); };
    cmd["p_answer"] = [this](const VArr& a) { P(a.at(0)).answer(static_cast<int>(a.at(1).i64())); return Val(); };
    cmd["p_next"] = [this](const VArr& a) { P(a.at(0)).next(); return Val(); };
    cmd["p_input"] = [this](const VArr& a) { P(a.at(0)).input(a.at(1).str()); return Val(); };
    cmd["p_input_number"] = [this](const VArr& a) { P(a.at(0)).input_number(static_cast<int32_t>(a.at(1).i64())); return Val(); };
    cmd["p_close_dialog"] = [this](const VArr& a) { P(a.at(0)).close_dialog(); return Val(); };
    cmd["p_ping"] = [this](const VArr& a) { P(a.at(0)).ping(); return Val(); };
    cmd["p_attack"] = [this](const VArr& a) { P(a.at(0)).attack(static_cast<uint32_t>(a.at(1).i64())); return Val(); };
    cmd["p_cast"] = [this](const VArr& a) {
      P(a.at(0)).cast(a.at(1).str(), static_cast<uint32_t>(a.at(2).i64()), static_cast<uint16_t>(a.at(3).i64()));
      return Val();
    };
    cmd["p_disconnect"] = [this](const VArr& a) { P(a.at(0)).disconnect(); return Val(); };
    cmd["p_send_raw"] = [this](const VArr& a) {
      const std::string& hex = a.at(1).str();
      wire::Bytes b;
      for (size_t i = 0; i + 1 < hex.size(); i += 2) b.push_back(static_cast<uint8_t>(hex_digit(hex[i]) * 16 + hex_digit(hex[i + 1])));
      P(a.at(0)).send_raw(b);
      return Val();
    };
    cmd["p_wait_for"] = [this](const VArr& a) {
      auto e = P(a.at(0)).wait_for(a.at(1).str(), a.at(2).i64());
      return e ? event_val(*e) : Val();
    };
    cmd["p_expect"] = [this](const VArr& a) { return event_val(P(a.at(0)).expect(a.at(1).str(), a.at(2).i64())); };
    cmd["p_expect_none"] = [this](const VArr& a) { return Val(P(a.at(0)).expect_none(a.at(1).str(), a.at(2).i64())); };
    cmd["p_events"] = [this](const VArr& a) {
      VArr out;
      for (auto& e : P(a.at(0)).drain_received()) out.push_back(event_val(e));
      return Val(std::move(out));
    };
    cmd["p_history"] = [this](const VArr& a) {
      VArr out;
      SimPlayer& p = P(a.at(0));
      for (auto& e : p.history())
        if (a.at(1).is_nil() || e.name == a.at(1).str() || e.name == SimPlayer::alias(a.at(1).str())) out.push_back(event_val(e));
      return Val(std::move(out));
    };
    cmd["p_count"] = [this](const VArr& a) { return Val(static_cast<double>(P(a.at(0)).count(a.at(1).str()))); };
    cmd["p_info"] = [this](const VArr& a) {
      SimPlayer& p = P(a.at(0));
      Val v = info_of(p);
      VMap& m = v.map_mut();
      m["id"] = a.at(0).num();
      auto pos = p.position();
      m["x"] = pos ? Val(pos->x) : Val();
      m["y"] = pos ? Val(pos->y) : Val();
      m["connected"] = p.connected();
      m["disconnected"] = p.disconnected();
      m["reason"] = p.disconnect_reason();
      VArr seen;
      for (uint32_t g : p.visible()) seen.push_back(static_cast<double>(g));
      m["sees"] = Val(std::move(seen));
      return v;
    };
    cmd["p_sees"] = [this](const VArr& a) { return Val(P(a.at(0)).sees(static_cast<uint32_t>(a.at(1).i64()))); };

    cmd["npc_load"] = [this](const VArr& a) {
      fs::path p = a.at(0).str();
      if (p.is_relative()) p = repo_root() / p;
      VArr out;
      for (auto& n : sim.load_npc_file(p)) out.push_back(n);
      return Val(std::move(out));
    };
    cmd["npc_place"] = [this](const VArr& a) {
      return Val(static_cast<double>(sim.place_npc(a.at(0).str(), static_cast<int>(a.at(1).i64()), static_cast<int>(a.at(2).i64()))));
    };
    cmd["dummy"] = [this](const VArr& a) {
      return Val(static_cast<double>(sim.spawn_dummy(a.at(0).str(), static_cast<int>(a.at(1).i64()), static_cast<int>(a.at(2).i64()),
                                                     a.at(3).is_nil() ? std::string() : a.at(3).str())));
    };
    cmd["unit_set"] = [this](const VArr& a) { sim.set_unit(static_cast<uint32_t>(a.at(0).i64()), a.at(1).str()); return Val(); };
    cmd["attack"] = [this](const VArr& a) {
      auto r = sim.basic_attack(static_cast<uint32_t>(a.at(0).i64()), static_cast<uint32_t>(a.at(1).i64()));
      VMap m;
      m["ok"] = r.ok;
      m["error"] = r.error;
      m["damage"] = static_cast<double>(r.damage);
      m["damage2"] = static_cast<double>(r.damage2);
      m["div"] = static_cast<double>(r.div);
      m["flag"] = static_cast<double>(r.flag);
      m["type"] = static_cast<double>(r.type);
      m["draws"] = static_cast<double>(r.draws);
      return Val(std::move(m));
    };
    cmd["last_damage"] = [this](const VArr& a) {
      auto d = sim.last_damage(static_cast<uint32_t>(a.at(0).i64()));
      return d ? Val(*d) : Val();
    };
    cmd["dialogue_of"] = [this](const VArr& a) {
      auto d = sim.dialogue_of(static_cast<uint32_t>(a.at(0).i64()));
      VMap m;
      m["active"] = d.active;
      m["npc"] = static_cast<double>(d.npc);
      m["generation"] = static_cast<double>(d.generation);
      m["mod"] = d.mod;
      return Val(std::move(m));
    };
    cmd["npc_log"] = [this](const VArr&) {
      VArr out;
      for (auto& l : sim.npc_log()) out.push_back(l);
      return Val(std::move(out));
    };
    cmd["block"] = [this](const VArr& a) { sim.block_cell(static_cast<int>(a.at(0).i64()), static_cast<int>(a.at(1).i64())); return Val(); };

    cmd["eval"] = [this](const VArr& a) {
      VArr out;
      for (auto& l : sim.eval(a.at(0).str(), a.at(1).str()).lines) out.push_back(l);
      return Val(std::move(out));
    };
    cmd["reload"] = [this](const VArr& a) {
      std::optional<std::string> init;
      if (a.size() > 1 && !a[1].is_nil()) init = a[1].str();
      auto r = sim.reload(a.at(0).str(), init);
      VMap m;
      m["ok"] = r.ok;
      m["error"] = r.error;
      m["generation"] = static_cast<double>(r.generation);
      m["needs_restart"] = r.needs_restart;
      return Val(std::move(m));
    };
    cmd["generation"] = [this](const VArr& a) { return Val(static_cast<double>(sim.generation(a.at(0).str()))); };
    cmd["dispatch"] = [this](const VArr& a) { return hook_val(sim.dispatch(a.at(0).str(), a.at(1).i64())); };
    cmd["registrations"] = [this](const VArr& a) {
      return Val(static_cast<double>(sim.host().reload_manager().live_registrations(a.at(0).str(), static_cast<uint64_t>(a.at(1).i64()))));
    };
    cmd["mod_disable"] = [this](const VArr& a) {
      sim.disable_mod(a.at(0).str());
      return Val();
    };
    cmd["mod_enable"] = [this](const VArr& a) {
      sim.enable_mod(a.at(0).str());
      return Val();
    };
    cmd["mod_state"] = [this](const VArr& a) {
      switch (sim.loader().state(a.at(0).str())) {
        case mods::ModState::Active: return Val("active");
        case mods::ModState::Disabled: return Val("disabled");
        default: return Val("invalid");
      }
    };
    cmd["storage_get"] = [this](const VArr& a) {
      auto v = sim.storage().get(scope_of(a.at(0).str()), a.at(1).str(), a.at(2).str(), a.at(2).str(), a.at(3).str());
      return v ? storage_val(*v) : Val();
    };
    cmd["rng"] = [this](const VArr&) {
      VMap m;
      auto r = sim.rng_report();
      m["main"] = static_cast<double>(r.main_draws);
      m["native"] = static_cast<double>(r.native_draws);
      return Val(std::move(m));
    };
    cmd["trace_hash"] = [this](const VArr&) {
      char b[32];
      std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(sim.trace_hash()));
      return Val(std::string(b));
    };
    cmd["stats"] = [this](const VArr&) {
      VMap m;
      m["now"] = static_cast<double>(sim.now_ms());
      m["steps"] = static_cast<double>(sim.steps());
      m["players"] = static_cast<double>(sim.player_count());
      m["trace_events"] = static_cast<double>(sim.trace_count());
      m["wall_ms"] = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - wall0).count();
      m["storage_backend"] = sim.storage_backend_name();
      return Val(std::move(m));
    };
  }

  // One prompt from the scenario; returns the reply text.
  std::string handle(const std::string& prompt) {
    if (prompt.empty()) return "E0bad request";
    const char tag = prompt[0];
    if (tag == 'C') { req += prompt.substr(1); return "K"; }
    if (tag == 'M') return next_piece();
    if (tag != 'X') return "E0unknown request tag";
    req += prompt.substr(1);
    std::string body;
    char kind = 'R';
    try {
      Val call = decode_all(req);
      const VArr& arr = call.arr();
      auto it = cmd.find(arr.at(0).str());
      if (it == cmd.end()) throw SimError("unknown command '" + arr.at(0).str() + "'");
      VArr args(arr.begin() + 1, arr.end());
      body = encode(it->second(args));
    } catch (const std::exception& e) {
      kind = 'E';
      body = e.what();
    }
    req.clear();
    constexpr size_t kPiece = 3000;
    if (body.size() <= kPiece) return std::string(1, kind) + "0" + body;
    reply_rest = body.substr(kPiece);
    pending_kind = kind;
    return std::string(1, kind) + "1" + body.substr(0, kPiece);
  }
  char pending_kind = 'R';
  std::string next_piece() {
    constexpr size_t kPiece = 3000;
    if (reply_rest.size() <= kPiece) {
      std::string r = std::string(1, pending_kind) + "0" + reply_rest;
      reply_rest.clear();
      return r;
    }
    std::string r = std::string(1, pending_kind) + "1" + reply_rest.substr(0, kPiece);
    reply_rest.erase(0, kPiece);
    return r;
  }
};

}  // namespace

fs::path repo_root() {
  fs::path p = fs::absolute(fs::path(TOOLING_SOURCE_DIR));
  for (int i = 0; i < 8 && p.has_parent_path(); ++i) {
    if (fs::is_directory(p / "npc") && fs::is_directory(p / "src") && fs::exists(p / "conf")) return p;
    p = p.parent_path();
  }
  return fs::absolute(fs::path(TOOLING_SOURCE_DIR)) / ".." / ".." / "..";
}

ScenarioSpec parse_scenario(const std::string& path, SimConfig cfg) {
  ScenarioSpec sp;
  sp.path = path;
  sp.source = slurp(path);
  std::istringstream in(sp.source);
  std::string line;
  const fs::path dir = fs::absolute(path).parent_path();
  int n = 0;
  while (std::getline(in, line) && n++ < 80) {
    line = trim(line);
    if (line.rfind("-- rule:", 0) == 0) {
      for (auto& t : split(trim(line.substr(8)), ' ')) sp.rule_tags.push_back(t);
      continue;
    }
    if (line.rfind("--@", 0) != 0) continue;
    std::string body = trim(line.substr(3));
    std::string key = body.substr(0, body.find(' '));
    std::string val = key.size() < body.size() ? trim(body.substr(key.size())) : std::string();
    if (key == "rule") {
      for (auto& t : split(val, ' ')) sp.rule_tags.push_back(t);
    } else if (key == "mods") {
      cfg.mods.clear();
      for (auto& m : split(val, ',')) cfg.mods.push_back(trim(m));
    } else if (key == "mods-dir") {
      cfg.mod_dirs.push_back(fs::weakly_canonical(dir / val));
    } else if (key == "seed") {
      cfg.seed = std::stoull(val);
    } else if (key == "map") {
      auto x = val.find('x');
      cfg.map_w = std::stoi(val.substr(0, x));
      cfg.map_h = std::stoi(val.substr(x + 1));
    } else if (key == "golden") {
      cfg.golden = val == "full" ? GoldenStage::Full : GoldenStage::Subset;
      cfg.stage_test_support = true;
    } else if (key == "test-support") {
      cfg.stage_test_support = true;
    } else if (key == "grant") {
      auto eq = val.find('=');
      cfg.grants[val.substr(0, eq)] = split(val.substr(eq + 1), ',');
    } else if (key == "memory-storage") {
      cfg.sqlite = false;
    } else if (key == "npc") {
      sp.npc_files.push_back(val);
    } else if (key == "strict-parity") {
      cfg.strict_parity = val != "off";
    } else {
      throw SimError(path + ": unknown directive --@" + key);
    }
  }
  sp.cfg = std::move(cfg);
  return sp;
}

std::vector<std::string> list_scenarios(const std::string& dir) {
  std::vector<std::string> out;
  std::error_code ec;
  for (auto& e : fs::directory_iterator(dir, ec))
    if (e.is_regular_file() && e.path().extension() == ".luau") out.push_back(e.path().string());
  std::sort(out.begin(), out.end());
  return out;
}

ScenarioResult run_scenario_spec(ScenarioSpec spec) {
  using Clock = std::chrono::steady_clock;
  const auto t0 = Clock::now();
  ScenarioResult res;
  res.name = fs::path(spec.path).stem().string();
  res.rule_tags = spec.rule_tags;
  auto finish = [&](Sim* sim) {
    if (sim) {
      res.virtual_ms = sim->now_ms();
      res.trace_hash = sim->trace_hash();
      auto r = sim->rng_report();
      res.main_rng_draws = r.main_draws;
      res.native_rng_draws = r.native_draws;
    }
    res.wall_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
  };
  std::unique_ptr<Sim> sim;
  try {
    sim = std::make_unique<Sim>(spec.cfg);
    for (auto& f : spec.npc_files) {
      fs::path p = f;
      if (p.is_relative()) p = repo_root() / p;
      sim->load_npc_file(p);
    }
    script_host::ModEnvironmentSpec es;
    es.mod_id = "sim.scenario";
    es.memory_cap_bytes = 256ull << 20;
    es.budget.steps_per_slice = 200'000'000ull;  // a runaway scenario fails fast with a budget error
    es.budget.steps_per_dispatch = 200'000'000ull;
    es.budget.wall_us_per_dispatch = 0;
    es.budget.wall_us_per_tick = 0;
    script_host::ModSandbox sb(es);
    std::vector<std::string> printed;
    sb.set_host({{}, [&](const std::string& l) { printed.push_back(l); }});
    auto pr = sb.run("sim_prelude", scenario_prelude_source());
    if (pr.status != script_host::RunStatus::Ok) throw SimError("scenario prelude failed: " + pr.error);
    Host host(*sim, res);
    script_host::CoStep st = sb.co_start(res.name + ".luau", spec.source);
    for (;;) {
      if (st.kind == script_host::CoStep::Kind::Done) { res.passed = true; break; }
      if (st.kind == script_host::CoStep::Kind::Failed) {
        res.passed = false;
        res.message = st.result.error;
        break;
      }
      script_host::DialogueResponse resp;
      if (auto* in = std::get_if<script_host::InputRequest>(&st.request)) {
        resp.text = host.handle(in->prompt);
      } else if (auto* w = std::get_if<script_host::WaitRequest>(&st.request)) {
        sim->run_for(static_cast<int64_t>(w->ms));
      } else if (std::holds_alternative<script_host::ChooseRequest>(st.request)) {
        resp.choice = 1;
      } else if (auto* sy = std::get_if<script_host::SayRequest>(&st.request)) {
        printed.push_back(sy->text);
      }
      st = sb.co_resume(st.id, resp);
    }
    if (!printed.empty() && !res.passed) {
      res.message += "\n-- scenario output --";
      for (size_t i = printed.size() > 20 ? printed.size() - 20 : 0; i < printed.size(); ++i) res.message += "\n" + printed[i];
    }
    sb.co_cancel(st.id);
    res.output = printed;
  } catch (const std::exception& e) {
    res.passed = false;
    res.message = e.what();
  }
  finish(sim.get());
  return res;
}

ScenarioResult run_scenario(const std::string& luau_path) {
  try {
    return run_scenario_spec(parse_scenario(luau_path));
  } catch (const std::exception& e) {
    ScenarioResult r;
    r.name = fs::path(luau_path).stem().string();
    r.message = e.what();
    return r;
  }
}

}  // namespace tooling::sim
