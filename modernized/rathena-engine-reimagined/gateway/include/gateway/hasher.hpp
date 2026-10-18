#pragma once
// Password hashing for the gateway.
//
// WHAT IS AND IS NOT ARGON2ID (read this before trusting the name `PasswordScheme::Argon2id`):
//  * Argon2idHasher is REAL Argon2id: it loads the system libargon2 (libargon2.so.1) with dlopen and
//    calls argon2id_hash_encoded / argon2id_verify. Nothing of Argon2 is re-implemented here. It exists
//    only when the shared library can be loaded (Argon2idHasher::available()).
//  * Pbkdf2Sha256Hasher is NOT Argon2id. It is the always-available fallback: PBKDF2-HMAC-SHA256
//    (RFC 8018), implemented in crypto.cpp and tested against the RFC 7914 section 11 vectors.
//  * LayeredHasher (what make_default_hasher returns) hashes new passwords with Argon2id when the
//    library is present and with PBKDF2-SHA256 otherwise, verifies either PHC string by its prefix,
//    verifies legacy plaintext/MD5 rows, and reports needs_rehash for legacy rows and for hashes made
//    by a weaker algorithm or weaker parameters than the current configuration (lazy rehash).
//  * algorithm_name() tells which one is active so the operator and the logs can say it.
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "gateway/config.hpp"
#include "gateway/seams.hpp"

namespace gateway {

struct HashParams {
  std::uint32_t pbkdf2_iterations = 600000;
  std::uint32_t argon2_time_cost = 2;
  std::uint32_t argon2_memory_kib = 19456;
  std::uint32_t argon2_parallelism = 1;
  static HashParams from(const GatewayConfig& c) {
    return {c.pbkdf2_iterations, c.argon2_time_cost, c.argon2_memory_kib, c.argon2_parallelism};
  }
};

class Pbkdf2Sha256Hasher : public PasswordHasher {
 public:
  explicit Pbkdf2Sha256Hasher(HashParams p = {}) : p_(p) {}
  std::string hash(const std::string& password) override;
  VerifyResult verify(PasswordScheme scheme, const std::string& stored, const std::string& password) override;
  std::string md5_hex(const std::string& data) override;
  std::string algorithm_name() const override { return "pbkdf2-sha256 (NOT argon2id)"; }
  static bool is_phc(const std::string& stored);
  bool verify_phc(const std::string& stored, const std::string& password, bool* weaker) const;
 private:
  HashParams p_;
};

class Argon2idHasher : public PasswordHasher {
 public:
  explicit Argon2idHasher(HashParams p = {});  // throws std::runtime_error when libargon2 is missing
  static bool available();
  std::string hash(const std::string& password) override;
  VerifyResult verify(PasswordScheme scheme, const std::string& stored, const std::string& password) override;
  std::string md5_hex(const std::string& data) override;
  std::string algorithm_name() const override { return "argon2id (libargon2)"; }
  static bool is_phc(const std::string& stored);
  bool verify_phc(const std::string& stored, const std::string& password, bool* weaker) const;
  /// Raw Argon2id (no secret/associated data) for known-answer tests.
  static std::string raw(std::uint32_t t, std::uint32_t m_kib, std::uint32_t p, const std::string& pw,
                         const std::string& salt, std::size_t out_len);
 private:
  HashParams p_;
};

class LayeredHasher : public PasswordHasher {
 public:
  explicit LayeredHasher(HashParams p = {}, bool allow_argon2 = true);
  std::string hash(const std::string& password) override;
  VerifyResult verify(PasswordScheme scheme, const std::string& stored, const std::string& password) override;
  std::string md5_hex(const std::string& data) override;
  std::string algorithm_name() const override;
  bool uses_argon2id() const { return static_cast<bool>(argon_); }
 private:
  Pbkdf2Sha256Hasher pbkdf2_;
  std::unique_ptr<Argon2idHasher> argon_;
};

std::unique_ptr<LayeredHasher> make_default_hasher(const GatewayConfig& cfg);

// ---- bounded worker pool --------------------------------------------------------------------
struct HashPoolStats {
  std::uint64_t submitted = 0, rejected = 0, completed = 0;
  std::size_t queued = 0, max_queued = 0;
};

// Workers hash on their own threads; run() blocks the caller (a gateway IO thread, never the sim
// thread) until its job is done. The queue of jobs not yet picked up by a worker is bounded: when it
// is full run() throws HashOverloaded at once instead of queueing (SEC-OVERLOAD).
class HashPool {
 public:
  /// `worker_nice` > 0 runs the workers at lower OS priority than the simulation thread.
  HashPool(std::size_t threads, std::size_t queue_cap, int worker_nice = 0);
  ~HashPool();
  HashPool(const HashPool&) = delete;
  HashPool& operator=(const HashPool&) = delete;
  void run(const std::function<void()>& job);
  HashPoolStats stats() const;
  void stop();
 private:
  struct Job { const std::function<void()>* fn; std::mutex* m; std::condition_variable* cv; bool* done; };
  void worker();
  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<Job> q_;
  std::size_t cap_;
  int nice_ = 0;
  bool stop_ = false;
  HashPoolStats st_;
  std::vector<std::thread> threads_;
};

class PooledHasher : public PasswordHasher {
 public:
  PooledHasher(PasswordHasher& inner, HashPool& pool) : inner_(inner), pool_(pool) {}
  std::string hash(const std::string& password) override;
  VerifyResult verify(PasswordScheme scheme, const std::string& stored, const std::string& password) override;
  std::string md5_hex(const std::string& data) override { return inner_.md5_hex(data); }
  std::string algorithm_name() const override { return inner_.algorithm_name() + " on bounded pool"; }
 private:
  PasswordHasher& inner_;
  HashPool& pool_;
};

}  // namespace gateway
