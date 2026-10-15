#include "rathena/login/password.hpp"

#include <algorithm>
#include <array>
#include <exception>

#include "rathena/login/auth_rules.hpp"
#include "rathena/login/md5.hpp"

namespace rathena::login {

namespace {

/// Best-effort wipe of a password copy; the volatile store keeps it from being elided.
void wipe(std::string& s) {
  volatile char* p = s.data();
  for (std::size_t i = 0; i < s.size(); ++i) p[i] = 0;
  s.clear();
}

/// A password copy that wipes itself however it dies (job ran, or queue full).
struct WipedString {
  std::string value;
  explicit WipedString(std::string v) : value(std::move(v)) {}
  WipedString(const WipedString&) = default;
  WipedString(WipedString&&) noexcept = default;
  WipedString& operator=(const WipedString&) = delete;
  WipedString& operator=(WipedString&&) = delete;
  ~WipedString() { wipe(value); }
};

}  // namespace

Argon2idHasher::Argon2idHasher(Argon2Params params) : params_(params) {
  validate_argon2_params(params_);
}

std::string Argon2idHasher::hash(std::string_view password) {
  std::array<std::uint8_t, kArgon2SaltBytes> salt{};
  os_random_bytes(salt);
  return argon2id_encode(params_, password, salt);
}

VerifyStatus Argon2idHasher::verify(std::string_view encoded, std::string_view password) {
  std::optional<bool> r;
  try {
    r = argon2id_check(encoded, password);
  } catch (const Argon2Error&) {
    return VerifyStatus::Unsupported;  // fails closed
  }
  if (!r) return VerifyStatus::Unsupported;
  return *r ? VerifyStatus::Match : VerifyStatus::Mismatch;
}

bool Argon2idHasher::needs_rehash(std::string_view encoded) const {
  const auto phc = parse_argon2id_phc(encoded);
  if (!phc) return false;
  return phc->params.memory_kib < params_.memory_kib || phc->params.iterations < params_.iterations ||
         phc->salt.size() < kArgon2SaltBytes || phc->hash.size() < kArgon2HashBytes;
}

Argon2Params argon2_params_from(const LoginConfig& cfg) {
  return {cfg.argon2_memory_kib, cfg.argon2_iterations, cfg.argon2_parallelism};
}

PasswordCheck verify_account_password(PasswordHasher& hasher, const Account& acc,
                                      std::string_view presented) {
  switch (acc.pass_scheme) {
    case PassScheme::Plain: {
      const bool ok = plaintext_password_matches(acc.pass_hash, presented);
      return {ok, ok};
    }
    case PassScheme::Md5: {
      const bool ok = plaintext_password_matches(acc.pass_hash, to_hex(md5(presented)));
      return {ok, ok};
    }
    case PassScheme::Argon2id: {
      const bool ok = hasher.verify(acc.pass_hash, presented) == VerifyStatus::Match;
      return {ok, ok && hasher.needs_rehash(acc.pass_hash)};
    }
  }
  return {};
}

void upgrade_account_password(PasswordHasher& hasher, Account& acc, std::string_view presented) {
  acc.pass_hash = hasher.hash(presented);
  acc.pass_scheme = PassScheme::Argon2id;
}

bool submit_password_check(HashWorkerPool& pool, PasswordHasher& hasher, Account acc,
                           std::string presented, std::function<void(PasswordOutcome)> done) {
  // The job owns the account and the password copy, which is wiped when the job object is
  // destroyed: after it ran, or right away when the queue is full.
  auto job = [&hasher, acc = std::move(acc), pw = WipedString(std::move(presented)),
              done = std::move(done)]() {
    PasswordOutcome out;
    try {
      const auto check = verify_account_password(hasher, acc, pw.value);
      out.ok = check.ok;
      if (check.ok && check.needs_rehash) out.new_argon2id_hash = hasher.hash(pw.value);
    } catch (const std::exception&) {
      out = PasswordOutcome{};
      out.error = true;
    }
    done(std::move(out));
  };
  return pool.try_submit(std::move(job));
}

HashWorkerPool::HashWorkerPool(unsigned workers, std::size_t queue_capacity) : capacity_(queue_capacity) {
  if (workers == 0) workers = std::max(1u, std::thread::hardware_concurrency());
  threads_.reserve(workers);
  for (unsigned i = 0; i < workers; ++i) threads_.emplace_back([this] { run(); });
}

HashWorkerPool::~HashWorkerPool() {
  {
    std::lock_guard lk(mu_);
    stopping_ = true;
  }
  cv_.notify_all();
  for (auto& t : threads_) t.join();
}

bool HashWorkerPool::try_submit(std::function<void()> job) {
  {
    std::lock_guard lk(mu_);
    if (stopping_ || queue_.size() >= capacity_) return false;
    queue_.push_back(std::move(job));
  }
  cv_.notify_one();
  return true;
}

std::size_t HashWorkerPool::queued() const {
  std::lock_guard lk(mu_);
  return queue_.size();
}

void HashWorkerPool::run() {
  for (;;) {
    std::function<void()> job;
    {
      std::unique_lock lk(mu_);
      cv_.wait(lk, [this] { return stopping_ || !queue_.empty(); });
      if (stopping_) return;
      job = std::move(queue_.front());
      queue_.pop_front();
    }
    job();
  }
}

}  // namespace rathena::login
