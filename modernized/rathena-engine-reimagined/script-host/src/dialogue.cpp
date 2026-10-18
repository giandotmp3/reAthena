#include "script_host/dialogue.hpp"

#include <cstdio>
#include <random>

#include "script_host/reload.hpp"
#include "script_host/sandbox.hpp"

namespace script_host {

struct DialogueManager::Impl {
  DialogueConfig cfg;
  ReloadManager* reload;
  std::map<std::string, std::shared_ptr<ModSandbox>> sandboxes;
  struct Entry {
    std::string mod;
    std::shared_ptr<ModSandbox> sb;
    std::uint64_t co = 0;
    DialogueRequest pending;
    ResumeToken token;
    std::uint64_t deadline = 0;  // idle deadline (non-wait) or wake time (wait)
    std::optional<ReloadManager::Lease> lease;
  };
  std::map<PlayerId, Entry> by_player;
  std::uint64_t now = 0;
  std::mt19937_64 rng;

  Impl(DialogueConfig c, ReloadManager* r)
      : cfg(c), reload(r), rng(c.token_seed ? c.token_seed : (std::uint64_t{std::random_device{}()} << 32) ^ std::random_device{}()) {}

  ResumeToken new_token() {
    char buf[40];
    std::snprintf(buf, sizeof buf, "%016llx%016llx", static_cast<unsigned long long>(rng()), static_cast<unsigned long long>(rng()));
    return buf;
  }

  void drop(PlayerId p) {
    auto it = by_player.find(p);
    if (it == by_player.end()) return;
    Entry e = std::move(it->second);
    by_player.erase(it);
    if (e.sb) e.sb->co_cancel(e.co);
    if (e.lease && reload) reload->release(e.mod, *e.lease);
  }

  // Applies a coroutine step to the entry (already in the map, or being created).
  DialogueState apply(PlayerId p, Entry& e, const CoStep& step, std::uint64_t base_time) {
    DialogueState st;
    if (step.kind == CoStep::Kind::Yield) {
      e.pending = step.request;
      if (auto* w = std::get_if<WaitRequest>(&e.pending)) {
        if (w->ms > cfg.max_wait_ms) {
          st.failed = st.finished = true;
          st.error = "wait(" + std::to_string(w->ms) + ") exceeds the maximum of " + std::to_string(cfg.max_wait_ms) + " ms";
          return st;
        }
        e.deadline = base_time + w->ms;
      } else {
        e.deadline = base_time + cfg.idle_timeout_ms;
      }
      e.token = new_token();
      st.token = e.token;
      st.pending = e.pending;
      return st;
    }
    st.finished = true;
    if (step.kind == CoStep::Kind::Failed) {
      st.failed = true;
      st.error = step.result.error;
    }
    (void)p;
    return st;
  }
};

DialogueManager::DialogueManager(DialogueConfig cfg, ReloadManager* reload) : impl_(std::make_unique<Impl>(cfg, reload)) {}
DialogueManager::~DialogueManager() {
  std::vector<PlayerId> ids;
  for (auto& [p, e] : impl_->by_player) ids.push_back(p);
  for (auto p : ids) impl_->drop(p);
}

void DialogueManager::attach(const std::string& mod, std::shared_ptr<ModSandbox> sb) { impl_->sandboxes[mod] = std::move(sb); }

DialogueState DialogueManager::start(PlayerId p, const std::string& mod, const std::string& source) {
  Impl& im = *impl_;
  im.drop(p);
  Impl::Entry e;
  e.mod = mod;
  if (im.reload) {
    // The cancel callback runs when the owning generation exceeds its drain timeout.
    auto lease = im.reload->acquire(mod, [this, p] { impl_->drop(p); });
    if (!lease) throw DialogueError("mod '" + mod + "' is not loaded");
    e.sb = lease->sandbox;
    e.lease = std::move(lease);
  } else {
    auto& slot = im.sandboxes[mod];
    if (!slot) {
      ModEnvironmentSpec spec;
      spec.mod_id = mod;
      slot = std::make_shared<ModSandbox>(spec);
    }
    e.sb = slot;
  }
  CoStep step = e.sb->co_start("dialogue", source);
  e.co = step.id;
  DialogueState st = im.apply(p, e, step, im.now);
  if (st.finished) {
    if (step.kind == CoStep::Kind::Yield) e.sb->co_cancel(e.co);  // wait too long
    if (e.lease && im.reload) im.reload->release(mod, *e.lease);
    return st;
  }
  im.by_player[p] = std::move(e);
  return st;
}

DialogueState DialogueManager::resume(PlayerId p, const ResumeToken& t, const DialogueResponse& r) {
  Impl& im = *impl_;
  auto it = im.by_player.find(p);
  if (it == im.by_player.end()) throw DialogueError("no active dialogue for player");
  Impl::Entry& e = it->second;
  // Constant-time compare of fixed-length tokens.
  unsigned diff = static_cast<unsigned>(t.size() ^ e.token.size());
  for (std::size_t i = 0; i < t.size() && i < e.token.size(); ++i) diff |= static_cast<unsigned char>(t[i] ^ e.token[i]);
  if (diff != 0) throw DialogueError("stale or forged resume token");
  DialogueResponse arg;
  if (std::holds_alternative<WaitRequest>(e.pending)) throw DialogueError("wait() resumes on a timer, not on a client reply");
  if (auto* c = std::get_if<ChooseRequest>(&e.pending)) {
    if (!r.choice || *r.choice < 1 || *r.choice > static_cast<std::int64_t>(c->options.size()))
      throw DialogueError("choose reply must be an index in 1.." + std::to_string(c->options.size()));
    arg.choice = r.choice;
  } else if (std::holds_alternative<InputRequest>(e.pending)) {
    if (!r.text) throw DialogueError("input reply must carry text");
    if (r.text->size() > 4096) throw DialogueError("input reply too long");
    arg.text = r.text;
  }
  CoStep step = e.sb->co_resume(e.co, arg);
  DialogueState st = im.apply(p, e, step, im.now);
  if (st.finished) {
    if (step.kind == CoStep::Kind::Yield) e.sb->co_cancel(e.co);
    auto lease = e.lease;
    std::string mod = e.mod;
    im.by_player.erase(it);
    if (lease && im.reload) im.reload->release(mod, *lease);
  }
  return st;
}

void DialogueManager::on_disconnect(PlayerId p) { impl_->drop(p); }

std::vector<TimerWake> DialogueManager::advance_time(std::uint64_t now_ms) {
  Impl& im = *impl_;
  std::vector<TimerWake> out;
  if (now_ms > im.now) im.now = now_ms;
  std::vector<PlayerId> ids;
  for (auto& [p, e] : im.by_player) ids.push_back(p);
  for (PlayerId p : ids) {
    for (int guard = 0; guard < 1000; ++guard) {
      auto it = im.by_player.find(p);
      if (it == im.by_player.end()) break;
      Impl::Entry& e = it->second;
      if (e.deadline > im.now) break;
      if (std::holds_alternative<WaitRequest>(e.pending)) {
        std::uint64_t due = e.deadline;
        CoStep step = e.sb->co_resume(e.co, {});
        DialogueState st = im.apply(p, e, step, due);
        if (st.finished) {
          if (step.kind == CoStep::Kind::Yield) e.sb->co_cancel(e.co);
          auto lease = e.lease;
          std::string mod = e.mod;
          im.by_player.erase(it);
          if (lease && im.reload) im.reload->release(mod, *lease);
          out.push_back({p, st});
          break;
        }
        out.push_back({p, st});
      } else {
        DialogueState st;
        st.finished = st.failed = true;
        st.error = "dialogue idle timeout";
        im.drop(p);
        out.push_back({p, st});
        break;
      }
    }
  }
  return out;
}

bool DialogueManager::has_dialogue(PlayerId p) const { return impl_->by_player.count(p) != 0; }
std::size_t DialogueManager::active_count() const { return impl_->by_player.size(); }

std::optional<DialogueState> DialogueManager::current(PlayerId p) const {
  auto it = impl_->by_player.find(p);
  if (it == impl_->by_player.end()) return std::nullopt;
  DialogueState st;
  st.token = it->second.token;
  st.pending = it->second.pending;
  return st;
}

}  // namespace script_host
