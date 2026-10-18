// rathena-pack, codegen drivers and M0 spikes.
#include "harness.hpp"
#include "tooling/codegen.hpp"
#include "tooling/pack.hpp"
#include "tooling/spike.hpp"

using namespace tooling;

PENDING_TEST("TL-PACK-001", client_tables_cover_mod_defined_skill_item_status) {
  std::vector<pack::ClientDefinition> defs{
      {{"mymod", "skill", "fire_brand"}, "skill", 30001, "Fire Brand", "fire_brand"}};
  auto out = pack::build_client_tables(defs);
  CHECK(out.skillinfolist.find("30001") != std::string::npos);
}

PENDING_TEST("TL-PACK-002", tombstoned_client_ids_are_never_reassigned) {
  int id = pack::allocate_client_id("skill", {30001}, {30002});
  CHECK(id == 30003);
}

PENDING_TEST("TL-GEN-001", generated_files_carry_do_not_edit_header) {
  for (const auto& f : codegen::generate_codecs("proto"))
    CHECK(f.content.rfind(codegen::kGeneratedHeader, 0) == 0);
}

PENDING_TEST("TL-GEN-002", api_schema_generates_engine_d_luau) {
  bool found = false;
  for (const auto& f : codegen::generate_api("api")) found |= f.path == "engine.d.luau";
  CHECK(found);
}

PENDING_TEST("TL-M0-001", damage_spike_reports_us_per_hit_with_budget_interrupt_on) {
  auto r = spike::damage_aoe_burst(50, true);
  CHECK(r.unit == "us/hit" && r.metric > 0);
}

PENDING_TEST("TL-M0-002", numeric_fidelity_spike_reproduces_worked_examples_E1_to_E10) {
  CHECK(spike::numeric_fidelity().go);
}
