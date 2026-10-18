#include "gateway/runtime.hpp"
#include "gateway/thread_util.hpp"

namespace gateway {

bool RateLimiter::allow(const std::string& key, Timestamp now) {
  auto it = m_.find(key);
  if (it == m_.end()) {
    if (m_.size() >= max_keys_) {  // bounded: drop expired windows first, then refuse new keys
      for (auto i = m_.begin(); i != m_.end();) i = (now - i->second.start >= window_) ? m_.erase(i) : std::next(i);
      if (m_.size() >= max_keys_) return false;
    }
    m_[key] = W{now, 1};
    return max_ >= 1;
  }
  W& w = it->second;
  if (now - w.start >= window_) { w.start = now; w.n = 0; }
  if (w.n >= max_) return false;
  ++w.n;
  return true;
}

GatewayRuntime::GatewayRuntime(Clock& clock, RuntimeOptions opt)
    : clock_(clock), opt_(opt), rate_(opt.rate_max_attempts, opt.rate_window_s, opt.rate_max_keys) {
  for (int i = 0; i < std::max(1, opt.io_threads); ++i) threads_.emplace_back([this] { worker(); });
}

GatewayRuntime::~GatewayRuntime() { stop(); }

void GatewayRuntime::stop() {
  {
    std::lock_guard<std::mutex> g(mu_);
    if (stop_) return;
    stop_ = true;
  }
  cv_.notify_all();
  for (auto& t : threads_) if (t.joinable()) t.join();
}

bool GatewayRuntime::submit(std::function<void()> task) {
  {
    std::lock_guard<std::mutex> g(mu_);
    if (stop_ || q_.size() >= opt_.io_queue_cap) { ++st_.refused_queue_full; return false; }
    q_.push_back(std::move(task));
    ++st_.submitted;
    st_.max_queued = std::max(st_.max_queued, q_.size());
  }
  cv_.notify_one();
  return true;
}

void GatewayRuntime::submit_login(LoginService& svc, LoginRequest req, std::function<void(const LoginOutcome&)> done) {
  bool allowed;
  {
    std::lock_guard<std::mutex> g(rate_mu_);
    allowed = rate_.allow(req.ip, clock_.now());
  }
  auto reject = [&](Reason why) {
    LoginOutcome o;
    o.refusal_code = refuse::ServerClosed;
    o.reason = why;
    done(o);
  };
  if (!allowed) {
    { std::lock_guard<std::mutex> g(mu_); ++st_.refused_rate; }
    reject(Reason::RateLimited);
    return;
  }
  // The task owns copies; `done` is moved into it. If the queue refuses it, `done` was moved-from, so
  // keep a shared holder to call exactly once.
  auto holder = std::make_shared<std::function<void(const LoginOutcome&)>>(std::move(done));
  const bool ok = submit([&svc, req = std::move(req), holder] { (*holder)(svc.login(req)); });
  if (!ok) {
    LoginOutcome o;
    o.refusal_code = refuse::ServerClosed;
    o.reason = Reason::Overloaded;
    (*holder)(o);
  }
}

void GatewayRuntime::worker() {
  lower_this_thread_priority(opt_.worker_nice);
  for (;;) {
    std::function<void()> t;
    {
      std::unique_lock<std::mutex> lk(mu_);
      cv_.wait(lk, [&] { return stop_ || !q_.empty(); });
      if (q_.empty()) return;
      t = std::move(q_.front());
      q_.pop_front();
    }
    t();
    std::lock_guard<std::mutex> g(mu_);
    ++st_.completed;
  }
}

RuntimeStats GatewayRuntime::stats() const {
  std::lock_guard<std::mutex> g(mu_);
  RuntimeStats s = st_;
  s.queued = q_.size();
  return s;
}

void GatewayRuntime::hold_strand_for(std::chrono::milliseconds ms) {
  std::lock_guard<std::mutex> g(strand_);
  std::this_thread::sleep_for(ms);
}

}  // namespace gateway
