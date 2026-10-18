#pragma once
// Ruleset-supplied data tables. Native stages carry no game numbers of their own
// (BR-RULESET-111: "driven by script-published tables"; BR-RULESET-118).
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace nd {

// ratio[atk_element][def_element][def_level-1], percent (100 = x1.0).
struct ElementTable {
  int elements = 0;
  int levels = 0;
  std::vector<int> ratio;
  int at(int atk, int def_element, int def_level) const;  // implemented
};

// ratio[weapon_type][size], percent.
struct SizeTable {
  int weapon_types = 0;
  int sizes = 0;
  std::vector<int> ratio;
  int at(int weapon_type, int size) const;  // implemented
};

enum class StatusElementMode {
  PreMultiplicative,  // PRE: dmg += dmg*pct/100, then dmg *= table/100   (BR-RULESET-031)
  RenewalAdditive,    // RE:  ratio += pct; dmg -= dmg*(100-ratio)/100
};

enum class DefenseCurve {
  PreRenewal,  // dmg*(100-def)/100 - vit_def (randomised)               (BR-MAP-015)
  Renewal,     // dmg*(k_base+def)/(k_base+k_slope*def) - soft_def       (BR-MAP-014)
};

struct DefenseParams {
  DefenseCurve curve = DefenseCurve::Renewal;
  std::int64_t k_base = 4000;
  std::int64_t k_slope = 10;
};

struct ResParams {            // BR-RULESET-025 (RE only)
  std::int64_t divisor_base = 400;
  std::int64_t max_reduction_pct = 80;
  std::int64_t max_ignore_pct = 50;
};

struct MdefParams {           // BR-RULESET-045
  bool renewal = true;
  std::int64_t k_base = 1000;
  std::int64_t k_slope = 10;
};

// Everything a ruleset hands to the native stages at opt-in time.
struct RulesetTables {
  std::string ruleset_id;                      // e.g. "ruleset.prerenewal"
  ElementTable element;
  SizeTable size;
  StatusElementMode status_element_mode = StatusElementMode::RenewalAdditive;
  DefenseParams defense;
  ResParams res;
  MdefParams mdef;
  std::map<std::string, std::int64_t> constants;  // free-form tunables (e.g. attack_attr_none)
};

}  // namespace nd
