#pragma once
// One schema (api/*.yaml) generates C++ bindings and engine.d.luau.
#include <string>
#include <vector>

namespace script_host {

struct ApiParam { std::string name, type; };
struct ApiFunction {
  std::string ns, name;
  std::vector<ApiParam> params;
  std::string returns;
};
struct ApiSchema {
  int api_version = 0;
  std::vector<ApiFunction> functions;
};

ApiSchema load_api_schema_yaml(const std::string& yaml_text);
std::string generate_luau_declarations(const ApiSchema& s);  // engine.d.luau
std::string generate_cpp_bindings(const ApiSchema& s);

}  // namespace script_host
