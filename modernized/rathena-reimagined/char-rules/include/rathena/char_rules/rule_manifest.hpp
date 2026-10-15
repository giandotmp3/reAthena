// Per-rule manifest for this unit (architecture §8: "Phase E generates a per-rule manifest
// (BR-ID -> module -> test), and CI fails on any rule without a test, whether passing or pending").
// tests/check_rule_coverage.cmake asserts each ID below appears in at least one test name.
#pragma once

#include <array>
#include <string_view>

namespace rathena::char_rules {

struct RuleHome {
	std::string_view id;
	std::string_view module;
};

inline constexpr std::array<RuleHome, 60> kRuleManifest{{
    {"BR-CHAR-001", "rules/charlife"}, {"BR-CHAR-002", "rules/charlife"}, {"BR-CHAR-003", "rules/charlife"},
    {"BR-CHAR-004", "rules/charlife"}, {"BR-CHAR-005", "rules/charlife"}, {"BR-CHAR-006", "rules/charlife"},
    {"BR-CHAR-007", "rules/charlife"}, {"BR-CHAR-008", "rules/charlife"}, {"BR-CHAR-009", "rules/charlife"},
    {"BR-CHAR-010", "rules/charlife"}, {"BR-CHAR-011", "rules/charlife"}, {"BR-CHAR-012", "rules/charlife"},
    {"BR-CHAR-013", "rules/charlife"}, {"BR-CHAR-014", "rules/charlife"}, {"BR-CHAR-015", "rules/charlife"},
    {"BR-CHAR-016", "rules/charlife"}, {"BR-CHAR-017", "rules/charlife"}, {"BR-CHAR-018", "rules/charlife"},
    {"BR-CHAR-019", "rules/charlife"}, {"BR-CHAR-020", "rules/charlife"}, {"BR-CHAR-021", "rules/charlife"},
    {"BR-CHAR-022", "rules/charlife"}, {"BR-CHAR-023", "rules/charlife"}, {"BR-CHAR-024", "rules/charlife"},
    {"BR-CHAR-025", "rules/charlife"}, {"BR-CHAR-026", "rules/charlife"}, {"BR-CHAR-027", "rules/charlife"},
    {"BR-CHAR-028", "rules/charlife"}, {"BR-CHAR-029", "rules/charlife"}, {"BR-CHAR-030", "rules/charlife"},
    {"BR-CHAR-031", "rules/charlife"}, {"BR-CHAR-032", "rules/charlife"},
    {"BR-CHAR-033", "rules/party"},    {"BR-CHAR-034", "rules/party"},    {"BR-CHAR-035", "rules/party"},
    {"BR-CHAR-036", "rules/party"},
    {"BR-CHAR-037", "rules/guild"},    {"BR-CHAR-038", "rules/guild"},    {"BR-CHAR-039", "rules/guild"},
    {"BR-CHAR-040", "rules/guild"},    {"BR-CHAR-041", "rules/guild"},    {"BR-CHAR-042", "rules/guild"},
    {"BR-CHAR-043", "rules/guild"},    {"BR-CHAR-044", "rules/guild"},    {"BR-CHAR-045", "rules/guild"},
    {"BR-CHAR-046", "rules/mail"},     {"BR-CHAR-047", "rules/mail"},     {"BR-CHAR-048", "rules/mail"},
    {"BR-CHAR-049", "rules/mail"},
    {"BR-CHAR-050", "rules/registry"}, {"BR-CHAR-051", "rules/clan"},     {"BR-CHAR-052", "rules/charlife"},
    {"BR-CHAR-053", "rules/charlife"}, {"BR-CHAR-054", "rules/registry"}, {"BR-CHAR-055", "rules/gm"},
    {"BR-INTER-008", "rules/session"}, {"BR-INTER-009", "rules/session"}, {"BR-INTER-015", "rules/session"},
    {"BR-INTER-018", "rules/session"}, {"BR-INTER-019", "rules/session"},
}};

} // namespace rathena::char_rules
