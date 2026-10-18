#include "script_host/api_schema.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>

#include "script_host/not_implemented.hpp"
#include "script_host/yaml_lite.hpp"

namespace script_host {

namespace {

const std::set<std::string> kBuiltinTypes = {"string", "number", "boolean", "any", "nil", "unknown", "never", "buffer", "thread"};

std::string need_scalar(const yaml::Node& n, const std::string& what) {
  if (!n.is_scalar()) throw LoadError("api schema: " + what + " must be a scalar");
  return n.scalar;
}
const yaml::Node& need(const yaml::Node& m, const std::string& key, const std::string& ctx) {
  if (!m.is_map()) throw LoadError("api schema: " + ctx + " must be a mapping");
  const yaml::Node* n = m.find(key);
  if (!n) throw LoadError("api schema: " + ctx + " is missing '" + key + "'");
  return *n;
}

ApiFunction read_function(const yaml::Node& n, const std::string& ns, const std::string& ctx) {
  ApiFunction f;
  f.ns = ns;
  f.name = need_scalar(need(n, "name", ctx), ctx + ".name");
  f.returns = "()";
  if (auto* r = n.find("returns")) f.returns = need_scalar(*r, ctx + ".returns");
  if (auto* d = n.find("doc")) f.doc = need_scalar(*d, ctx + ".doc");
  if (auto* p = n.find("params")) {
    if (!p->is_seq() && p->kind != yaml::Node::Kind::Null) throw LoadError("api schema: " + ctx + ".params must be a list");
    for (auto& e : p->seq)
      f.params.push_back({need_scalar(need(e, "name", ctx + ".params"), ctx + ".params.name"),
                          need_scalar(need(e, "type", ctx + ".params"), ctx + ".params.type")});
  }
  return f;
}

// Identifiers in a Luau type string that must resolve to declared types.
std::vector<std::string> type_identifiers(const std::string& t) {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < t.size();) {
    if (std::isalpha(static_cast<unsigned char>(t[i])) || t[i] == '_') {
      std::size_t j = i;
      while (j < t.size() && (std::isalnum(static_cast<unsigned char>(t[j])) || t[j] == '_')) ++j;
      // A name followed by ':' is a parameter label inside a function type.
      std::size_t k = j;
      while (k < t.size() && t[k] == ' ') ++k;
      if (!(k < t.size() && t[k] == ':')) out.push_back(t.substr(i, j - i));
      i = j;
    } else {
      ++i;
    }
  }
  return out;
}

std::string param_list(const std::vector<ApiParam>& ps, bool self) {
  std::string s = self ? "self" : "";
  for (auto& p : ps) s += (s.empty() ? "" : ", ") + p.name + ": " + p.type;
  return s;
}
std::string fn_type(const ApiFunction& f) { return "(" + param_list(f.params, false) + ") -> " + f.returns; }

}  // namespace

ApiSchema load_api_schema_yaml(const std::string& text) {
  yaml::Node root = yaml::parse(text);
  ApiSchema s;
  if (!root.is_map()) throw LoadError("api schema: top level must be a mapping");
  s.api_version = std::stoi(need_scalar(need(root, "api_version", "schema"), "api_version"));
  if (s.api_version != 1) throw LoadError("api schema: unsupported api_version " + std::to_string(s.api_version));

  if (auto* types = root.find("types")) {
    if (!types->is_map()) throw LoadError("api schema: 'types' must be a mapping");
    for (auto& [name, t] : types->map) {
      ApiType ty;
      ty.name = name;
      if (auto* o = t.find("opaque")) ty.opaque = need_scalar(*o, name + ".opaque") == "true";
      if (auto* fl = t.find("fields"))
        for (auto& e : fl->seq)
          ty.fields.push_back({need_scalar(need(e, "name", name + ".fields"), "name"), need_scalar(need(e, "type", name + ".fields"), "type")});
      if (auto* ms = t.find("methods"))
        for (auto& e : ms->seq) ty.methods.push_back(read_function(e, "", name + ".methods"));
      if (!ty.opaque && ty.fields.empty() && ty.methods.empty()) throw LoadError("api schema: type '" + name + "' declares nothing");
      if (!ty.fields.empty() && !ty.methods.empty()) throw LoadError("api schema: type '" + name + "' mixes fields and methods");
      s.types.push_back(std::move(ty));
    }
  }
  if (auto* nss = root.find("namespaces")) {
    if (!nss->is_map()) throw LoadError("api schema: 'namespaces' must be a mapping");
    for (auto& [ns, body] : nss->map) {
      if (auto* g = body.find("also_global"); g && need_scalar(*g, ns + ".also_global") == "true") s.global_namespaces.push_back(ns);
      s.namespaces.push_back(ns);
      auto* fns = body.find("functions");
      auto* vals = body.find("values");
      if ((!fns || !fns->is_seq()) && (!vals || !vals->is_seq()))
        throw LoadError("api schema: namespace '" + ns + "' needs a 'functions' list (or a 'values' list)");
      if (fns && fns->is_seq())
        for (auto& e : fns->seq) s.functions.push_back(read_function(e, ns, ns + ".functions"));
      if (vals && vals->is_seq())
        for (auto& e : vals->seq) {
          ApiValue v;
          v.ns = ns;
          v.name = need_scalar(need(e, "name", ns + ".values"), ns + ".values.name");
          v.type = need_scalar(need(e, "type", ns + ".values"), ns + ".values.type");
          if (auto* d = e.find("doc")) v.doc = need_scalar(*d, ns + ".values.doc");
          s.values.push_back(std::move(v));
        }
    }
  }
  if (auto* eng = root.find("engine")) {
    if (!eng->is_seq()) throw LoadError("api schema: 'engine' must be a list of namespace names");
    for (auto& e : eng->seq) s.engine.push_back(need_scalar(e, "engine entry"));
  }

  // Cross-checks: unique names, declared types.
  std::set<std::string> declared(kBuiltinTypes);
  for (auto& t : s.types)
    if (!declared.insert(t.name).second) throw LoadError("api schema: duplicate type '" + t.name + "'");
  std::set<std::string> seen;
  auto check_fn = [&](const ApiFunction& f, const std::string& owner) {
    for (auto& id : type_identifiers(f.returns)) if (!declared.count(id)) throw LoadError("api schema: " + owner + f.name + " returns undeclared type '" + id + "'");
    for (auto& p : f.params)
      for (auto& id : type_identifiers(p.type)) if (!declared.count(id)) throw LoadError("api schema: " + owner + f.name + " param '" + p.name + "' uses undeclared type '" + id + "'");
  };
  for (auto& f : s.functions) {
    if (!seen.insert(f.ns + "." + f.name).second) throw LoadError("api schema: duplicate function " + f.ns + "." + f.name);
    check_fn(f, f.ns + ".");
  }
  for (auto& t : s.types) {
    std::set<std::string> ms;
    for (auto& m : t.methods) {
      if (!ms.insert(m.name).second) throw LoadError("api schema: duplicate method " + t.name + ":" + m.name);
      check_fn(m, t.name + ":");
    }
    for (auto& f : t.fields)
      for (auto& id : type_identifiers(f.type)) if (!declared.count(id)) throw LoadError("api schema: " + t.name + "." + f.name + " uses undeclared type '" + id + "'");
  }
  std::set<std::string> seen_values;
  for (auto& v : s.values) {
    if (!seen_values.insert(v.ns + "." + v.name).second) throw LoadError("api schema: duplicate value " + v.ns + "." + v.name);
    if (seen.count(v.ns + "." + v.name)) throw LoadError("api schema: " + v.ns + "." + v.name + " is both a function and a value");
    for (auto& id : type_identifiers(v.type)) if (!declared.count(id)) throw LoadError("api schema: value " + v.ns + "." + v.name + " uses undeclared type '" + id + "'");
  }
  for (auto& e : s.engine)
    if (std::find(s.namespaces.begin(), s.namespaces.end(), e) == s.namespaces.end())
      throw LoadError("api schema: engine lists unknown namespace '" + e + "'");
  for (auto& g : s.global_namespaces)
    if (std::none_of(s.functions.begin(), s.functions.end(), [&](const ApiFunction& f) { return f.ns == g; }))
      throw LoadError("api schema: also_global namespace '" + g + "' has no functions");
  return s;
}

std::string generate_luau_declarations(const ApiSchema& s) {
  std::ostringstream o;
  o << "-- GENERATED by gen_api from api/engine.yaml (api_version " << s.api_version << "). Do not edit.\n"
    << "-- Declarations for the globals the script host injects into every mod environment.\n"
    << "-- Numbers are doubles; int64 values above 2^53 only live inside the native ctx.\n";
  for (auto& t : s.types) {
    o << "\n";
    if (t.opaque) {
      o << "declare class " << t.name << "\nend\n";
    } else if (!t.fields.empty()) {
      o << "export type " << t.name << " = {\n";
      for (auto& f : t.fields) o << "  " << f.name << ": " << f.type << ",\n";
      o << "}\n";
    } else {
      o << "declare class " << t.name << "\n";
      for (auto& m : t.methods) {
        if (!m.doc.empty()) o << "  -- " << m.doc << "\n";
        o << "  function " << m.name << "(" << param_list(m.params, true) << ")" << (m.returns == "()" ? "" : ": " + m.returns) << "\n";
      }
      o << "end\n";
    }
  }
  for (auto& ns : s.namespaces) {
    o << "\ndeclare " << ns << ": {\n";
    for (auto& f : s.functions) {
      if (f.ns != ns) continue;
      if (!f.doc.empty()) o << "  -- " << f.doc << "\n";
      o << "  " << f.name << ": " << fn_type(f) << ",\n";
    }
    for (auto& v : s.values) {
      if (v.ns != ns) continue;
      if (!v.doc.empty()) o << "  -- " << v.doc << "\n";
      o << "  " << v.name << ": " << v.type << ",\n";
    }
    o << "}\n";
    if (std::find(s.global_namespaces.begin(), s.global_namespaces.end(), ns) != s.global_namespaces.end())
      for (auto& f : s.functions)
        if (f.ns == ns) o << "declare function " << f.name << "(" << param_list(f.params, false) << ")" << (f.returns == "()" ? "" : ": " + f.returns) << "\n";
  }
  if (!s.engine.empty()) {
    o << "\n-- the table a package's register(engine) receives (a global `engine` exists when the loader asks for it)\nexport type Engine = {\n";
    for (auto& e : s.engine) o << "  " << e << ": typeof(" << e << "),\n";
    o << "}\n";
  }
  return o.str();
}

std::string generate_cpp_bindings(const ApiSchema& s) {
  std::ostringstream o;
  auto q = [](const std::string& x) { return "\"" + x + "\""; };
  o << "// GENERATED by gen_api from api/engine.yaml. Do not edit.\n#pragma once\n#include <cstddef>\n\n"
    << "namespace script_host::api {\n\n"
    << "inline constexpr int kApiVersion = " << s.api_version << ";\n\n"
    << "struct FunctionSig { const char* ns; const char* name; int arity; const char* returns; };\n"
    << "inline constexpr FunctionSig kFunctions[] = {\n";
  for (auto& f : s.functions) o << "    {" << q(f.ns) << ", " << q(f.name) << ", " << f.params.size() << ", " << q(f.returns) << "},\n";
  o << "};\ninline constexpr std::size_t kFunctionCount = " << s.functions.size() << ";\n\n"
    << "struct MethodSig { const char* type; const char* name; int arity; const char* returns; };\n"
    << "inline constexpr MethodSig kMethods[] = {\n";
  std::size_t mc = 0;
  for (auto& t : s.types)
    for (auto& m : t.methods) { o << "    {" << q(t.name) << ", " << q(m.name) << ", " << m.params.size() << ", " << q(m.returns) << "},\n"; ++mc; }
  if (mc == 0) o << "    {\"\", \"\", 0, \"\"},\n";
  o << "};\ninline constexpr std::size_t kMethodCount = " << mc << ";\n\n}  // namespace script_host::api\n";
  return o.str();
}

}  // namespace script_host
