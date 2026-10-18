#include "script_host/api_schema.hpp"

#include "script_host/not_implemented.hpp"

namespace script_host {

ApiSchema load_api_schema_yaml(const std::string&) { not_implemented("load_api_schema_yaml"); }
std::string generate_luau_declarations(const ApiSchema&) { not_implemented("generate_luau_declarations"); }
std::string generate_cpp_bindings(const ApiSchema&) { not_implemented("generate_cpp_bindings"); }

}  // namespace script_host
