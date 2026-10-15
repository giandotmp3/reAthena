#pragma once
// Password-at-rest verification (S6, HITL #1 drop decision 3, migration M6)
// and the bounded hash worker pool (architecture §3/§7).

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "rathena/login/account.hpp"

namespace rathena::login {

enum class VerifyStatus { Match, Mismatch, Unsupported };

/// KDF port. Argon2id in production (libsodium crypto_pwhash_str_verify).
class PasswordHasher {
 public:
  virtual ~PasswordHasher() = default;
  virtual std::string hash(std::string_view password) = 0;
  virtual VerifyStatus verify(std::string_view encoded, std::string_view password) = 0;
};

/// Argon2id binding. STUB: libsodium is not vendored yet; every call reports
/// Unsupported so the login fails closed rather than accepting.
class Argon2idHasher final : public PasswordHasher {
 public:
  std::string hash(std::string_view password) override;
  VerifyStatus verify(std::string_view encoded, std::string_view password) override;
};

struct PasswordCheck {
  bool ok = false;
  /// Lazy rehash: true when a Plain/MD5 row verified and must be upgraded to Argon2id.
  bool needs_rehash = false;
};

/// Scheme-aware verification. Plain rows use BR-LOGIN-002 semantics.
PasswordCheck verify_account_password(PasswordHasher& hasher, const Account& acc,
                                      std::string_view presented);

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

 private:
  void run();
  std::size_t capacity_;
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> queue_;
  bool stopping_ = false;
  std::vector<std::thread> threads_;
};

/// Refusal code when the pool rejects a job (architecture §3: "queue full -> refuse code 3").
inline constexpr std::uint32_t kBackPressureRefuseCode = 3;

}  // namespace rathena::login
