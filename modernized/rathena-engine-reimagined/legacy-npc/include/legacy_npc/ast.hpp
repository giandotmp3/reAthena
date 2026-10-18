#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "legacy_npc/types.hpp"

namespace legacy_npc {

enum class DeclKind { Script, Function, Duplicate, Shop, Warp, Monster, Mapflag };

struct Expr {
  enum class Kind { Literal, Var, Call, Binary, Unary } kind = Kind::Literal;
  std::string text;  // operator, command or variable text
  Value literal;
  std::vector<Expr> children;
};

// Lowered flat statement list: control flow is pc assignment, not recursion.
enum class Op { Nop, Command, Assign, Jump, JumpIfFalse, Goto, Callsub, Return, Switch, End };

struct Instr {
  Op op = Op::Nop;
  std::string name;         // command name or target label
  std::vector<Expr> args;
  std::int32_t target = -1; // resolved pc
  std::uint32_t line = 0;
};

struct Program {
  std::vector<Instr> code;
  std::map<std::string, std::int32_t> labels;
  std::uint64_t source_hash = 0;
};

struct Npc {
  DeclKind kind = DeclKind::Script;
  std::string name;
  std::string map;
  int x = 0, y = 0, dir = 0;
  std::string duplicate_of;  // for Duplicate
  Program program;
};

struct Diagnostic {
  std::uint32_t line = 0;
  std::string message;
};

struct ParsedFile {
  std::string file_name;
  std::uint64_t source_hash = 0;
  std::vector<Npc> npcs;
  std::vector<Diagnostic> diagnostics;
};

}  // namespace legacy_npc
