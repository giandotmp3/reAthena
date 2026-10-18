#pragma once
// One schema (api/*.yaml) generates C++ binding tables and engine.d.luau.
// Generated output is deterministic (schema order, no timestamps).
#include <string>
#include <vector>

namespace script_host {

struct ApiParam { std::string name, type; };
struct ApiFunction {
  std::string ns, name;        // ns empty for methods (owner is the ApiType)
  std::vector<ApiParam> params;
  std::string returns = "()";  // Luau type; "()" means no result
  std::string doc;
};
struct ApiField { std::string name, type; };
struct ApiValue {            // a read-only value in a namespace table (e.g. config.legacy_quirks)
  std::string ns, name, type, doc;
};
struct ApiType {
  std::string name;
  bool opaque = false;                 // declare class X end
  std::vector<ApiField> fields;        // data-only record: emitted as `type X = {...}`
  std::vector<ApiFunction> methods;    // host userdata: emitted as `declare class X`
};
struct ApiSchema {
  int api_version = 0;
  std::vector<ApiFunction> functions;  // namespaced functions, in schema order
  std::vector<ApiValue> values;        // namespaced read-only values, in schema order
  std::vector<std::string> namespaces; // every namespace, in schema order
  std::vector<std::string> engine;     // namespaces collected into the `engine` global table (handed to register(engine))
  std::vector<std::string> global_namespaces;  // namespaces whose functions are also globals (say, choose, ...)
  std::vector<ApiType> types;
};

// Throws LoadError (with line numbers for syntax errors) on malformed schemas,
// duplicate names and references to undeclared types.
ApiSchema load_api_schema_yaml(const std::string& yaml_text);
std::string generate_luau_declarations(const ApiSchema& s);  // engine.d.luau
std::string generate_cpp_bindings(const ApiSchema& s);

}  // namespace script_host
