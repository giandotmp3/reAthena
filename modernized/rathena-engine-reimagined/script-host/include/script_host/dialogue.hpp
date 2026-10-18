#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace script_host {

using PlayerId = std::uint64_t;
using ResumeToken = std::string;

struct SayRequest { std::string text; };
struct ChooseRequest { std::vector<std::string> options; };
struct InputRequest { std::string prompt; };
struct WaitRequest { std::uint64_t ms; };
using DialogueRequest = std::variant<SayRequest, ChooseRequest, InputRequest, WaitRequest>;

struct DialogueResponse {
  std::optional<std::int64_t> choice;
  std::optional<std::string> text;
};

struct DialogueState {
  ResumeToken token;
  DialogueRequest pending;
  bool finished = false;
};

class DialogueManager {
 public:
  // Starts a coroutine for the player; returns the first yielded request.
  DialogueState start(PlayerId p, const std::string& mod, const std::string& source);
  // Resumes with a client reply. A stale/unknown token is rejected.
  DialogueState resume(PlayerId p, const ResumeToken& t, const DialogueResponse& r);
  void on_disconnect(PlayerId p);                   // cancels the coroutine
  void advance_time(std::uint64_t now_ms);          // idle timeouts and wait() wakeups
  bool has_dialogue(PlayerId p) const;
};

}  // namespace script_host
