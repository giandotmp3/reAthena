// rathena-web-server: JSON config merge rules (BR-WEB-009, BR-WEB-011).
#pragma once

#include <nlohmann/json.hpp>

namespace rathena::web {

/// Recursive merge of `patch` into `orig`.
///  - null_deletes = true : RFC 7386 merge-patch semantics (null removes the key)
///  - null_deletes = false: null in the patch leaves the stored value untouched
/// A non-object `orig` is replaced by an object before merging object patches.
void merge_json(nlohmann::json& orig, const nlohmann::json& patch, bool null_deletes);

/// BR-WEB-009: user config save; null deletes.
/// {"a":1,"b":2} + {"b":null,"c":3} -> {"a":1,"c":3}
nlohmann::json merge_user_config(nlohmann::json stored, const nlohmann::json& patch);

/// BR-WEB-011: char config save; null preserves.
/// {"a":1,"b":2} + {"b":null,"c":3} -> {"a":1,"b":2,"c":3}
nlohmann::json merge_char_config(nlohmann::json stored, const nlohmann::json& patch);

}  // namespace rathena::web
