#pragma once
// Coroutine dialogue (CAP-E03): say / choose / input / wait yield to the host
// and resume on a client reply or a timer. A disconnect cancels; idle
// dialogues time out. Each dialogue is a Luau coroutine inside the owning
// mod's lua_State.
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace script_host {

class ModSandbox;
class ReloadManager;

using PlayerId = std::uint64_t;
using ResumeToken = std::string;

struct SayRequest { std::string text; };
struct ChooseRequest { std::vector<std::string> options; };
struct InputRequest { std::string prompt; };
struct WaitRequest { std::uint64_t ms; };
using DialogueRequest = std::variant<SayRequest, ChooseRequest, InputRequest, WaitRequest>;

struct DialogueResponse {
  std::optional<std::int64_t> choice;  // 1-based index into ChooseRequest::options
  std::optional<std::string> text;
};

struct DialogueState {
  ResumeToken token;       // single-use; rotates on every successful resume
  DialogueRequest pending;
  bool finished = false;   // the script returned (or failed); the dialogue is gone
  bool failed = false;
  std::string error;       // set when failed (includes mod, chunk, trace)
};

// Resume rejected (stale/forged token, wrong reply kind, no dialogue).
struct DialogueError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct DialogueConfig {
  std::uint64_t idle_timeout_ms = 300000;  // [dialogue] idle_timeout_ms
  std::uint64_t token_seed = 0;            // 0: seed tokens from std::random_device
  std::uint64_t max_wait_ms = 24ull * 3600 * 1000;
};

struct TimerWake {
  PlayerId player;
  DialogueState state;  // the state after the wait() returned
};

class DialogueManager {
 public:
  explicit DialogueManager(DialogueConfig cfg = {}, ReloadManager* reload = nullptr);
  ~DialogueManager();
  DialogueManager(const DialogueManager&) = delete;
  DialogueManager& operator=(const DialogueManager&) = delete;

  // Without a ReloadManager, dialogues run in sandboxes registered here (or
  // default-configured ones created on first use per mod id).
  void attach(const std::string& mod, std::shared_ptr<ModSandbox> sb);

  // Starts a coroutine for the player; returns the first yielded request.
  // A running dialogue for the player is cancelled first.
  DialogueState start(PlayerId p, const std::string& mod, const std::string& source);
  // Resumes with a client reply. A stale/unknown token, a reply of the wrong
  // kind, an out-of-range choice, or a reply to a wait() is rejected
  // (DialogueError) and leaves the dialogue untouched.
  DialogueState resume(PlayerId p, const ResumeToken& t, const DialogueResponse& r);
  void on_disconnect(PlayerId p);                   // cancels the coroutine
  // Advances monotonic time (never backwards): idle timeouts and wait() wakeups.
  std::vector<TimerWake> advance_time(std::uint64_t now_ms);
  bool has_dialogue(PlayerId p) const;
  std::optional<DialogueState> current(PlayerId p) const;
  std::size_t active_count() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace script_host
