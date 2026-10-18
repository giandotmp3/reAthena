#pragma once
// A deliberately small YAML subset reader, enough for api/*.yaml: block
// mappings and sequences by indentation, flow {a: b} / [a, b] collections,
// plain and quoted scalars, '#' comments. No anchors, tags, multi-document
// streams, block scalars or tabs. Anything else is a LoadError with a line number.
#include <string>
#include <utility>
#include <vector>

namespace script_host::yaml {

struct Node {
  enum class Kind { Null, Scalar, Seq, Map } kind = Kind::Null;
  std::string scalar;
  bool quoted = false;
  std::vector<Node> seq;
  std::vector<std::pair<std::string, Node>> map;  // insertion order preserved

  const Node* find(const std::string& key) const;
  bool is_map() const { return kind == Kind::Map; }
  bool is_seq() const { return kind == Kind::Seq; }
  bool is_scalar() const { return kind == Kind::Scalar; }
};

Node parse(const std::string& text);  // throws LoadError

}  // namespace script_host::yaml
