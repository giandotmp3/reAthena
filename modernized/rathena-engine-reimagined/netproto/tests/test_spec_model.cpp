#include "fixtures.hpp"
#include "harness.hpp"

using namespace netproto;
using namespace fx;

TEST("CAP-24", client_version_parse_and_print) {
  const auto v = ClientVersion::parse("main/20211103");
  CHECK(v && v->date == 20211103 && v->flavour == Flavour::Main);
  CHECK(v->to_string() == "main/20211103");
  CHECK(!ClientVersion::parse("main/2021"));
  CHECK(!ClientVersion::parse("bogus/20211103"));
}

TEST("CAP-24", version_predicate_or_terms_per_flavour) {
  const auto p = *VersionPredicate::parse("MAIN>=20180000 || RE>=20100000 || ZERO>=20200000");
  CHECK(p.matches({20190101, Flavour::Main}));
  CHECK(!p.matches({20170101, Flavour::Main}));
  CHECK(p.matches({20100101, Flavour::Re}));
  CHECK(!p.matches({20190101, Flavour::Zero}));
  CHECK(VersionPredicate::parse("")->matches({1, Flavour::Zero}));  // empty = always
  CHECK(!VersionPredicate::parse("MAIN>20180000"));                 // malformed
}

TEST("CAP-24", version_predicate_upper_bound_is_exclusive) {
  const auto p = *VersionPredicate::parse("MAIN>=20040000 && MAIN<20180000");
  CHECK(p.matches({20171231, Flavour::Main}));
  CHECK(!p.matches({20180000, Flavour::Main}));
}

TEST("CAP-E02", resolve_picks_the_layout_for_the_version) {
  const auto cat = catalog();  // ResolvedPacket points into the catalog: keep it alive
  const auto r = cat.resolve(v2021());
  bool saw_enter = false;
  for (const auto& rp : r) if (rp.def->name == "CZ_ENTER") { saw_enter = true; CHECK(rp.version->opcode == 0x0436); }
  CHECK(saw_enter);
}

TEST("CAP-E02", opcode_collision_within_a_version_is_a_spec_error) {
  PacketCatalog c;
  c.add(packet("A", Direction::ClientToServer, Phase::InWorld, 0x0100, fixed(2)));
  c.add(packet("B", Direction::ClientToServer, Phase::InWorld, 0x0100, fixed(2)));
  bool threw = false;
  try { (void)c.resolve(v2021()); } catch (const SpecError&) { threw = true; }
  CHECK(threw);
}

TEST("CAP-E02", same_opcode_in_opposite_directions_is_allowed) {
  PacketCatalog c;
  c.add(packet("CZ_X", Direction::ClientToServer, Phase::InWorld, 0x0100, fixed(2)));
  c.add(packet("ZC_X", Direction::ServerToClient, Phase::InWorld, 0x0100, fixed(2)));
  CHECK(c.resolve(v2021()).size() == 2);
}

TEST("CAP-E02", overlapping_layouts_of_one_name_are_a_spec_error) {
  auto d = packet("A", Direction::ClientToServer, Phase::InWorld, 0x0100, fixed(2));
  d.versions.push_back(d.versions[0]);
  PacketCatalog c; c.add(d);
  bool threw = false;
  try { (void)c.resolve(v2021()); } catch (const SpecError&) { threw = true; }
  CHECK(threw);
}

PENDING_TEST("CAP-E02", yaml_spec_loads_and_matches_the_hand_built_catalog) {
  const auto c = load_catalog_yaml(std::string(NETPROTO_SOURCE_DIR) + "/proto/client_packets.yaml");
  CHECK(c.resolve(v2021()).size() >= 5);
}
