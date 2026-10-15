#include "rathena/login/password.hpp"

#include <algorithm>
#include <stdexcept>

#include "rathena/login/auth_rules.hpp"
#include "rathena/login/md5.hpp"

namespace rathena::login {

std::string Argon2idHasher::hash(std::string_view /*password*/) {
  // TODO(M6): crypto_pwhash_str with OPSLIMIT_MODERATE/MEMLIMIT_MODERATE once
  // libsodium is vendored. Throwing keeps a stub from ever persisting a weak hash.
  throw std::logic_error("Argon2idHasher::hash: libsodium binding not implemented");
}

VerifyStatus Argon2idHasher::verify(std::string_view /*encoded*/, std::string_view /*password*/) {
  return VerifyStatus::Unsupported;  // fails closed
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
    case PassScheme::Argon2id:
      return {hasher.verify(acc.pass_hash, presented) == VerifyStatus::Match, false};
  }
  return {};
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
