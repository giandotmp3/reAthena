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

TEST("MOD-PERM-002", ungranted_permission_refused_with_precise_error) {
  auto m = mk_with({"atcommand"});
  ModsLock lock;
  auto msg = load_error([&] { authorize(m, lock, kAccept, kFakeDigest); }, "permission.ungranted");
  CHECK(msg.find("example.mod") != std::string::npos);
  CHECK(msg.find("atcommand") != std::string::npos);
}

TEST("MOD-PERM-003", granted_permission_is_accepted) {
  auto m = mk_with({"http.route"});
  ModsLock lock;
  lock.entries["example.mod"].granted.insert(*parse_permission("http.route"));
  authorize(m, lock, kAccept, kFakeDigest);  // must not throw
}

TEST("MOD-PERM-004", grants_not_requested_are_ignored) {
  auto m = mk_with({"http.route"});
  ModsLock lock;
  lock.entries["example.mod"].granted = {*parse_permission("http.route"), *parse_permission("net.raw")};
  auto eff = effective_grants(m, lock);
  CHECK(eff.size() == 1);
  CHECK(eff.count(*parse_permission("net.raw")) == 0);
}

TEST("MOD-PERM-005", unsigned_mod_refused_when_policy_requires_signatures) {
  auto m = mk_with({});
  ModsLock lock;
  lock.require_signatures = true;
  lock.entries["example.mod"];  // no signature
  load_error([&] { authorize(m, lock, kAccept, kFakeDigest); }, "signature.missing");
}

TEST("MOD-PERM-006", invalid_signature_refused) {
  auto m = mk_with({});
  ModsLock lock;
  lock.require_signatures = true;
  lock.entries["example.mod"].signature = "fake-signature-placeholder";
  load_error([&] { authorize(m, lock, kReject, kFakeDigest); }, "signature.invalid");
}

TEST("MOD-PERM-007", signatures_optional_when_policy_off) {
  auto m = mk_with({});
  ModsLock lock;
  authorize(m, lock, kReject, kFakeDigest);  // must not throw
}

TEST("MOD-PERM-008", mods_lock_toml_round_trip) {
  ModsLock lock = parse_mods_lock(R"(
require_signatures = true
[mod."example.mod"]
grants = ["storage.account"]
signature = "fake-signature-placeholder"
)");
  CHECK(lock.require_signatures);
  CHECK(lock.entries.at("example.mod").granted.size() == 1);
}

TEST("MOD-PERM-009", unknown_grant_string_in_lock_rejected) {
  try { parse_mods_lock("[mod.\"a\"]\ngrants = [\"sql.read\"]\n"); }
  catch (const NotImplemented&) { CHECK(false); }
  catch (const LoadError&) { return; }
  CHECK(false);
}

// ---- added with the implementation ---------------------------------------------
#include <cstdlib>

#include "mods/sha256.hpp"

TEST("MOD-PERM-010", sha256_matches_the_fips_test_vectors) {
  CHECK(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  CHECK(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
  CHECK(sha256_hex(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
  // block-boundary lengths agree between one-shot and incremental hashing
  for (std::size_t n : {55u, 56u, 57u, 63u, 64u, 65u, 119u, 120u, 128u}) {
    std::string s(n, 'x');
    Sha256 h;
    for (char c : s) h.update(&c, 1);
    auto d = h.finish();
    CHECK(to_hex(d.data(), d.size()) == sha256_hex(s));
  }
}

TEST("MOD-PERM-011", keyed_hash_verifier_accepts_only_the_right_key_mod_and_digest) {
  const std::string key = "fake-test-key-material";
  const std::string trusted = key_hash(key);
  CHECK(trusted.size() == 71 && trusted.rfind("sha256:", 0) == 0);
  auto verify = make_keyed_hash_verifier(key, trusted);
  std::string sig = keyed_hash_signature(key, "example.mod", kFakeDigest);
  CHECK(verify("example.mod", kFakeDigest, sig));
  CHECK(!verify("other.mod", kFakeDigest, sig));                       // bound to the mod id
  CHECK(!verify("example.mod", "sha256:" + std::string(64, '1'), sig));  // bound to the content
  CHECK(!verify("example.mod", kFakeDigest, sig.substr(0, sig.size() - 1) + "0"));
  CHECK(!verify("example.mod", kFakeDigest, ""));
  CHECK(!make_keyed_hash_verifier("another-key", trusted)("example.mod", kFakeDigest, keyed_hash_signature("another-key", "example.mod", kFakeDigest)));  // wrong key in force
  // end to end through authorize()
  auto m = mk_with({});
  ModsLock lock;
  lock.require_signatures = true;
  lock.trusted_key_hash = trusted;
  lock.entries["example.mod"].signature = sig;
  authorize(m, lock, verify, kFakeDigest);
  lock.entries["example.mod"].signature = keyed_hash_signature(key, "example.mod", "sha256:" + std::string(64, '2'));
  load_error([&] { authorize(m, lock, verify, kFakeDigest); }, "signature.invalid");
}

TEST("MOD-PERM-012", present_signature_is_verified_even_when_policy_is_off_and_no_verifier_means_reject) {
  auto m = mk_with({});
  ModsLock lock;
  lock.entries["example.mod"].signature = "x";
  load_error([&] { authorize(m, lock, kReject, kFakeDigest); }, "signature.invalid");
  authorize(m, lock, kAccept, kFakeDigest);
  load_error([&] { authorize(m, lock, SignatureVerifier{}, kFakeDigest); }, "signature.invalid");
}

TEST("MOD-PERM-013", verifier_from_environment_needs_key_and_trusted_hash) {
  ModsLock lock;
  ::unsetenv("MODS_TEST_KEY");
  CHECK(!verifier_from_environment(lock, "MODS_TEST_KEY")("a", "d", "s"));
  ::setenv("MODS_TEST_KEY", "fake-env-key", 1);
  CHECK(!verifier_from_environment(lock, "MODS_TEST_KEY")("a", "d", keyed_hash_signature("fake-env-key", "a", "d")));  // no trusted hash configured
  lock.trusted_key_hash = key_hash("fake-env-key");
  CHECK(verifier_from_environment(lock, "MODS_TEST_KEY")("a", "d", keyed_hash_signature("fake-env-key", "a", "d")));
  lock.trusted_key_hash = key_hash("different");
  CHECK(!verifier_from_environment(lock, "MODS_TEST_KEY")("a", "d", keyed_hash_signature("fake-env-key", "a", "d")));
  ::unsetenv("MODS_TEST_KEY");
}

TEST("MOD-PERM-014", pinned_digest_must_match_and_all_missing_permissions_are_named) {
  auto m = mk_with({"atcommand", "net.raw", "http.route"});
  ModsLock lock;
  lock.entries["example.mod"].granted = {*parse_permission("http.route")};
  lock.entries["example.mod"].digest = kFakeDigest;
  auto msg = load_error([&] { authorize(m, lock, kAccept, kFakeDigest); }, "permission.ungranted");
  CHECK(msg.find("atcommand") != std::string::npos && msg.find("net.raw") != std::string::npos && msg.find("http.route") == std::string::npos);
  auto msg2 = load_error([&] { authorize(m, lock, kAccept, "sha256:" + std::string(64, '3')); }, "digest.mismatch");
  CHECK(msg2.find("example.mod") != std::string::npos);
}

TEST("MOD-PERM-015", lock_file_validation_and_canonical_round_trip) {
  auto code = [](const std::string& text) -> std::string {
    try { parse_mods_lock(text, "l.lock"); } catch (const LoadError& e) { return e.code(); }
    return "";
  };
  CHECK(code("require_signatures = \"yes\"\n") == "lock.bad_type");
  CHECK(code("trusted_key_hash = \"md5:abc\"\n") == "lock.bad_value");
  CHECK(code("bogus = 1\n") == "lock.unknown_key");
  CHECK(code("[mod.\"A_B\"]\n") == "lock.bad_value");
  CHECK(code("[mod.\"a\"]\ngrants = [\"storage.read:x\", \"storage.read:x\"]\n") == "lock.bad_value");
  CHECK(code("[mod.\"a\"]\nextra = 1\n") == "lock.unknown_key");
  CHECK(code("[mod.\"a\"]\ngrants = [\"sql.read\"]\n") == "lock.unknown_grant");
  CHECK(code("a = = 1\n") == "lock.syntax");
  CHECK(code("[mod.\"a\"]\ndigest = \"sha256:xyz\"\n") == "lock.bad_value");
  ModsLock l;
  l.require_signatures = true;
  l.trusted_key_hash = key_hash("k");
  l.entries["b.mod"].granted = {*parse_permission("storage.read:a.mod"), *parse_permission("net.raw")};
  l.entries["b.mod"].signature = "sig";
  l.entries["b.mod"].digest = kFakeDigest;
  l.entries["a.mod"];
  std::string text = write_mods_lock(l);
  ModsLock r = parse_mods_lock(text);
  CHECK(r.require_signatures && r.trusted_key_hash == l.trusted_key_hash && r.entries.size() == 2);
  CHECK(r.entries.at("b.mod").granted == l.entries.at("b.mod").granted && r.entries.at("b.mod").digest == kFakeDigest);
  CHECK(write_mods_lock(r) == text);
}

// ---- pipeline.protected_stages: least-privilege grant semantics ---------------------------------------------------------------
TEST("MOD-PERM-015", protected_stages_requested_but_not_granted_blocks_the_generation) {
  auto m = mk_with({"pipeline.protected_stages"});
  ModsLock none;
  auto msg = load_error([&] { authorize(m, none, kAccept, kFakeDigest); }, "permission.ungranted");
  CHECK(msg.find("example.mod") != std::string::npos && msg.find("pipeline.protected_stages") != std::string::npos);
  ModsLock other;   // a grant of something else does not help
  other.entries["example.mod"].granted.insert(*parse_permission("storage.account"));
  load_error([&] { authorize(m, other, kAccept, kFakeDigest); }, "permission.ungranted");
  ModsLock ok;
  ok.entries["example.mod"].granted.insert(*parse_permission("pipeline.protected_stages"));
  authorize(m, ok, kAccept, kFakeDigest);   // granted: accepted
  CHECK(effective_grants(m, ok).count(*parse_permission("pipeline.protected_stages")) == 1);
}

TEST("MOD-PERM-016", protected_stages_granted_but_not_requested_is_dropped_and_the_lock_file_round_trips) {
  auto plain = mk_with({});
  ModsLock lock = parse_mods_lock("[mod.\"example.mod\"]\ngrants = [\"pipeline.protected_stages\", \"storage.account\"]\n");
  CHECK(lock.entries.at("example.mod").granted.size() == 2);
  CHECK(effective_grants(plain, lock).empty());                          // least privilege: nothing requested, nothing in force
  authorize(plain, lock, kAccept, kFakeDigest);                           // an extra grant is not an error
  auto asks = mk_with({"pipeline.protected_stages"});
  auto eff = effective_grants(asks, lock);
  CHECK(eff.size() == 1 && eff.count(*parse_permission("pipeline.protected_stages")) == 1);   // storage.account was not requested
  ModsLock back = parse_mods_lock(write_mods_lock(lock));
  CHECK(back.entries.at("example.mod").granted == lock.entries.at("example.mod").granted);
  CHECK(write_mods_lock(back) == write_mods_lock(lock));
  try { parse_mods_lock("[mod.\"a\"]\ngrants = [\"pipeline.protected\"]\n"); CHECK(false); }
  catch (const LoadError& e) { CHECK(e.code() == "lock.unknown_grant"); }
}
