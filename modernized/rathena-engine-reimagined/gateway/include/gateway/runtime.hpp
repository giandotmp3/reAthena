#pragma once
// Gateway-owned execution resources: a bounded IO task queue with its own worker threads, per-address
// rate limits, and the gateway strand. This is the "own IO thread pool and queues" of architecture
// section 3. Nothing here references the simulation: there is no shared lock, no callback into sim
// state, and a full queue refuses work at admission instead of growing.
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "gateway/role.hpp"
#include "gateway/services.hpp"

namespace gateway {

struct RuntimeOptions {
  int io_threads = 2;
  std::size_t io_queue_cap = 1024;
  int rate_max_attempts = 10;        // per address ...
  int rate_window_s = 10;            // ... per window
  std::size_t rate_max_keys = 65536; // bounded memory: oldest windows are dropped
  int worker_nice = 0;               // > 0: IO workers run at lower OS priority than the simulation thread
};

// Fixed-window counter per key, bounded in memory. Not thread-safe by itself (the runtime locks it).
class RateLimiter {
 public:
  RateLimiter(int max_attempts, int window_s, std::size_t max_keys)
      : max_(max_attempts), window_(window_s), max_keys_(max_keys) {}
  bool allow(const std::string& key, Timestamp now);
  std::size_t keys() const { return m_.size(); }
 private:
  struct W { Timestamp start = 0; int n = 0; };
  int max_, window_;
  std::size_t max_keys_;
  std::map<std::string, W> m_;
};

struct RuntimeStats {
  std::uint64_t submitted = 0, completed = 0, refused_queue_full = 0, refused_rate = 0;
  std::size_t queued = 0, max_queued = 0;
};

class GatewayRuntime {
 public:
  GatewayRuntime(Clock& clock, RuntimeOptions opt);
  ~GatewayRuntime();
  GatewayRuntime(const GatewayRuntime&) = delete;
  GatewayRuntime& operator=(const GatewayRuntime&) = delete;

  /// The mutex to put into GatewayDeps::strand.
  std::mutex* strand() { return &strand_; }
  /// Queue a task for the IO threads; false if the queue is full (the task is not run).
  bool submit(std::function<void()> task);
  /// Admission for a login: rate limit per address, then the bounded queue. `done` is always called
  /// exactly once - on an IO thread, or inline with a refusal when admission failed.
  void submit_login(LoginService& svc, LoginRequest req, std::function<void(const LoginOutcome&)> done);
  RuntimeStats stats() const;
  void stop();
  /// Test hook: holds the strand for `ms` on the calling thread (proves nothing outside the gateway waits for it).
  void hold_strand_for(std::chrono::milliseconds ms);

 private:
  void worker();
  Clock& clock_;
  RuntimeOptions opt_;
  std::mutex strand_;
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> q_;
  bool stop_ = false;
  RuntimeStats st_;
  std::mutex rate_mu_;
  RateLimiter rate_;
  std::vector<std::thread> threads_;
};

}  // namespace gateway
