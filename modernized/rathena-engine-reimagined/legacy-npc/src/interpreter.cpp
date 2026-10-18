#include "legacy_npc/interpreter.hpp"

namespace legacy_npc {

Interpreter::Interpreter(const Program& p, HostApi& host, const CommandRegistry& reg,
                         PermissionSet granted, RunLimits limits)
    : program_(p), host_(host), registry_(reg), granted_(std::move(granted)), limits_(limits) {}

RunOutcome Interpreter::run() { throw NotImplemented("Interpreter::run"); }
RunOutcome Interpreter::resume(const ResumeInput&) { throw NotImplemented("Interpreter::resume"); }
void Interpreter::cancel() { throw NotImplemented("Interpreter::cancel"); }

}  // namespace legacy_npc
