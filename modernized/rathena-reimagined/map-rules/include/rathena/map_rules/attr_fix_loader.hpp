// Loader for db/{re,pre-re}/attr_fix.yml (ATTRIBUTE_DB) -> AttrFixTable (BR-MAP-017, CAP-23).
//
// Deliberately narrow: it accepts exactly the shape the shipped file uses
// (Body: - Level: N / <Attacker>: / <Defender>: pct) and rejects anything else, so a format
// change upstream fails loudly instead of loading a partial table. When the general
// `gamedata` YAML library lands, this becomes a thin adapter over it.
#pragma once

#include <istream>
#include <optional>
#include <string>

#include "rathena/map_rules/combat.hpp"

namespace rathena::map_rules::combat {

struct AttrFixLoadResult {
	std::optional<AttrFixTable> table; ///< set only when every level x attacker x defender cell was read exactly once
	std::string error;                 ///< "line N: ..." when table is empty
};

/// Parses an ATTRIBUTE_DB YAML stream. Element names follow the legacy file ("Dark" is Element::Shadow).
AttrFixLoadResult load_attr_fix_yaml(std::istream& in);

} // namespace rathena::map_rules::combat
