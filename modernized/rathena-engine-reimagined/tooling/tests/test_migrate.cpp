// rathena-migrate acceptance tests (CAP-E10, spec 4.4, architecture section 8).
#include "harness.hpp"
#include "tooling/migrate.hpp"

using namespace tooling;
using namespace tooling::migrate;

static Value rec(Map m) { return Value(std::move(m)); }

TEST("TL-MIG-001", per_field_last_file_wins) {
  Value base = rec({{"Id", 1}, {"Name", "Poring"}, {"Hp", 50}});
  Value over = rec({{"Id", 1}, {"Hp", 60}});
  Value out = merge_fields(base, over);
  CHECK(out == rec({{"Id", 1}, {"Name", "Poring"}, {"Hp", 60}}));
}

TEST("TL-MIG-001", merge_records_keyed_chain_later_file_wins) {
  List main_db{rec({{"Id", 1}, {"Name", "A"}, {"Atk", 5}}), rec({{"Id", 2}, {"Name", "B"}})};
  List imp{rec({{"Id", 1}, {"Atk", 9}}), rec({{"Id", 3}, {"Name", "C"}})};
  Map out = merge_records({main_db, imp}, "Id");
  CHECK(out.size() == 3);
  CHECK(out.at("1") == rec({{"Id", 1}, {"Name", "A"}, {"Atk", 9}}));
}

TEST("TL-MIG-002", index_keyed_list_patches_matching_entries_and_appends_new) {
  Value base = rec({{"Drops", List{rec({{"Index", 0}, {"Item", "Jellopy"}, {"Rate", 70}}),
                                   rec({{"Index", 1}, {"Item", "Knife"}, {"Rate", 5}})}}});
  Value over = rec({{"Drops", List{rec({{"Index", 1}, {"Rate", 10}}),
                                   rec({{"Index", 2}, {"Item", "Apple"}, {"Rate", 1}})}}});
  Value out = merge_fields(base, over);
  Value want = rec({{"Drops", List{rec({{"Index", 0}, {"Item", "Jellopy"}, {"Rate", 70}}),
                                   rec({{"Index", 1}, {"Item", "Knife"}, {"Rate", 10}}),
                                   rec({{"Index", 2}, {"Item", "Apple"}, {"Rate", 1}})}}});
  CHECK(out == want);
}

TEST("TL-MIG-002", list_without_index_is_replaced) {
  Value out = merge_fields(rec({{"Jobs", List{Value("a"), Value("b")}}}),
                           rec({{"Jobs", List{Value("c")}}}));
  CHECK(out == rec({{"Jobs", List{Value("c")}}}));
}

PENDING_TEST("TL-MIG-003", mode_import_sets_and_clears_individual_flags) {
  Value base = rec({{"Mode", rec({{"CanMove", true}, {"Aggressive", true}})}});
  Value over = rec({{"CanMove", false}, {"Looter", true}});
  CHECK(merge_mode(base.map()->at("Mode"), over) ==
        rec({{"CanMove", false}, {"Aggressive", true}, {"Looter", true}}));
}

PENDING_TEST("TL-MIG-004", yaml_db_chain_loads_main_then_import_in_order) {
  auto chain = load_db_chain("db", "mob_db", DbSet::PreRenewal);
  CHECK(chain.size() >= 2);
}

PENDING_TEST("TL-MIG-005", emits_luau_module_with_namespaced_ids_into_ruleset_package) {
  Map m{{"1", rec({{"Id", 1}, {"AegisName", "PORING"}})}};
  std::string lua = emit_luau_module("ruleset.prerenewal", "mob", m);
  CHECK(lua.find("ruleset.prerenewal:mob/poring") != std::string::npos);
  CHECK(lua.find("client_id = 1") != std::string::npos);
}

PENDING_TEST("TL-MIG-006", diff_report_has_shared_by_diff_section) {
  Map pre{{"a", Value(1)}, {"b", Value(2)}};
  Map re{{"a", Value(1)}, {"b", Value(3)}, {"c", Value(4)}};
  auto d = diff_rulesets(pre, re);
  CHECK(d.shared_identical == std::vector<std::string>{"a"});
  CHECK(d.differing == std::vector<std::string>{"b"});
  CHECK(d.only_renewal == std::vector<std::string>{"c"});
}

PENDING_TEST("TL-MIG-007", embedded_scripts_are_routed_to_front_end_not_emitted_raw) {
  auto r = route_embedded_script("Script", "bonus bStr,3;");
  CHECK(r.compiled_to_bonus);
}
