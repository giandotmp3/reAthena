// rathena-migrate-saves acceptance tests (architecture section 8, risk: data loss).
#include "harness.hpp"
#include "tooling/migrate_saves.hpp"

using namespace tooling::saves;

TEST("TL-SAVE-001", legacy_db_rejects_every_write_statement) {
  ReadOnlyLegacyDb db("${LEGACY_DATABASE_URL}");
  CHECK(ReadOnlyLegacyDb::is_read_only_statement("SELECT * FROM `char`"));
  for (const char* sql : {"UPDATE `char` SET zeny=0", "DELETE FROM inventory", "DROP TABLE sc_data",
                          "INSERT INTO skill VALUES (1)", "SELECT 1; DROP TABLE x",
                          "SELECT * FROM t FOR UPDATE", "SELECT 1 INTO OUTFILE '/tmp/x'",
                          "  TRUNCATE char"}) {
    bool threw = false;
    try { db.guard(sql); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
  }
}

TEST("TL-SAVE-002", reconciliation_requires_every_legacy_row_migrated_or_parked) {
  TableReconciliation ok{"inventory", 10, 8, 2, "", ""};
  TableReconciliation lost{"inventory", 10, 8, 1, "", ""};
  CHECK(ok.ok());
  CHECK(!lost.ok());
  MigrationReport r;
  r.tables = {ok, lost};
  CHECK(!r.all_ok());
}

TEST("TL-SAVE-002", table_checksum_is_order_independent_and_content_sensitive) {
  CHECK(table_checksum({"a", "b"}) == table_checksum({"b", "a"}));
  CHECK(table_checksum({"a", "b"}) != table_checksum({"a", "c"}));
}

PENDING_TEST("TL-SAVE-003", dry_run_writes_nothing_to_new_store) {
  auto r = run_migration(Mode::DryRun);
  CHECK(r.mode == Mode::DryRun);
  CHECK(r.all_ok());
}

PENDING_TEST("TL-SAVE-004", sc_data_val_fields_map_to_named_fields) {
  LegacyStatusRow blessing{10 /*SC_BLESSING-like type*/, 5, 0, 0, 0, 240000};
  auto m = map_status(blessing);
  CHECK(m.mapped && !m.orphaned);
  CHECK(m.fields.at("level") == 5);
}

PENDING_TEST("TL-SAVE-005", unmappable_status_is_parked_orphaned_never_dropped) {
  LegacyStatusRow unknown{999999, 1, 2, 3, 4, 1000};
  auto m = map_status(unknown);
  CHECK(!m.mapped && m.orphaned);
  CHECK(m.original.val3 == 3);
}

PENDING_TEST("TL-SAVE-006", registry_sigils_map_to_namespaced_kv_under_legacy_mod) {
  auto t = map_registry_key("#quest_state", "acc_reg_num");
  CHECK(t.scope == "account" && t.mod == "legacy");
  auto g = map_registry_key("##global_flag", "global_acc_reg_num");
  CHECK(g.scope == "account");
}

PENDING_TEST("TL-SAVE-007", rollback_removes_only_new_store_rows) {
  auto r = run_migration(Mode::Apply);
  rollback(r);
  CHECK(false);  // real assertion: new store empty, legacy checksums unchanged
}
