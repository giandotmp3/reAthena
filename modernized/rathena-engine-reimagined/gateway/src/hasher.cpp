#include "gateway/hasher.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <stdexcept>

#include "gateway/crypto.hpp"
#include "gateway/thread_util.hpp"

namespace gateway {

namespace {

constexpr const char* kPbkdf2Prefix = "$pbkdf2-sha256$";
constexpr const char* kArgon2idPrefix = "$argon2id$";

// Legacy rows: plaintext, or lowercase-hex MD5 (use_MD5_passwords). Constant-time compares.
VerifyResult verify_legacy(PasswordScheme scheme, const std::string& stored, const std::string& pw) {
  if (scheme == PasswordScheme::Plaintext) {
    const bool ok = crypto::constant_time_equal(stored, pw);
    return {ok, ok};
  }
  if (scheme == PasswordScheme::Md5) {
    const bool ok = crypto::constant_time_equal(stored, crypto::md5_hex(pw));
    return {ok, ok};
  }
  return {};
}

// "$pbkdf2-sha256$i=N$salt$hash"
struct Pbkdf2Parsed { std::uint32_t iterations = 0; std::string salt, hash; };
bool parse_pbkdf2(const std::string& s, Pbkdf2Parsed& out) {
  if (s.rfind(kPbkdf2Prefix, 0) != 0) return false;
  const std::size_t p1 = std::strlen(kPbkdf2Prefix);
  if (s.compare(p1, 2, "i=") != 0) return false;
  const std::size_t p2 = s.find('$', p1);
  const std::size_t p3 = p2 == std::string::npos ? p2 : s.find('$', p2 + 1);
  if (p2 == std::string::npos || p3 == std::string::npos) return false;
  const std::string it = s.substr(p1 + 2, p2 - p1 - 2);
  if (it.empty() || it.size() > 9) return false;
  std::uint64_t n = 0;
  for (char c : it) { if (c < '0' || c > '9') return false; n = n * 10 + static_cast<std::uint64_t>(c - '0'); }
  if (n == 0 || n > 100000000ULL) return false;
  out.iterations = static_cast<std::uint32_t>(n);
  return crypto::base64_nopad_decode(s.substr(p2 + 1, p3 - p2 - 1), out.salt) &&
         crypto::base64_nopad_decode(s.substr(p3 + 1), out.hash) && !out.salt.empty() && out.hash.size() >= 16;
}

// ---- libargon2 through dlopen (no header, no link-time dependency) ---------------------------
struct Argon2Lib {
  using hash_encoded_t = int (*)(std::uint32_t, std::uint32_t, std::uint32_t, const void*, std::size_t, const void*,
                                 std::size_t, std::size_t, char*, std::size_t);
  using verify_t = int (*)(const char*, const void*, std::size_t);
  using raw_t = int (*)(std::uint32_t, std::uint32_t, std::uint32_t, const void*, std::size_t, const void*,
                        std::size_t, void*, std::size_t);
  using encodedlen_t = std::size_t (*)(std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, int);
  void* handle = nullptr;
  hash_encoded_t hash_encoded = nullptr;
  verify_t verify = nullptr;
  raw_t raw = nullptr;
  encodedlen_t encodedlen = nullptr;
  bool ok() const { return handle && hash_encoded && verify && raw && encodedlen; }
};

const Argon2Lib& argon2_lib() {
  static const Argon2Lib lib = [] {
    Argon2Lib l;
    for (const char* name : {"libargon2.so.1", "libargon2.so"}) {
      l.handle = ::dlopen(name, RTLD_NOW | RTLD_LOCAL);
      if (l.handle) break;
    }
    if (l.handle) {
      l.hash_encoded = reinterpret_cast<Argon2Lib::hash_encoded_t>(::dlsym(l.handle, "argon2id_hash_encoded"));
      l.verify = reinterpret_cast<Argon2Lib::verify_t>(::dlsym(l.handle, "argon2id_verify"));
      l.raw = reinterpret_cast<Argon2Lib::raw_t>(::dlsym(l.handle, "argon2id_hash_raw"));
      l.encodedlen = reinterpret_cast<Argon2Lib::encodedlen_t>(::dlsym(l.handle, "argon2_encodedlen"));
    }
    return l;
  }();
  return lib;
}

// "$argon2id$v=19$m=65536,t=2,p=1$salt$hash": returns the cost parameters, false if malformed.
bool parse_argon2_params(const std::string& s, std::uint32_t& m, std::uint32_t& t, std::uint32_t& p) {
  if (s.rfind(kArgon2idPrefix, 0) != 0) return false;
  const std::size_t at = s.find("m=");
  if (at == std::string::npos) return false;
  unsigned mm = 0, tt = 0, pp = 0;
  if (std::sscanf(s.c_str() + at, "m=%u,t=%u,p=%u", &mm, &tt, &pp) != 3) return false;
  m = mm; t = tt; p = pp;
  return true;
}

}  // namespace

// ---- PBKDF2 ----------------------------------------------------------------------------------
bool Pbkdf2Sha256Hasher::is_phc(const std::string& s) { return s.rfind(kPbkdf2Prefix, 0) == 0; }

std::string Pbkdf2Sha256Hasher::hash(const std::string& password) {
  const std::string salt = crypto::random_bytes(16);
  const std::string dk = crypto::pbkdf2_sha256(password, salt, p_.pbkdf2_iterations, 32);
  return std::string(kPbkdf2Prefix) + "i=" + std::to_string(p_.pbkdf2_iterations) + "$" +
         crypto::base64_nopad_encode(salt) + "$" + crypto::base64_nopad_encode(dk);
}

bool Pbkdf2Sha256Hasher::verify_phc(const std::string& stored, const std::string& password, bool* weaker) const {
  Pbkdf2Parsed pr;
  if (!parse_pbkdf2(stored, pr)) return false;
  const std::string dk = crypto::pbkdf2_sha256(password, pr.salt, pr.iterations, pr.hash.size());
  if (weaker) *weaker = pr.iterations < p_.pbkdf2_iterations;
  return crypto::constant_time_equal(dk, pr.hash);
}

VerifyResult Pbkdf2Sha256Hasher::verify(PasswordScheme scheme, const std::string& stored, const std::string& pw) {
  if (scheme != PasswordScheme::Argon2id) return verify_legacy(scheme, stored, pw);
  bool weaker = false;
  const bool ok = verify_phc(stored, pw, &weaker);
  return {ok, ok && weaker};
}
std::string Pbkdf2Sha256Hasher::md5_hex(const std::string& d) { return crypto::md5_hex(d); }

// ---- Argon2id --------------------------------------------------------------------------------
bool Argon2idHasher::available() { return argon2_lib().ok(); }
bool Argon2idHasher::is_phc(const std::string& s) { return s.rfind(kArgon2idPrefix, 0) == 0; }

Argon2idHasher::Argon2idHasher(HashParams p) : p_(p) {
  if (!available()) throw std::runtime_error("libargon2 is not available on this machine");
}

std::string Argon2idHasher::hash(const std::string& password) {
  const auto& l = argon2_lib();
  const std::string salt = crypto::random_bytes(16);
  const std::size_t len = l.encodedlen(p_.argon2_time_cost, p_.argon2_memory_kib, p_.argon2_parallelism, 16, 32, 2);
  std::string out(len + 1, '\0');
  const int rc = l.hash_encoded(p_.argon2_time_cost, p_.argon2_memory_kib, p_.argon2_parallelism, password.data(),
                                password.size(), salt.data(), salt.size(), 32, out.data(), out.size());
  if (rc != 0) throw std::runtime_error("argon2id_hash_encoded failed");
  out.resize(std::strlen(out.c_str()));
  return out;
}

bool Argon2idHasher::verify_phc(const std::string& stored, const std::string& password, bool* weaker) const {
  std::uint32_t m = 0, t = 0, par = 0;
  if (!parse_argon2_params(stored, m, t, par)) return false;
  const bool ok = argon2_lib().verify(stored.c_str(), password.data(), password.size()) == 0;
  if (weaker) *weaker = m < p_.argon2_memory_kib || t < p_.argon2_time_cost || par < p_.argon2_parallelism;
  return ok;
}

VerifyResult Argon2idHasher::verify(PasswordScheme scheme, const std::string& stored, const std::string& pw) {
  if (scheme != PasswordScheme::Argon2id) return verify_legacy(scheme, stored, pw);
  bool weaker = false;
  const bool ok = verify_phc(stored, pw, &weaker);
  return {ok, ok && weaker};
}
std::string Argon2idHasher::md5_hex(const std::string& d) { return crypto::md5_hex(d); }

std::string Argon2idHasher::raw(std::uint32_t t, std::uint32_t m_kib, std::uint32_t p, const std::string& pw,
                                const std::string& salt, std::size_t out_len) {
  const auto& l = argon2_lib();
  if (!l.ok()) throw std::runtime_error("libargon2 is not available on this machine");
  std::string out(out_len, '\0');
  if (l.raw(t, m_kib, p, pw.data(), pw.size(), salt.data(), salt.size(), out.data(), out_len) != 0)
    throw std::runtime_error("argon2id_hash_raw failed");
  return out;
}

// ---- layered ---------------------------------------------------------------------------------
LayeredHasher::LayeredHasher(HashParams p, bool allow_argon2) : pbkdf2_(p) {
  if (allow_argon2 && Argon2idHasher::available()) argon_ = std::make_unique<Argon2idHasher>(p);
}

std::string LayeredHasher::hash(const std::string& password) {
  return argon_ ? argon_->hash(password) : pbkdf2_.hash(password);
}

VerifyResult LayeredHasher::verify(PasswordScheme scheme, const std::string& stored, const std::string& pw) {
  if (scheme != PasswordScheme::Argon2id) {
    VerifyResult r = verify_legacy(scheme, stored, pw);
    return r;
  }
  if (Argon2idHasher::is_phc(stored)) {
    if (!Argon2idHasher::available()) return {};  // cannot verify an Argon2id row without the library: fail closed
    bool weaker = false;
    const bool ok = argon_ ? argon_->verify_phc(stored, pw, &weaker)
                           : Argon2idHasher(HashParams{}).verify_phc(stored, pw, &weaker);
    return {ok, ok && weaker};
  }
  if (Pbkdf2Sha256Hasher::is_phc(stored)) {
    bool weaker = false;
    const bool ok = pbkdf2_.verify_phc(stored, pw, &weaker);
    // a PBKDF2 row is "weaker" the moment Argon2id is available
    return {ok, ok && (weaker || argon_ != nullptr)};
  }
  return {};
}
std::string LayeredHasher::md5_hex(const std::string& d) { return crypto::md5_hex(d); }
std::string LayeredHasher::algorithm_name() const {
  return argon_ ? argon_->algorithm_name() : pbkdf2_.algorithm_name();
}

std::unique_ptr<LayeredHasher> make_default_hasher(const GatewayConfig& cfg) {
  return std::make_unique<LayeredHasher>(HashParams::from(cfg));
}

// ---- pool ------------------------------------------------------------------------------------
HashPool::HashPool(std::size_t threads, std::size_t queue_cap, int worker_nice) : cap_(queue_cap), nice_(worker_nice) {
  for (std::size_t i = 0; i < std::max<std::size_t>(1, threads); ++i) threads_.emplace_back([this] { worker(); });
}
HashPool::~HashPool() { stop(); }

void HashPool::stop() {
  {
    std::lock_guard<std::mutex> g(mu_);
    if (stop_) return;
    stop_ = true;
  }
  cv_.notify_all();
  for (auto& t : threads_) if (t.joinable()) t.join();
}

void HashPool::run(const std::function<void()>& job) {
  std::mutex m;
  std::condition_variable cv;
  bool done = false;
  {
    std::lock_guard<std::mutex> g(mu_);
    if (stop_ || q_.size() >= cap_) { ++st_.rejected; throw HashOverloaded(); }
    q_.push_back({&job, &m, &cv, &done});
    ++st_.submitted;
    st_.max_queued = std::max(st_.max_queued, q_.size());
  }
  cv_.notify_one();
  std::unique_lock<std::mutex> lk(m);
  cv.wait(lk, [&] { return done; });
}

void HashPool::worker() {
  lower_this_thread_priority(nice_);
  for (;;) {
    Job j;
    {
      std::unique_lock<std::mutex> lk(mu_);
      cv_.wait(lk, [&] { return stop_ || !q_.empty(); });
      if (q_.empty()) return;  // stop requested and drained
      j = q_.front();
      q_.pop_front();
    }
    (*j.fn)();
    {
      std::lock_guard<std::mutex> g(*j.m);
      *j.done = true;
      j.cv->notify_one();   // notify under the lock: the waiter's stack frame outlives this block
    }
    std::lock_guard<std::mutex> g(mu_);
    ++st_.completed;
  }
}

HashPoolStats HashPool::stats() const {
  std::lock_guard<std::mutex> g(mu_);
  HashPoolStats s = st_;
  s.queued = q_.size();
  return s;
}

std::string PooledHasher::hash(const std::string& password) {
  std::string out;
  std::exception_ptr err;
  pool_.run([&] {
    try { out = inner_.hash(password); } catch (...) { err = std::current_exception(); }
  });
  if (err) std::rethrow_exception(err);
  return out;
}

VerifyResult PooledHasher::verify(PasswordScheme scheme, const std::string& stored, const std::string& password) {
  if (scheme != PasswordScheme::Argon2id) return inner_.verify(scheme, stored, password);  // cheap legacy compare
  VerifyResult out;
  std::exception_ptr err;
  pool_.run([&] {
    try { out = inner_.verify(scheme, stored, password); } catch (...) { err = std::current_exception(); }
  });
  if (err) std::rethrow_exception(err);
  return out;
}

}  // namespace gateway
