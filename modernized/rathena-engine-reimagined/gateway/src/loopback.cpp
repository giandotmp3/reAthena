#include "gateway/loopback.hpp"

namespace gateway {

void LoopbackWorldBus::post(Message m) {
  {
    std::lock_guard<std::mutex> g(mu_);
    if (q_.size() < cap_) { q_.push_back(std::move(m)); return; }
  }
  refused_.fetch_add(1);
  throw StoreUnavailable("world mailbox full");
}

void LoopbackWorldBus::kick(AccountId aid) {
  Message m;
  m.kind = Message::Kind::Kick;
  m.account_id = aid;
  post(std::move(m));
}
void LoopbackWorldBus::broadcast(const AccountEvent& ev) {
  Message m;
  m.kind = Message::Kind::Account;
  m.account_id = ev.account_id;
  m.event = ev;
  post(std::move(m));
}
void LoopbackWorldBus::purge_character_data(CharId id, const std::vector<std::string>& plan) {
  Message m;
  m.kind = Message::Kind::Purge;
  m.char_id = id;
  m.plan = plan;
  post(std::move(m));
}

MembershipInfo LoopbackWorldBus::membership(CharId id) const {
  std::shared_ptr<const std::map<CharId, MembershipInfo>> s;
  {
    std::lock_guard<std::mutex> g(snap_mu_);
    s = snap_;
  }
  auto it = s->find(id);
  return it == s->end() ? MembershipInfo{} : it->second;
}

void LoopbackWorldBus::publish_membership(std::map<CharId, MembershipInfo> snapshot) {
  auto p = std::make_shared<const std::map<CharId, MembershipInfo>>(std::move(snapshot));
  std::lock_guard<std::mutex> g(snap_mu_);
  snap_ = std::move(p);
}

std::size_t LoopbackWorldBus::drain(std::size_t max, const std::function<void(const Message&)>& sink) {
  std::size_t n = 0;
  while (n < max) {
    Message m;
    {
      std::lock_guard<std::mutex> g(mu_);
      if (q_.empty()) break;
      m = std::move(q_.front());
      q_.pop_front();
    }
    sink(m);
    ++n;
  }
  return n;
}

std::size_t LoopbackWorldBus::queued() const {
  std::lock_guard<std::mutex> g(mu_);
  return q_.size();
}

}  // namespace gateway
