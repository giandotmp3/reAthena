// mods.lock grants and signatures. Tags MOD-PERM-*.
// All signature/digest values below are fake same-shape placeholders.
#include "harness.hpp"
#include "mods/lock.hpp"

using namespace mods;

static Manifest mk_with(std::vector<std::string> perms) {
  Manifest m;
  m.id = "example.mod";
  m.version = {1, 0, 0};
  m.engine_api = "^1";
  for (auto& p : perms) m.permissions.push_back(*parse_permission(p));
  return m;
}
static const SignatureVerifier kAccept = [](auto&, auto&, auto&) { return true; };
static const SignatureVerifier kReject = [](auto&, auto&, auto&) { return false; };
static const char* kFakeDigest = "sha256:0000000000000000000000000000000000000000000000000000000000000000";

template <class F>
static std::string load_error(F f, const std::string& code) {
  try { f(); }
  catch (const NotImplemented&) { throw CheckFailure("not implemented"); }
  catch (const LoadError& e) { CHECK(e.code() == code); return e.what(); }
  throw CheckFailure("expected LoadError " + code);
}

TEST("MOD-PERM-001", default_lock_grants_nothing) {
  ModsLock lock;
  CHECK(!lock.require_signatures);
  CHECK(lock.entries.empty());
}

PENDING_TEST("MOD-PERM-002", ungranted_permission_refused_with_precise_error) {
  auto m = mk_with({"atcommand"});
  ModsLock lock;
  auto msg = load_error([&] { authorize(m, lock, kAccept, kFakeDigest); }, "permission.ungranted");
  CHECK(msg.find("example.mod") != std::string::npos);
  CHECK(msg.find("atcommand") != std::string::npos);
}

PENDING_TEST("MOD-PERM-003", granted_permission_is_accepted) {
  auto m = mk_with({"http.route"});
  ModsLock lock;
  lock.entries["example.mod"].granted.insert(*parse_permission("http.route"));
  authorize(m, lock, kAccept, kFakeDigest);  // must not throw
}

PENDING_TEST("MOD-PERM-004", grants_not_requested_are_ignored) {
  auto m = mk_with({"http.route"});
  ModsLock lock;
  lock.entries["example.mod"].granted = {*parse_permission("http.route"), *parse_permission("net.raw")};
  auto eff = effective_grants(m, lock);
  CHECK(eff.size() == 1);
  CHECK(eff.count(*parse_permission("net.raw")) == 0);
}

PENDING_TEST("MOD-PERM-005", unsigned_mod_refused_when_policy_requires_signatures) {
  auto m = mk_with({});
  ModsLock lock;
  lock.require_signatures = true;
  lock.entries["example.mod"];  // no signature
  load_error([&] { authorize(m, lock, kAccept, kFakeDigest); }, "signature.missing");
}

PENDING_TEST("MOD-PERM-006", invalid_signature_refused) {
  auto m = mk_with({});
  ModsLock lock;
  lock.require_signatures = true;
  lock.entries["example.mod"].signature = "fake-signature-placeholder";
  load_error([&] { authorize(m, lock, kReject, kFakeDigest); }, "signature.invalid");
}

PENDING_TEST("MOD-PERM-007", signatures_optional_when_policy_off) {
  auto m = mk_with({});
  ModsLock lock;
  authorize(m, lock, kReject, kFakeDigest);  // must not throw
}

PENDING_TEST("MOD-PERM-008", mods_lock_toml_round_trip) {
  ModsLock lock = parse_mods_lock(R"(
require_signatures = true
[mod."example.mod"]
grants = ["storage.account"]
signature = "fake-signature-placeholder"
)");
  CHECK(lock.require_signatures);
  CHECK(lock.entries.at("example.mod").granted.size() == 1);
}

PENDING_TEST("MOD-PERM-009", unknown_grant_string_in_lock_rejected) {
  try { parse_mods_lock("[mod.\"a\"]\ngrants = [\"sql.read\"]\n"); }
  catch (const NotImplemented&) { CHECK(false); }
  catch (const LoadError&) { return; }
  CHECK(false);
}
