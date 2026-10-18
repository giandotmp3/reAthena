// CharacterStore / AccountStore / registry purger over the persistence unit.
#include <functional>
#include <thread>

#include "fixture.hpp"
#include "pstack.hpp"

using namespace gt;

TEST("CAP-07", account_and_character_serialisation_roundtrip_and_reject_garbage) {
  Account a;
  a.id = 2000007; a.userid = "u"; a.stored_secret = "$x$"; a.scheme = PasswordScheme::Argon2id; a.challenge_secret = "c";
  a.sex = Sex::Female; a.group_id = 3; a.old_group = 1; a.state = 2; a.unban_time = 5; a.expiration_time = 6;
  a.login_id1 = 7; a.login_id2 = 8; a.logincount = 9; a.last_ip = "1.2.3.4"; a.lastlogin = 10; a.char_slots = 4;
  a.vip_time = 11; a.email = "e@x.com"; a.birthdate = "900101"; a.web_token = "0123456789abcdef"; a.web_token_enabled = true;
  a.pin = "p"; a.pin_changed = 12; a.pin_failures = 2; a.pin_last_failure = 13;
  auto b = gateway::decode_account(gateway::encode_account(a));
  CHECK(b && b->userid == "u" && b->old_group == 1 && b->pin_last_failure == 13 && b->web_token_enabled && b->sex == Sex::Female);
  CHECK(gateway::encode_account(*b) == gateway::encode_account(a));
  Character c;
  c.id = 150001; c.account_id = 2000007; c.name = "Bobby"; c.slot = 3; c.base_level = 9; c.max_hp = 40; c.max_sp = 11;
  c.delete_date = 99; c.online = true; c.extra["k"] = -5; c.extra["z"] = 1LL << 40;
  auto d = gateway::decode_character(gateway::encode_character(c));
  CHECK(d && d->extra == c.extra && d->name == "Bobby" && d->online && d->delete_date == 99);
  CHECK(!gateway::decode_account("") && !gateway::decode_character("junk") && !gateway::decode_account(gateway::encode_account(a) + "x"));
  std::string trunc = gateway::encode_character(c);
  trunc.resize(trunc.size() - 3);
  CHECK(!gateway::decode_character(trunc));
}

TEST("CAP-07", character_store_crud_indexes_and_restart_durability) {
  auto dir = scratch("charstore");
  CharId id1 = 0, id2 = 0;
  {
    PStack s(dir);
    gateway::PersistCharacterStore chars(*s.bb);
    Character c; c.account_id = 2000001; c.name = "Bobby"; c.slot = 1; c.max_hp = 40;
    id1 = chars.insert(c);
    Character d; d.account_id = 2000001; d.name = "Carol"; d.slot = 0; d.extra["zeny"] = 5;
    id2 = chars.insert(d);
    CHECK(id1 == 150000 && id2 == 150001);
    CHECK(chars.name_exists("bobby") && chars.name_exists("CAROL") && !chars.name_exists("Nobody"));   // read-your-writes, any case
    auto list = chars.list_by_account(2000001);
    CHECK(list.size() == 2 && list[0].name == "Carol" && list[1].name == "Bobby");                       // by slot
    Character u = *chars.find(id1); u.name = "Robert"; u.online = true; chars.update(u);
    CHECK(!chars.name_exists("Bobby") && chars.name_exists("Robert") && chars.find(id1)->online);
    chars.erase(id2);
    CHECK(!chars.find(id2) && !chars.name_exists("Carol") && chars.list_by_account(2000001).size() == 1);
    CHECK(chars.list_by_account(777).empty());
    CHECK(s.wait_durable());
  }
  {  // a new process on the same database and WAL directory sees exactly that state
    PStack s(dir, 1u << 20, false);
    gateway::PersistCharacterStore chars(*s.bb);
    CHECK(chars.find(id1)->name == "Robert" && !chars.find(id2) && chars.name_exists("robert") && !chars.name_exists("carol"));
    CHECK(chars.list_by_account(2000001).size() == 1);
    Character e; e.account_id = 2000002; e.name = "Later"; e.slot = 0;
    CHECK(chars.insert(e) == 150002);                                                                   // ids continue, never reused
    chars.set_all_offline();
    CHECK(!chars.find(id1)->online);
  }
}

TEST("CAP-07", account_store_indexes_case_policy_max_id_tokens_and_removal) {
  auto dir = scratch("acctstore");
  {
  PStack s(dir);
  gateway::PersistAccountStore accounts(*s.bb);
  Account a; a.id = 2000001; a.userid = "Bobby"; a.web_token = "aaaaaaaaaaaaaaaa";
  Account b; b.id = 2000002; b.userid = "bobby";
  CHECK(accounts.create(a) == 2000001 && accounts.create(b) == 2000002);
  CHECK(!accounts.create(a) && !accounts.create(Account{}));                                           // exists / id 0
  Account big; big.id = 100000001; big.userid = "big";
  CHECK(!accounts.create(big));
  CHECK(accounts.find_by_userid("Bobby", true).size() == 1 && accounts.find_by_userid("BOBBY", true).empty());
  CHECK(accounts.find_by_userid("BOBBY", false).size() == 2);                                           // BR-LOGIN-038: caller denies on > 1
  CHECK(accounts.max_id() == 2000002);
  CHECK(accounts.web_token_in_use("aaaaaaaaaaaaaaaa") && !accounts.web_token_in_use("bbbbbbbbbbbbbbbb"));
  Account u = *accounts.find(2000001); u.web_token = "bbbbbbbbbbbbbbbb"; accounts.update(u);
  CHECK(!accounts.web_token_in_use("aaaaaaaaaaaaaaaa") && accounts.web_token_in_use("bbbbbbbbbbbbbbbb"));
  accounts.clear_all_web_tokens();
  CHECK(!accounts.web_token_in_use("bbbbbbbbbbbbbbbb") && accounts.find(2000001)->web_token.empty());
  accounts.remove(2000001);
  CHECK(!accounts.find(2000001) && accounts.find_by_userid("Bobby", true).empty() && accounts.find_by_userid("bobby", false).size() == 1);
  CHECK(s.wait_durable());
  }  // the first process is gone (WAL directory lock released)
  PStack again(dir, 1u << 20, false);
  gateway::PersistAccountStore reread(*again.bb);
  CHECK(!reread.find(2000001) && reread.find(2000002)->userid == "bobby" && reread.max_id() == 2000002);
}

TEST("BR-LOGIN-037", registry_purge_erases_every_row_of_the_account_in_the_listed_mods) {
  auto dir = scratch("registry");
  PStack s(dir);
  namespace rp = rathena::persist;
  for (const char* mod : {"legacy.npc", "other.mod"}) {
    rp::KvEntry e;
    e.key = rp::KvKey{rp::OwnerRef{rp::OwnerScope::GlobalAccount, 0}, mod, "#gold", "2000001"};
    e.value = rp::Value(std::int64_t{5});
    e.revision = 1;
    s.wb->mark(e);
    e.key.owner_text = "2000002";
    s.wb->mark(e);
  }
  s.wb->handoff();
  CHECK(s.wb->wait_durable(1, std::chrono::seconds(5)));
  CHECK(s.count("SELECT COUNT(*) FROM kv_entries") == 4);
  gateway::PersistRegistryPurger purger(*s.bb, {"legacy.npc"});
  purger.purge_account_registry(2000001);
  CHECK(s.eventually([&] { return s.count("SELECT COUNT(*) FROM kv_entries") == 3; }));
  CHECK(s.count("SELECT COUNT(*) FROM kv_entries WHERE owner_ref = '2000001' AND mod_id = 'legacy.npc'") == 0);
  CHECK(s.count("SELECT COUNT(*) FROM kv_entries WHERE owner_ref = '2000002'") == 2);                 // other accounts untouched
  CHECK(s.count("SELECT COUNT(*) FROM kv_entries WHERE owner_ref = '2000001' AND mod_id = 'other.mod'") == 1);   // unlisted mod untouched
}

TEST("CAP-04", full_login_char_select_and_world_handoff_on_the_persistence_stores) {
  auto dir = scratch("flow");
  PStack s(dir);
  gateway::PersistAccountStore accounts(*s.bb);
  gateway::PersistCharacterStore chars(*s.bb);
  Env e;
  FakeHasher hasher;
  GatewayDeps d{e.cfg, e.clock, e.rng, accounts, chars, e.dir, e.bus, e.tickets, hasher, e.bans, e.fails, e.policy};
  LoginService login(d);
  CharService chr(d);
  Account a; a.id = 2000001; a.userid = "fakeuser"; a.stored_secret = hasher.hash(kFakePass); a.birthdate = "900101";
  CHECK(accounts.create(a));
  auto lo = login.login(e.req());
  CHECK(lo.ok);
  CharSession cs;
  auto en = chr.enter(cs, a.id, lo.ticket->login_id1, lo.ticket->login_id2, Sex::Male);
  CHECK(en.ok);
  MakeCharRequest mk; mk.name = "Bobby";
  auto made = chr.make(cs, mk);
  CHECK(made.ok && made.ch.max_hp == 40);
  auto sel = chr.select(cs, 0);
  CHECK(sel.ok && sel.ticket->char_id == made.ch.id);
  CHECK(chr.authenticate_from_world(a.id, made.ch.id, cs.login_id1));
  CHECK(chars.find(made.ch.id)->online);
  CHECK(s.wait_durable());
  CHECK(s.count("SELECT COUNT(*) FROM persist_records WHERE kind = 'gateway:char'") == 1);
  CHECK(s.count("SELECT COUNT(*) FROM persist_records WHERE kind = 'gateway:account'") == 1);
  // delete cycle ends with a tombstoned row and a freed name
  e.cfg.char_del_delay_s = 0;
  CHECK(chr.confirm_delete(cs, made.ch.id, "900101").ok);
  CHECK(!chars.name_exists("Bobby") && chars.list_by_account(a.id).empty());
}
