#include "fixtures.hpp"
#include "script_host/dialogue.hpp"

using namespace script_host;

LUAU_TEST("CAP-E03", "say yields a SayRequest and resumes on the client reply") {
  DialogueManager d;
  auto s = d.start(1, "m", "say('hello'); local c = choose({'a','b'})");
  CHECK(std::holds_alternative<SayRequest>(s.pending));
  auto s2 = d.resume(1, s.token, {});
  CHECK(std::holds_alternative<ChooseRequest>(s2.pending));
  DialogueResponse r;
  r.choice = 1;
  auto s3 = d.resume(1, s2.token, r);
  CHECK(s3.finished);
}

LUAU_TEST("CAP-E03", "disconnect cancels the dialogue") {
  DialogueManager d;
  d.start(2, "m", "say('x')");
  d.on_disconnect(2);
  CHECK(!d.has_dialogue(2));
}

LUAU_TEST("CAP-E03", "stale or foreign resume token is rejected") {
  DialogueManager d;
  auto s = d.start(3, "m", "say('x'); say('y')");
  d.resume(3, s.token, {});
  CHECK_THROWS(d.resume(3, s.token, {}));        // already consumed
  CHECK_THROWS(d.resume(4, "forged-token", {})); // never issued
}

LUAU_TEST("CAP-E03", "idle dialogue times out") {
  DialogueManager d;
  d.start(5, "m", "say('x')");
  d.advance_time(24ull * 3600 * 1000);
  CHECK(!d.has_dialogue(5));
}

LUAU_TEST("CAP-E03", "wait resumes on a timer, not a packet") {
  DialogueManager d;
  auto s = d.start(6, "m", "wait(500); say('done')");
  CHECK(std::holds_alternative<WaitRequest>(s.pending));
  d.advance_time(500);
  CHECK(d.has_dialogue(6));
}

namespace {
DialogueConfig seeded() {
  DialogueConfig c;
  c.token_seed = 99;
  return c;
}
}  // namespace

LUAU_TEST("CAP-E03", "choose returns the 1-based index; input returns the text; tokens rotate every step") {
  DialogueManager d(seeded());
  auto s = d.start(1, "m", R"(
    local c = choose({'red', 'green', 'blue'})
    local name = input('name?')
    say(('picked %d (%s) by %s'):format(c, ({'red','green','blue'})[c], name))
  )");
  auto& ch = std::get<ChooseRequest>(s.pending);
  CHECK_EQ(ch.options.size(), std::size_t(3));
  DialogueResponse r1; r1.choice = 2;
  auto s2 = d.resume(1, s.token, r1);
  CHECK(s2.token != s.token);
  CHECK(std::holds_alternative<InputRequest>(s2.pending));
  DialogueResponse r2; r2.text = "Bob";
  auto s3 = d.resume(1, s2.token, r2);
  CHECK(s3.token != s2.token);
  CHECK_EQ(std::get<SayRequest>(s3.pending).text, std::string("picked 2 (green) by Bob"));
  auto done = d.resume(1, s3.token, {});
  CHECK(done.finished && !done.failed);
  CHECK(!d.has_dialogue(1));
}

LUAU_TEST("CAP-E03", "invalid client replies are rejected without consuming the dialogue") {
  DialogueManager d(seeded());
  auto s = d.start(1, "m", "local c = choose({'a','b'}) say('got ' .. c)");
  DialogueResponse bad;
  bad.choice = 0;
  CHECK_THROWS(d.resume(1, s.token, bad));
  bad.choice = 3;
  CHECK_THROWS(d.resume(1, s.token, bad));
  DialogueResponse wrong_kind; wrong_kind.text = "a";
  CHECK_THROWS(d.resume(1, s.token, wrong_kind));
  CHECK_THROWS(d.resume(2, s.token, {}));              // another player's token is useless
  CHECK_THROWS(d.resume(1, s.token + "x", {}));
  DialogueResponse ok; ok.choice = 2;
  auto s2 = d.resume(1, s.token, ok);                  // the original token still works
  CHECK_EQ(std::get<SayRequest>(s2.pending).text, std::string("got 2"));
  auto in = d.start(5, "m", "input('x')");
  CHECK_THROWS(d.resume(5, in.token, {}));              // input needs text
}

LUAU_TEST("CAP-E03", "wait cannot be resumed by a packet, and chained waits fire from one time jump") {
  DialogueManager d(seeded());
  auto s = d.start(1, "m", "wait(100) wait(100) say('done')");
  CHECK_THROWS(d.resume(1, s.token, {}));
  auto wakes = d.advance_time(50);
  CHECK(wakes.empty());
  wakes = d.advance_time(1000);
  CHECK_EQ(wakes.size(), std::size_t(2));  // first wait ends at 100, second at 200, both due
  CHECK(std::holds_alternative<WaitRequest>(wakes[0].state.pending));
  CHECK(std::holds_alternative<SayRequest>(wakes[1].state.pending));
  CHECK_EQ(wakes[1].player, PlayerId(1));
}

LUAU_TEST("CAP-E03", "disconnect cancels the coroutine inside the mod's state") {
  DialogueManager d(seeded());
  ModEnvironmentSpec spec;
  spec.mod_id = "m";
  auto sb = std::make_shared<ModSandbox>(spec);
  d.attach("m", sb);
  d.start(1, "m", "say('a')");
  d.start(2, "m", "say('b')");
  CHECK_EQ(sb->co_count(), std::size_t(2));
  d.on_disconnect(1);
  CHECK_EQ(sb->co_count(), std::size_t(1));
  d.on_disconnect(1);  // idempotent
  CHECK_EQ(d.active_count(), std::size_t(1));
  CHECK_THROWS(d.resume(1, "anything", {}));
}

LUAU_TEST("CAP-E03", "idle timeout reports the player and frees the coroutine; replies reset the clock") {
  DialogueConfig c = seeded();
  c.idle_timeout_ms = 1000;
  DialogueManager d(c);
  auto s = d.start(1, "m", "say('a') say('b')");
  d.start(2, "m", "say('x')");
  d.advance_time(900);
  auto s2 = d.resume(1, s.token, {});   // reply at t=900 resets player 1's clock
  auto wakes = d.advance_time(1100);    // player 2 idle since 0 -> timed out; player 1 until 1900
  CHECK_EQ(wakes.size(), std::size_t(1));
  CHECK_EQ(wakes[0].player, PlayerId(2));
  CHECK(wakes[0].state.finished && wakes[0].state.failed);
  CHECK(d.has_dialogue(1));
  d.advance_time(1900);
  CHECK(!d.has_dialogue(1));
  d.advance_time(10);                   // time never runs backwards
  (void)s2;
}

LUAU_TEST("CAP-E03", "dialogue script errors and budget aborts end that dialogue only") {
  DialogueManager d(seeded());
  auto err = d.start(1, "m", "say('a') error('npc bug')");
  auto r = d.resume(1, err.token, {});
  CHECK(r.finished && r.failed);
  CHECK(fx::contains(r.error, "npc bug"));
  CHECK(!d.has_dialogue(1));
  auto spin = d.start(2, "m", "while true do end");
  CHECK(spin.finished && spin.failed);
  CHECK(fx::contains(spin.error, "budget"));
  // The mod's state is still fine for the next player.
  auto ok = d.start(3, "m", "say('still alive')");
  CHECK_EQ(std::get<SayRequest>(ok.pending).text, std::string("still alive"));
  auto syntax = d.start(4, "m", "say(");
  CHECK(syntax.finished && syntax.failed);
  auto first = d.start(5, "m", "return");
  CHECK(first.finished && !first.failed);
}

LUAU_TEST("CAP-E03", "dialogue calls are only valid directly inside the dialogue coroutine") {
  DialogueManager d(seeded());
  auto s = d.start(1, "m", "local co = coroutine.wrap(function() say('inner') end) co()");
  CHECK(s.finished && s.failed);
  ModSandbox sb(fx::env("m"));
  CHECK(sb.run("direct", "say('x')").status == RunStatus::Error);
  auto big = d.start(2, "m", "wait(10 * 24 * 3600 * 1000)");
  CHECK(big.finished && big.failed);
  CHECK(fx::contains(big.error, "maximum"));
}

LUAU_TEST("CAP-E03", "a dialogue memory bomb disables the mod and cancels the dialogue") {
  DialogueManager d(seeded());
  ModEnvironmentSpec spec = fx::env("hog", 1 << 20);
  d.attach("hog", std::make_shared<ModSandbox>(spec));
  auto s = d.start(1, "hog", "say('a') local t = {} for i = 1, 1e7 do t[i] = {i} end");
  auto r = d.resume(1, s.token, {});
  CHECK(r.finished && r.failed);
  CHECK(!d.has_dialogue(1));
}
