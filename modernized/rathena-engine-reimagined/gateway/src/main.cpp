// Gateway process entry (bring-up skeleton). Parses --role / --config, validates, prints the plan and,
// with --run-for=MS, starts the gateway-owned runtime (IO pool, hash pool, maintenance timers) over the
// in-process seams, so the wiring can be smoke-tested without a socket layer (not part of this unit).
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#include "gateway/hasher.hpp"
#include "gateway/loopback.hpp"
#include "gateway/role.hpp"
#include "gateway/runtime.hpp"

int main(int argc, char** argv) {
  using namespace gateway;
  CliOptions cli = parse_cli(std::vector<std::string>(argv + 1, argv + argc));
  if (!cli.error.empty()) { std::cerr << "error: " << cli.error << "\n"; return 2; }

  GatewayConfig cfg;
  if (!cli.config_path.empty()) {
    std::ifstream f(cli.config_path);
    if (!f) { std::cerr << "error: cannot read config file\n"; return 2; }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string err;
    // [process] is read here; every other section goes to the typed config
    if (auto r = role_from_config_text(ss.str(), &err); !err.empty()) { std::cerr << "error: " << err << "\n"; return 2; }
    else if (r && !cli.role_from_cli) cli.role = *r;
    const std::string text = ss.str();
    if (std::string e = apply_config_text(cfg, text); !e.empty()) { std::cerr << "error: config " << e << "\n"; return 2; }
  }

  const ProcessPlan plan = plan_for(cli.role);
  auto hasher = make_default_hasher(cfg);
  std::cout << "role=" << role_name(cli.role) << " gateway_threads=" << plan.gateway_threads
            << " world_side=" << plan.world_side << " needs_world_peer=" << plan.needs_world_peer
            << " password_hash=\"" << hasher->algorithm_name() << "\"\n";
  if (cli.check_only || cli.run_for_ms <= 0 || !plan.gateway_threads) return 0;

  SystemClock clock;
  RuntimeOptions opt;
  opt.io_threads = cfg.argon2_pool_threads;
  GatewayRuntime rt(clock, opt);
  HashPool pool(static_cast<std::size_t>(cfg.argon2_pool_threads), static_cast<std::size_t>(cfg.argon2_queue_cap));
  std::this_thread::sleep_for(std::chrono::milliseconds(cli.run_for_ms));
  rt.stop();
  pool.stop();
  return 0;
}
