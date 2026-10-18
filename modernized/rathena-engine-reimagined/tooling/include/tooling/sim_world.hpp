#pragma once
// rathena-sim world: engine-core (ECS, grid, virtual-clock tick, movement), script-host (one ModState
// per mod, EventBus, dialogue), the mod loader over a mods directory, persistence (SQLite in memory),
// seeded RNG with draw counting, and a minimal world/session boundary that terminates netproto
// frames. There is NO gateway here (no login server, no tickets, no sockets): the "server" is the
// handler table in sim_server.cpp and nothing else. See README "What the sim exercises".
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "engine/ecs/registry.hpp"
#include "engine/grid/grid.hpp"
#include "engine/movement/movement.hpp"
#include "engine/tick/tick.hpp"
#include "mods/host_runtime.hpp"
#include "mods/id_map.hpp"
#include "mods/loader.hpp"
#include "netproto/codec_set.hpp"
#include "netproto/gating.hpp"
#include "script_host/dialogue.hpp"
#include "script_host/numeric.hpp"
#include "tooling/sim.hpp"
#include "tooling/sim_value.hpp"
#include "tooling/sim_wire.hpp"

namespace rathena::persist {
class SqliteConnection;
class Quarantine;
class KvStore;
class KvStorageBackend;
}  // namespace rathena::persist

namespace tooling::sim {

struct SimError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

class SimPlayer;

enum class GoldenStage { None, Subset, Full };

struct SimConfig {
  uint64_t seed = 1;
  // Root of the engine tree (the directory holding ruleset-*, legacy-npc, mods, ...).
  std::filesystem::path engine_root;
  // Extra roots searched for mod.toml (scenario test mods, third-party mods).
  std::vector<std::filesystem::path> mod_dirs;
  // Mods to load (their dependencies are added). Default: rathena.base, ruleset.prerenewal, legacy.npc.
  std::vector<std::string> mods;
  // mods.lock grants: mod id -> permission strings ("storage.account", "storage.read:other.mod", ...).
  std::map<std::string, std::vector<std::string>> grants;
  bool require_ruleset = false;  // resolve option: exactly one ruleset must be enabled
  bool strict_parity = true;     // third-party hooks may not draw from the main RNG stream

  bool sqlite = true;  // false: script-host's in-memory StorageBackend
  std::string sqlite_path = ":memory:";

  std::string map_name = "prontera";
  int map_w = 320, map_h = 320;
  int64_t tick_ms = 20;         // one step(); the engine's tick floor
  int32_t walk_step_ms = 150;   // Mover::step_ms (a game number; supplied here, not by the engine)
  int view_range = 14;          // legacy AREA_SIZE
  bool keep_trace = true;       // keep trace lines (the hash is always computed)
  size_t trace_cap = 200000;

  // Ruleset package staging: what the bundle of each ruleset mod contains besides scripts/ and data/.
  GoldenStage golden = GoldenStage::None;
  bool stage_test_support = false;  // tests/support (fake ctx, scripted RNG, golden runner)
  bool stage_package_tests = false; // the package's whole tests/ and conformance/ trees
  size_t golden_subset_per_category = 3;
  // Extra generated files per mod (relative path -> content) and extra entry modules.
  std::map<std::string, std::map<std::string, std::string>> stage_files;
  std::map<std::string, std::vector<std::string>> extra_entries;
  std::filesystem::path work_dir;  // base of the staging area (default: system temp dir); the sim makes its own subdirectory
  uint64_t steps_per_dispatch = 2'000'000'000ull;
  uint64_t memory_cap_mb = 512;
};

struct HookResult {
  bool cancelled = false;
  std::string reason, cancelled_by, replaced_by;
  std::vector<std::string> ran;
  std::vector<script_host::Effect> effects;
  std::vector<std::string> errors;       // "<hook>: <kind>: <message>"
  std::vector<std::string> diagnostics;
  int64_t value = 0;                     // ctx value after the dispatch
};

struct ReloadOutcome {
  bool ok = false;
  bool needs_restart = false;
  uint64_t generation = 0;
  std::string error;
};

struct RunReport {
  std::vector<std::string> lines;  // print() output of the run
};

class Sim {
 public:
  explicit Sim(SimConfig cfg = {});
  ~Sim();
  Sim(const Sim&) = delete;
  Sim& operator=(const Sim&) = delete;

  const SimConfig& config() const { return cfg_; }

  // ---- virtual time (nothing moves unless the caller advances it) --------------------------
  int64_t now_ms() const { return clock_.now(); }
  void step();                                  // advances one tick_ms
  void run_for(int64_t ms);                     // whole ticks; the last one may be short
  bool run_until(const std::function<bool()>& done, int64_t timeout_ms);  // false on timeout
  uint64_t steps() const { return steps_; }

  // ---- RNG (seeded; draws are counted) -------------------------------------------------------
  script_host::RngStreams& rng_streams() { return *streams_; }
  DeterministicRng& rng() { return rng_; }
  struct RngReport { uint64_t main_draws = 0, native_draws = 0; };
  RngReport rng_report() const { return {streams_->main_draw_count(), rng_.draws()}; }

  // ---- trace -----------------------------------------------------------------------------------
  void trace(const std::string& line);
  uint64_t trace_hash() const { return trace_hash_; }
  uint64_t trace_count() const { return trace_count_; }
  const std::vector<std::string>& trace_lines() const { return trace_lines_; }

  // ---- mods -------------------------------------------------------------------------------------
  mods::HostRuntime& host() { return *host_; }
  mods::ModLoader& loader() { return *loader_; }
  const std::vector<std::string>& load_order() const { return order_; }
  bool has_mod(const std::string& id) const;
  const mods::Manifest& manifest(const std::string& id) const;
  std::string ruleset_id() const;  // the loaded mod that provides "ruleset", or ""
  uint64_t generation(const std::string& mod) const;
  // print() output of `source` run in the mod's sandbox (the globals of the mod are visible).
  RunReport eval(const std::string& mod, const std::string& source);
  // Re-runs the mod's bundle as generation N+1 (hot reload). With `init_source`, scripts/init.luau of
  // a copy-staged (synthetic) mod is replaced first. A failed reload leaves generation N active.
  ReloadOutcome reload(const std::string& mod, const std::optional<std::string>& init_source = std::nullopt);
  // Dispatches `event` on the live bus with an int64 context value.
  HookResult dispatch(const std::string& event, int64_t value);
  // Operator kill switch (`mod disable <id>`) and its inverse, through the mod loader. The sim re-installs
  // its print sink and timer hook on every sandbox the operation replaced.
  void disable_mod(const std::string& id);
  void enable_mod(const std::string& id);
  std::filesystem::path stage_dir(const std::string& mod) const;
  const std::filesystem::path& work_dir() const { return work_dir_; }

  // ---- storage ------------------------------------------------------------------------------------
  script_host::StorageApi& storage() { return host_->storage(); }
  std::string storage_backend_name() const { return cfg_.sqlite ? "sqlite" : "memory"; }

  // ---- world ---------------------------------------------------------------------------------------
  engine::MapGrid& grid() { return *grid_; }
  engine::Registry& registry() { return reg_; }
  engine::SpatialIndex& spatial() { return *spatial_; }
  engine::MovementSystem& movement() { return *movement_; }
  engine::TimerQueue& timers() { return *timers_; }
  void block_cell(int x, int y);  // terrain: not walkable

  // ---- players (client side objects; the "server" end is internal) ---------------------------------
  struct PlayerSpec {
    std::string name;
    int x = 100, y = 100;
    std::string job = "novice";
    int level = 1;
    std::string unit;      // Luau table constructor: combat snapshot for the ruleset pipeline
    uint32_t aid = 0;      // account id; 0: a fresh one (reuse an id to reconnect an account)
    bool connect = true;   // run the connect handshake now
  };
  SimPlayer& add_player(const PlayerSpec& spec);
  SimPlayer* player_by_gid(uint32_t gid);
  size_t player_count() const { return players_.size(); }
  SimPlayer& player(size_t index) { return *players_.at(index); }

  // ---- npcs & dummies -------------------------------------------------------------------------------
  // Loads a legacy .txt NPC file (legacy.npc must be loaded). Returns the names of the NPC entries.
  std::vector<std::string> load_npc_file(const std::filesystem::path& file);
  // Places a loaded NPC on the map. Returns its gid (the id the client clicks).
  uint32_t place_npc(const std::string& name, int x, int y);
  uint32_t npc_gid(const std::string& name) const;
  // A target for the basic attack: stationary, with a combat snapshot (Luau table constructor).
  uint32_t spawn_dummy(const std::string& name, int x, int y, const std::string& unit_src);
  // Combat snapshot of any unit.
  void set_unit(uint32_t gid, const std::string& unit_src);
  std::optional<engine::Point> position_of(uint32_t gid) const;
  // client_id of a ruleset skill definition ("mod:skill/name"), read from the ruleset's data/skills.
  uint16_t skill_client_id(const std::string& skill_id);

  // The last damage the basic attack produced against a unit (nullopt if never hit).
  std::optional<int64_t> last_damage(uint32_t gid) const;
  struct AttackResult {
    bool ok = false;
    std::string error;
    int64_t damage = 0, damage2 = 0;
    int64_t div = 1, flag = 0, type = 0;
    uint64_t draws = 0;
  };
  // Runs the loaded ruleset's basic weapon attack (calc_attack) for two units, drawing from the sim's
  // main RNG stream. Needs a ruleset with tests/support staged (SimConfig::stage_test_support).
  AttackResult basic_attack(uint32_t attacker, uint32_t target);

  // ---- dialogue / legacy runtime state, for tests -----------------------------------------------------
  script_host::DialogueManager& dialogues() { return *dialogues_; }
  struct DialogueInfo {
    bool active = false;
    uint32_t npc = 0;
    uint64_t generation = 0;  // generation of the mod the dialogue runs in
    std::string mod;
  };
  DialogueInfo dialogue_of(uint32_t player_gid) const;
  const std::vector<std::string>& npc_log() const { return npc_log_; }  // @run status lines of finished dialogues
  uint64_t unsupported_skill_casts() const { return unsupported_skill_casts_; }

  // ---- internals shared with sim_server.cpp / sim_player.cpp -----------------------------------------------
  struct Unit {
    uint32_t gid = 0;
    uint8_t kind = 0;  // 0 player, 1 npc, 2 dummy
    std::string name;
    uint16_t job = 0, level = 1;
    uint32_t max_hp = 100, hp = 100;
    std::string unit_src;
    int session = -1;
    engine::EntityId ent{};
    engine::Point dest{};
    bool walking = false;
    std::optional<int64_t> last_damage;
    std::unordered_set<uint32_t> view;  // gids currently visible to this unit (players only are notified)
  };
  struct Session {
    int idx = 0;
    uint32_t aid = 0;
    netproto::Phase phase = netproto::Phase::PreSession;
    bool online = false;
    wire::Bytes in, out;
    netproto::CycleBudget budget{3};
    engine::EntityId ent{};
    uint32_t gid = 0;
    std::string name;
    // dialogue
    bool dlg_active = false;
    uint32_t dlg_npc = 0;
    std::string dlg_token;
    script_host::DialogueRequest dlg_pending;
    std::shared_ptr<script_host::ModSandbox> dlg_sandbox;
    std::string dlg_mod;
    uint64_t dlg_generation = 0;
    std::string disconnect_reason;
    // what the player asked for when it was added
    PlayerSpec spec;
  };

  // client -> server byte stream (what a socket would carry)
  void client_send(int session, wire::ByteView bytes);
  void client_close(int session);  // the client hung up
  // server -> client bytes since the last call
  wire::Bytes client_take(int session);
  Session& session(int idx) { return *sessions_.at(static_cast<size_t>(idx)); }
  const netproto::CodecSet& codecs() const { return *codec_; }
  const netproto::FrameTable& c2s_table() const { return c2s_; }
  const netproto::FrameTable& s2c_table() const { return s2c_; }
  uint32_t server_tick() const { return static_cast<uint32_t>(clock_.now()); }

 private:
  friend class SimPlayer;
  void build_mods();
  void build_world();
  void build_persistence();
  void stage_all();
  void install_hosts(const std::string& mod);
  void ensure_npc_runtime(const std::string& mod);
  bool bus_has_hooks(const std::string& event);

  // server side (sim_server.cpp)
  void process_inbound(Session& s);
  void handle_frame(Session& s, const wire::Packet& p);
  void send(Session& s, const wire::Bytes& frame);
  void drop_session(Session& s, const std::string& why);
  void on_connect(Session& s, const wire::Packet& p);
  void on_actorinit(Session& s);
  void on_walk(Session& s, const wire::Packet& p);
  void on_chat(Session& s, const wire::Packet& p);
  void on_click_npc(Session& s, const wire::Packet& p);
  void on_dialogue_reply(Session& s, const wire::Packet& p);
  void on_act(Session& s, const wire::Packet& p);
  void on_skill(Session& s, const wire::Packet& p);
  void present_dialogue(Session& s, const script_host::DialogueState& st);
  void end_dialogue(Session& s, bool notify_client);
  void tell_runtime_dialogue_ended(Session& s);
  void update_view(uint32_t gid);
  void unit_moved(engine::EntityId e, engine::Point from, engine::Point to);
  wire::Actor actor_of(const Unit& u, bool walking_form) const;
  void send_appear(Session& to, const Unit& who);
  Unit* unit_of_gid(uint32_t gid);
  const Unit* unit_of_gid(uint32_t gid) const;
  Session* session_of_gid(uint32_t gid);
  void broadcast_area(const Unit& center, const wire::Bytes& frame, bool include_self, uint32_t except_gid = 0);
  void after_tick_walk_stops();
  void wake_dialogues();
  Unit& make_unit(uint8_t kind, const std::string& name, int x, int y);
  void drain_logs_for_npc();

  SimConfig cfg_;
  engine::VirtualClock clock_;
  std::unique_ptr<engine::TimerQueue> timers_;
  uint64_t steps_ = 0;
  std::unique_ptr<script_host::RngStreams> streams_;
  DeterministicRng rng_;
  uint64_t trace_hash_ = 1469598103934665603ull;
  uint64_t trace_count_ = 0;
  std::vector<std::string> trace_lines_;

  std::filesystem::path work_dir_;
  bool own_work_dir_ = false;
  std::map<std::string, mods::Manifest> manifests_;       // chosen + their dependencies
  std::map<std::string, std::filesystem::path> real_dir_, stage_dir_;
  std::map<std::string, mods::BundleOptions> bundle_opts_;
  std::vector<std::string> order_;
  std::unique_ptr<mods::IdMap> ids_;
  std::unique_ptr<mods::HostRuntime> host_;
  std::unique_ptr<mods::ModLoader> loader_;
  struct PersistImpl;
  std::unique_ptr<PersistImpl> persist_;
  std::shared_ptr<script_host::StorageBackend> backend_;
  std::vector<std::string> log_capture_;
  std::set<std::string> logged_hosts_;
  std::unordered_map<std::string, bool> bus_cache_;

  // world
  engine::Registry reg_;
  std::unique_ptr<engine::MapGrid> grid_;
  std::unique_ptr<engine::SpatialIndex> spatial_;
  std::unique_ptr<engine::MovementSystem> movement_;
  std::unique_ptr<netproto::CodecSet> codec_;
  netproto::FrameTable c2s_, s2c_;
  std::unique_ptr<script_host::DialogueManager> dialogues_;
  std::vector<std::unique_ptr<Session>> sessions_;
  std::vector<std::unique_ptr<SimPlayer>> players_;
  std::unordered_map<uint32_t, Unit> units_;           // by gid
  std::unordered_map<uint64_t, uint32_t> gid_of_ent_;  // entity key -> gid
  std::map<std::string, uint32_t> npc_by_name_;
  uint32_t next_gid_ = 5'000'000, next_aid_ = 2'000'001;
  std::vector<std::pair<std::string, std::string>> npc_files_;  // path, text
  std::map<std::string, std::string> npc_index_;       // npc name -> file path
  std::vector<std::string> npc_log_;
  std::vector<uint32_t> walkers_;                      // gids of units whose walk has been announced
  std::map<std::string, uint64_t> npc_runtime_gen_;    // mod -> generation the runtime was built in
  uint64_t unsupported_skill_casts_ = 0;
};

}  // namespace tooling::sim
