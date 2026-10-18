#pragma once
// Shared helpers of the rathena-sim tests.
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "harness.hpp"
#include "tooling/sim_player.hpp"
#include "tooling/sim_world.hpp"

namespace simtest {

struct TempDir {
  std::filesystem::path path;
  explicit TempDir(const std::string& name) {
    path = std::filesystem::temp_directory_path() / ("tooling-test-" + name + "-" + std::to_string(::getpid()));
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }
  void write(const std::string& rel, const std::string& text) const {
    std::filesystem::create_directories((path / rel).parent_path());
    std::ofstream(path / rel) << text;
  }
};

// A package with a manifest and scripts/init.luau.
inline void write_mod(const TempDir& d, const std::string& id, const std::string& extra, const std::string& init) {
  d.write(id + "/mod.toml", "id = \"" + id + "\"\nversion = \"1.0.0\"\nengine_api = \"^1\"\n" + extra);
  d.write(id + "/scripts/init.luau", init);
}


inline void need_luau() {
  if (!mods::HostRuntime::vm_available()) SKIP_TEST("built without the Luau VM (set RATHENA_LUAU_ROOT / RATHENA_LUAU_LIB_DIR)");
}
inline std::filesystem::path engine_root() { return std::filesystem::path(TOOLING_SOURCE_DIR) / ".."; }

// A sim with the default mods unless `mods` says otherwise.
inline std::unique_ptr<tooling::sim::Sim> make_sim(std::vector<std::string> mods = {}, uint64_t seed = 1,
                                                   const std::function<void(tooling::sim::SimConfig&)>& tweak = {}) {
  need_luau();
  tooling::sim::SimConfig c;
  c.seed = seed;
  c.mods = std::move(mods);
  c.map_w = c.map_h = 320;
  if (tweak) tweak(c);
  return std::make_unique<tooling::sim::Sim>(std::move(c));
}

inline tooling::sim::SimPlayer& add(tooling::sim::Sim& s, const std::string& name, int x, int y) {
  tooling::sim::Sim::PlayerSpec p;
  p.name = name;
  p.x = x;
  p.y = y;
  return s.add_player(p);
}

}  // namespace simtest
