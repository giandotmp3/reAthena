#pragma once
// A small YAML-subset reader for the proto/*.yaml files (spec 4.1, CAP-E02). No third-party library.
//
// Supported (everything the checked-in proto files use, nothing else):
//   - block mappings and block sequences by indentation (spaces only; a sequence may sit at the
//     indentation of its parent key), `- key: value` items that open a mapping
//   - flow sequences `[a, b]` and flow mappings `{ k: v, k2: v2 }`, nested, on one line
//   - scalars: plain, 'single' and "double" quoted (\\ \" \n \t \r \0 \xNN escapes), integers (decimal,
//     0x hexadecimal, optional sign), true/false, null/~ and the empty value
//   - `#` comments (outside quotes, preceded by whitespace or at the line start), a leading `---`
// Not supported and rejected with a line number (never silently misread): tabs in indentation,
// anchors/aliases/tags, block scalars (| >), multi-line flow collections or quoted scalars, multiple
// documents, duplicate mapping keys, floats (a float-looking plain scalar is a string).
// Booleans are only `true`/`false` (YAML 1.1 yes/no/on/off are plain strings here).
#include <cstdint>
#include <string>
#include <vector>

#include "netproto/spec.hpp"

namespace netproto::yaml {

struct Node {
  enum class Kind : std::uint8_t { Null, Bool, Int, Str, Seq, Map };
  Kind kind = Kind::Null;
  bool b = false;
  std::int64_t i = 0;
  std::string s;
  std::vector<Node> seq;
  // Mapping entries in document order. Two parallel vectors (not vector<pair<string, Node>>: Node is still an
  // incomplete type here, which libc++ / clang reject).
  std::vector<std::string> keys;
  std::vector<Node> vals;
  int line = 0;  // 1-based line of the node's first token

  [[nodiscard]] bool is_null() const noexcept { return kind == Kind::Null; }
  [[nodiscard]] const Node* find(const std::string& key) const;  // Map only, nullptr when absent
  [[nodiscard]] const Node& at(const std::string& key) const;    // throws SpecError naming the line
};

/// Parses one document. Throws SpecError("yaml line N: ...").
[[nodiscard]] Node parse(const std::string& text);

/// Canonical one-line JSON of a node (used by the differential test against PyYAML).
[[nodiscard]] std::string to_json(const Node& n);

}  // namespace netproto::yaml
