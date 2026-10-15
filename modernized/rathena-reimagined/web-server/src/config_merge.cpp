#include "rathena/web/config_merge.hpp"

namespace rathena::web {

void merge_json(nlohmann::json& orig, const nlohmann::json& patch, bool null_deletes) {
    if (!patch.is_object()) {
        if (!patch.is_null() || null_deletes) {
            orig = patch;
        }
        return;
    }
    if (!orig.is_object()) {
        orig = nlohmann::json::object();
    }
    for (auto it = patch.begin(); it != patch.end(); ++it) {
        if (it.value().is_null()) {
            if (null_deletes) {
                orig.erase(it.key());
            }
        } else {
            merge_json(orig[it.key()], it.value(), null_deletes);
        }
    }
}

nlohmann::json merge_user_config(nlohmann::json stored, const nlohmann::json& patch) {
    if (!stored.is_object()) stored = nlohmann::json::object();
    merge_json(stored, patch, /*null_deletes=*/true);
    return stored;
}

nlohmann::json merge_char_config(nlohmann::json stored, const nlohmann::json& patch) {
    if (!stored.is_object()) stored = nlohmann::json::object();
    merge_json(stored, patch, /*null_deletes=*/false);
    return stored;
}

}  // namespace rathena::web
