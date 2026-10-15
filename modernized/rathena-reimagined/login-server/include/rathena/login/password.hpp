#pragma once
// Password-at-rest verification (S6, HITL #1 drop decision 3, migration M6)
// and the bounded hash worker pool (architecture §3/§7).

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "rathena/login/account.hpp"
#include "rathena/login/argon2id.hpp"
#include "rathena/login/config.hpp"

namespace rathena::login {

enum class VerifyStatus { Match, Mismatch, Unsupported };

/// KDF port. Production: Argon2idHasher. Implementations must be safe to call from several
/// HashWorkerPool threads at once.
class PasswordHasher {
 public:
  virtual ~PasswordHasher() = default;
  virtual std::string hash(std::string_view password) = 0;
  virtual VerifyStatus verify(std::string_view encoded, std::string_view password) = 0;
  /// True when a verified hash was made with weaker parameters than the hasher's
  /// current ones and should be replaced on this login (lazy rehash, M6).
  virtual bool needs_rehash(std::string_view /*encoded*/) const { return false; }
};

/// Argon2id (P-H-C reference implementation, see argon2id.hpp). hash() draws a 16-byte
/// salt from the OS CSPRNG and returns a PHC string; verify() is constant-time in the tag
/// and fails closed (Unsupported) on anything that is not a well-formed `$argon2id$v=19$`
/// string within the parameter bounds. Stateless apart from the parameters: thread-safe.
class Argon2idHasher final : public PasswordHasher {
 public:
  /// Throws Argon2Error when `params` are out of range.
  explicit Argon2idHasher(Argon2Params params = {});
  std::string hash(std::string_view password) override;
  VerifyStatus verify(std::string_view encoded, std::string_view password) override;
  /// Weaker = less memory, fewer passes, or a shorter salt/tag than this hasher writes.
  /// A different lane count alone is not weaker. Malformed strings report false (they
  /// never verify, so there is nothing to rehash).
  bool needs_rehash(std::string_view encoded) const override;
  const Argon2Params& params() const { return params_; }

 private:
  Argon2Params params_;
};

/// Hasher parameters from the login configuration (argon2_memory_kib/iterations/parallelism).
Argon2Params argon2_params_from(const LoginConfig& cfg);

struct PasswordCheck {
  bool ok = false;
  /// Lazy rehash: true when the password matched and the row must be rewritten as
  /// Argon2id with the current parameters (a Plain/MD5 row, or an Argon2id row made
  /// with weaker parameters).
  bool needs_rehash = false;
};

/// Scheme-aware verification. Plain rows use BR-LOGIN-002 semantics.
PasswordCheck verify_account_password(PasswordHasher& hasher, const Account& acc,
                                      std::string_view presented);

/// Rewrites `acc` as an Argon2id row of `presented` (call only after a successful check).
/// The caller persists it with AccountRepository::save.
void upgrade_account_password(PasswordHasher& hasher, Account& acc, std::string_view presented);

/// Result of a pooled password check.
struct PasswordOutcome {
  bool ok = false;
  /// Set when the row must be upgraded: the new Argon2id PHC string, already computed on
  /// the worker so the KDF never runs on the IO thread.
  std::optional<std::string> new_argon2id_hash;
  /// The hasher threw (out of memory, CSPRNG failure). The login is refused.
  bool error = false;
};

/// Bounded worker pool with queue-depth back-pressure. A full queue rejects
/// the job; the caller refuses the login with code 3.
class HashWorkerPool {
 public:
  HashWorkerPool(unsigned workers, std::size_t queue_capacity);
  ~HashWorkerPool();
  HashWorkerPool(const HashWorkerPool&) = delete;
  HashWorkerPool& operator=(const HashWorkerPool&) = delete;

  /// Returns false when the queue is full (back-pressure).
  bool try_submit(std::function<void()> job);
  std::size_t queued() const;
  std::size_t capacity() const { return capacity_; }
  std::size_t workers() const { return threads_.size(); }

 private:
  void run();
  std::size_t capacity_;
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> queue_;
  bool stopping_ = false;
  std::vector<std::thread> threads_;
};

/// Runs verify_account_password (and, when due, the Argon2id rehash) for `acc` on the pool
/// and hands the outcome to `done` on the worker thread. Returns false without running
/// anything when the queue is full: the caller refuses the login with
/// kBackPressureRefuseCode. `hasher` must outlive the pool. The presented password copy is
/// wiped once the job has finished with it.
bool submit_password_check(HashWorkerPool& pool, PasswordHasher& hasher, Account acc,
                           std::string presented, std::function<void(PasswordOutcome)> done);

/// Refusal code when the pool rejects a job (architecture §3: "queue full -> refuse code 3").
inline constexpr std::uint32_t kBackPressureRefuseCode = 3;

}  // namespace rathena::login
