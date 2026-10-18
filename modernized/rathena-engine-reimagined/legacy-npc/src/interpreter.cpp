#include "legacy_npc/interpreter.hpp"

#include <atomic>
#include <variant>

#include "legacy_npc/package.hpp"

namespace legacy_npc {

namespace {

std::atomic<std::uint64_t> g_next_player{1};

const char* grant_name(Permission p) {
  switch (p) {
    case Permission::AtCommand: return "atcommand";
    case Permission::QuerySql: return "query_sql";
    case Permission::SetUnitData: return "setunitdata";
    case Permission::UnboundedLoop: return "unbounded_loop";
  }
  return "";
}

std::string lua_quote(const std::string& s) {
  std::string o = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += static_cast<char>(c); }
    else if (c == '\n') o += "\\n";
    else if (c < 32) o += "\\" + std::to_string(static_cast<int>(c));
    else o += static_cast<char>(c);
  }
  return o + "\"";
}

// Luau source of the program body: the parsed body itself, or the source synthesised from hand-built instructions.
std::string body_expr(const Program& p) {
  if (!p.body.empty()) return long_string(p.body);
  std::string list = "LEGACY.synth.source({";
  std::vector<std::vector<const std::string*>> labels_at(p.code.size() + 1);
  for (auto& [name, idx] : p.labels)
    if (idx >= 0 && static_cast<std::size_t>(idx) <= p.code.size()) labels_at[static_cast<std::size_t>(idx)].push_back(&name);
  for (std::size_t i = 0; i <= p.code.size(); ++i) {
    for (auto* l : labels_at[i]) list += "{op=\"label\",name=" + lua_quote(*l) + "},";
    if (i == p.code.size()) break;
    auto& in = p.code[i];
    switch (in.op) {
      case Op::Command: {
        list += "{op=\"command\",name=" + lua_quote(in.name) + ",args={";
        for (auto& a : in.args) {
          if (a.kind != Expr::Kind::Literal) continue;
          if (auto* n = std::get_if<std::int64_t>(&a.literal)) list += std::to_string(*n) + ",";
          else list += lua_quote(std::get<std::string>(a.literal)) + ",";
        }
        list += "}},";
        break;
      }
      case Op::Goto:
      case Op::Jump: list += "{op=\"goto\",name=" + lua_quote(in.name) + "},"; break;
      case Op::Callsub: list += "{op=\"callsub\",name=" + lua_quote(in.name) + "},"; break;
      case Op::Return: list += "{op=\"return\"},"; break;
      default: list += "{op=\"end\"},"; break;
    }
  }
  return list + "})";
}

Value arg_value(const std::string& s) {
  if (s.size() >= 2 && s[0] == 'i' && s[1] == ':') return static_cast<std::int64_t>(std::atoll(s.c_str() + 2));
  if (s.size() >= 2 && s[0] == 'r' && s[1] == ':') return "&" + s.substr(2);
  return s.size() >= 2 ? s.substr(2) : std::string();
}

}  // namespace

struct Interpreter::Impl {
  std::uint64_t player = g_next_player++;
  script_host::ResumeToken token;
  std::optional<script_host::DialogueRequest> pending;
  bool started = false, cancelled = false, finished = false;
  std::uint64_t now_ms = 0;
};

Interpreter::Interpreter(const Program& p, HostApi& host, const CommandRegistry& reg, PermissionSet granted, RunLimits limits)
    : program_(p), host_(host), registry_(reg), granted_(std::move(granted)), limits_(limits), impl_(std::make_unique<Impl>()) {}

Interpreter::~Interpreter() {
  if (impl_ && impl_->started && !impl_->finished && !impl_->cancelled) shared_host().dialogue().on_disconnect(impl_->player);
}

RunOutcome Interpreter::pump() {
  auto& host = shared_host();
  // commands first: they were executed before whatever stopped the script
  for (auto& r : host.records("@cmd")) {
    if (r.size() < 2) continue;
    std::vector<Value> args;
    for (std::size_t i = 2; i < r.size(); ++i) args.push_back(arg_value(r[i]));
    host_.emit(r[1], args);
  }
  std::string status, etype, ecmd;
  for (auto& r : host.records("@run")) {
    status = r.size() > 1 ? r[1] : "";
    etype = r.size() > 3 ? r[3] : "";
    ecmd = r.size() > 4 ? r[4] : "";
  }
  error_type_ = etype;
  error_command_ = ecmd;
  auto st = host.dialogue().current(impl_->player);
  if (impl_->finished) st.reset();
  if (st && !st->finished) {
    impl_->token = st->token;
    impl_->pending = st->pending;
    return RunOutcome::Yielded;
  }
  impl_->finished = true;
  impl_->pending.reset();
  if (st && st->failed) { error_ = st->error; return RunOutcome::Error; }
  if (status == "end") return RunOutcome::Finished;
  error_ = etype + (ecmd.empty() ? "" : ":" + ecmd);
  if (status == "unsupported" || etype == "UNSUPPORTED") return RunOutcome::Unsupported;
  if (etype == "PERMISSION_DENIED") return RunOutcome::PermissionDenied;
  if (etype == "BUDGET_EXCEEDED") return RunOutcome::BudgetExceeded;
  if (etype == "CANCELLED") return RunOutcome::Cancelled;
  return RunOutcome::Error;
}

RunOutcome Interpreter::run() {
  auto& host = shared_host();
  if (impl_->cancelled) return RunOutcome::Cancelled;
  impl_->started = true;
  std::string grants;
  for (auto g : granted_) grants += std::string("\"") + grant_name(g) + "\",";
  std::string src =
      "local rt = LEGACY.new{ granted = {" + grants + "}, limits = { max_commands = " + std::to_string(limits_.max_commands) +
      ", max_gotos = " + std::to_string(limits_.max_gotos) + ", max_depth = " + std::to_string(limits_.max_call_depth) + " },\n"
      "  host = { dialogue = { say = say, choose = choose, input = input, wait = wait }, log = function() end },\n"
      "  trace = LEGACY.dump.trace }\n"
      "rt:addFile('t.txt', '-\\tscript\\tt\\t-1,{\\n' .. " + body_expr(program_) + " .. '\\n}\\n')\n"
      "LEGACY.emit_result(rt:run('t', 1))\n";
  host.clear_log();
  auto st = host.dialogue().start(impl_->player, kPackageId, src);
  if (st.failed) { impl_->finished = true; error_ = st.error; error_type_ = "HOST_ERROR"; return RunOutcome::Error; }
  return pump();
}

RunOutcome Interpreter::resume(const ResumeInput& in) {
  auto& host = shared_host();
  if (impl_->cancelled || !impl_->started) return RunOutcome::Cancelled;
  if (impl_->finished || !impl_->pending) return RunOutcome::Cancelled;
  script_host::DialogueResponse r;
  host.clear_log();
  if (std::holds_alternative<script_host::ChooseRequest>(*impl_->pending)) {
    r.choice = in.choice;
  } else if (std::holds_alternative<script_host::InputRequest>(*impl_->pending)) {
    if (auto* s = std::get_if<std::string>(&in.input)) r.text = *s;
    else r.text = std::to_string(std::get<std::int64_t>(in.input));
  } else if (auto* w = std::get_if<script_host::WaitRequest>(&*impl_->pending)) {
    // wait() resumes from game time, never from a packet
    impl_->now_ms += w->ms;
    host.dialogue().advance_time(impl_->now_ms);
    return pump();
  }
  host.dialogue().resume(impl_->player, impl_->token, r);
  return pump();
}

void Interpreter::cancel() {
  if (impl_->started && !impl_->finished) shared_host().dialogue().on_disconnect(impl_->player);
  impl_->cancelled = true;
}

}  // namespace legacy_npc
