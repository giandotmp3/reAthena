#pragma once
#include <cstddef>
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

// C++ facade; the loop itself is luau/interpreter.luau hosted by script-host.
class Interpreter {
 public:
  Interpreter(const Program& p, HostApi& host, const CommandRegistry& reg,
              PermissionSet granted, RunLimits limits = {});
  RunOutcome run();                       // until yield / finish. Stub.
  RunOutcome resume(const ResumeInput&);  // Stub.
  void cancel();                          // disconnect cancels dialogue. Stub.
  const std::string& last_error() const { return error_; }
  std::size_t frame_depth() const { return depth_; }

 private:
  const Program& program_;
  HostApi& host_;
  const CommandRegistry& registry_;
  PermissionSet granted_;
  RunLimits limits_;
  std::string error_;
  std::size_t depth_ = 0;
};

}  // namespace legacy_npc
