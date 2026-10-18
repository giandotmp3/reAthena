#pragma once
// The in-process transport of the WorldBus seam. Gateway threads post messages into a bounded mailbox
// and read a membership snapshot the world publishes; the simulation thread drains the mailbox at its
// own pace. The gateway never waits for the simulation and the simulation never takes a gateway lock:
// the only shared state is the mailbox, whose critical sections are O(1) pushes and pops (a bounded
// std::deque under a short-lived mutex), plus two atomics. A full mailbox refuses the message
// (StoreUnavailable) instead of growing or blocking.
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

#include "gateway/seams.hpp"

namespace gateway {

class LoopbackWorldBus : public WorldBus {
 public:
  struct Message {
    enum class Kind { Kick, Account, Purge } kind = Kind::Kick;
    AccountId account_id = 0;
    AccountEvent event{AccountEventKind::Ban, 0, 0};
    CharId char_id = 0;
    std::vector<std::string> plan;
  };
  explicit LoopbackWorldBus(std::size_t capacity = 4096) : cap_(capacity) {}

  // ---- gateway side ----
  void kick(AccountId aid) override;
  void broadcast(const AccountEvent& ev) override;
  MembershipInfo membership(CharId id) const override;
  void purge_character_data(CharId id, const std::vector<std::string>& plan) override;
  bool available() const override { return up_.load(std::memory_order_acquire); }

  // ---- world side (simulation thread) ----
  std::size_t drain(std::size_t max, const std::function<void(const Message&)>& sink);
  void publish_membership(std::map<CharId, MembershipInfo> snapshot);
  void set_available(bool up) { up_.store(up, std::memory_order_release); }

  std::size_t queued() const;
  std::uint64_t refused() const { return refused_.load(); }

 private:
  void post(Message m);
  std::size_t cap_;
  mutable std::mutex mu_;
  std::deque<Message> q_;
  std::atomic<bool> up_{true};
  std::atomic<std::uint64_t> refused_{0};
  mutable std::mutex snap_mu_;
  std::shared_ptr<const std::map<CharId, MembershipInfo>> snap_ = std::make_shared<const std::map<CharId, MembershipInfo>>();
};

}  // namespace gateway
