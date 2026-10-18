#pragma once
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "legacy_npc/ast.hpp"
#include "legacy_npc/commands.hpp"
#include "legacy_npc/variables.hpp"

namespace legacy_npc {

struct RunLimits {
  std::size_t max_commands = 65535;  // legacy check_cmdcount
  std::size_t max_gotos = 2048;      // legacy check_gotocount
  std::size_t max_call_depth = 32768;  // heap frames: a typed CALL_DEPTH safety net, not a Lua limit
};

enum class RunOutcome {
  Finished, Yielded, BudgetExceeded, Unsupported, PermissionDenied, Cancelled, Error
};

class HostApi {
 public:
  virtual ~HostApi() = default;
  virtual VariableStore& vars() = 0;
  virtual void emit(std::string_view command, const std::vector<Value>& args) = 0;
};

struct ResumeInput {
  std::int64_t choice = 0;
  Value input;
};

// C++ facade over the Luau interpreter (luau/interp.luau, driven by luau/runtime.luau) running as a script-host dialogue coroutine:
// run() starts the script for a private player, blocking commands (next, close2, menu, select, prompt, input, sleep, sleep2,
// progressbar) leave run()/resume() as Yielded, cancel() is the disconnect. Every command the script executes is reported to
// HostApi::emit with its evaluated arguments (strings as std::string, numbers as int64, variable references as "&name").
class Interpreter {
 public:
  Interpreter(const Program& p, HostApi& host, const CommandRegistry& reg,
              PermissionSet granted, RunLimits limits = {});
  ~Interpreter();
  Interpreter(const Interpreter&) = delete;
  Interpreter& operator=(const Interpreter&) = delete;
  RunOutcome run();                       // until yield / finish
  RunOutcome resume(const ResumeInput&);  // answers the pending request (Yielded again, Finished, ...)
  void cancel();                          // disconnect cancels the dialogue; later resume() reports Cancelled
  const std::string& last_error() const { return error_; }
  std::size_t frame_depth() const { return depth_; }
  // typed error of the last outcome ("UNSUPPORTED", "PERMISSION_DENIED", "BUDGET_EXCEEDED", "CALL_DEPTH", ...) and its command, if any
  const std::string& last_error_type() const { return error_type_; }
  const std::string& last_error_command() const { return error_command_; }

 private:
  struct Impl;
  RunOutcome pump();  // reads the log records written by the last start/resume and maps the dialogue state
  const Program& program_;
  HostApi& host_;
  const CommandRegistry& registry_;
  PermissionSet granted_;
  RunLimits limits_;
  std::string error_, error_type_, error_command_;
  std::size_t depth_ = 0;
  std::unique_ptr<Impl> impl_;
};

}  // namespace legacy_npc
