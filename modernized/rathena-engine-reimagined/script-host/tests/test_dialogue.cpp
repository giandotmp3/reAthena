#include "fixtures.hpp"
#include "script_host/dialogue.hpp"

using namespace script_host;

PENDING("CAP-E03", "say yields a SayRequest and resumes on the client reply") {
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

PENDING("CAP-E03", "disconnect cancels the dialogue") {
  DialogueManager d;
  d.start(2, "m", "say('x')");
  d.on_disconnect(2);
  CHECK(!d.has_dialogue(2));
}

PENDING("CAP-E03", "stale or foreign resume token is rejected") {
  DialogueManager d;
  auto s = d.start(3, "m", "say('x'); say('y')");
  d.resume(3, s.token, {});
  CHECK_THROWS(d.resume(3, s.token, {}));        // already consumed
  CHECK_THROWS(d.resume(4, "forged-token", {})); // never issued
}

PENDING("CAP-E03", "idle dialogue times out") {
  DialogueManager d;
  d.start(5, "m", "say('x')");
  d.advance_time(24ull * 3600 * 1000);
  CHECK(!d.has_dialogue(5));
}

PENDING("CAP-E03", "wait resumes on a timer, not a packet") {
  DialogueManager d;
  auto s = d.start(6, "m", "wait(500); say('done')");
  CHECK(std::holds_alternative<WaitRequest>(s.pending));
  d.advance_time(500);
  CHECK(d.has_dialogue(6));
}
