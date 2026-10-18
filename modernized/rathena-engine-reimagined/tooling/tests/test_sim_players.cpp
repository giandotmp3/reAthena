// SimPlayer and the world/session boundary: connect, walk, chat, visibility, phase gating.
// Tags: TL-SIM-0xx (derived from architecture section 8) and BR-MAP-059 / BR-MAP-060 (netproto rules
// the boundary applies through netproto's own gating and connect checks).
#include "sim_fixture.hpp"

using namespace tooling::sim;

TEST("TL-SIM-010", players_connect_with_the_netproto_handshake_and_enter_the_world) {
  auto sim = simtest::make_sim({"rathena.base"});
  auto& a = simtest::add(*sim, "alice", 50, 50);
  CHECK(a.connected());
  CHECK(a.position().has_value() && a.position()->x == 50 && a.position()->y == 50);
  // the first thing a client sees is ZC_ACCEPT_ENTER (0x02eb)
  CHECK(a.count("accept") == 1);
  CHECK(a.history().front().name == "ZC_ACCEPT_ENTER" && a.history().front().opcode == 0x02eb);
}

TEST("TL-SIM-011", two_players_in_range_see_each_other_and_stop_seeing_each_other_when_one_walks_away) {
  auto sim = simtest::make_sim({"rathena.base"});
  auto& a = simtest::add(*sim, "alice", 100, 100);
  auto& b = simtest::add(*sim, "bob", 105, 100);
  // b entered the world second: a is told about b, b about a
  CHECK(a.wait_for("appear", 200).has_value());
  CHECK(b.wait_for("appear", 200).has_value());
  CHECK(a.sees(b.gid()) && b.sees(a.gid()));
  // the standing-entry frame carries the name and the cell (struct packet_idle_unit, 108 bytes)
  auto ev = a.last("ZC_NOTIFY_STANDENTRY11");
  CHECK(ev && ev->raw.size() == 108 && ev->text("actor") == "bob" && ev->num("x") == 105 && ev->num("y") == 100);
  // b walks 30 cells east: out of the 14-cell view of a
  b.walk(135, 100);
  CHECK(a.wait_for("vanish", 8000).has_value());
  CHECK(b.wait_for("vanish", 100).has_value());
  CHECK(!a.sees(b.gid()) && !b.sees(a.gid()));
  CHECK(b.wait_for("stop", 8000).has_value());
  CHECK(b.position()->x == 135);
}

TEST("TL-SIM-012", walking_takes_one_step_timer_per_cell_and_others_in_view_get_the_move) {
  auto sim = simtest::make_sim({"rathena.base"});
  auto& a = simtest::add(*sim, "alice", 100, 100);
  auto& b = simtest::add(*sim, "bob", 102, 100);
  a.drain_received();
  b.drain_received();
  const int64_t t0 = sim->now_ms();
  a.walk(107, 100);  // 7 cells at 150 ms
  auto mv = a.expect("move", 200);
  CHECK(mv.num("x0") == 100 && mv.num("x1") == 107 && mv.num("y1") == 100);
  auto stop = a.expect("stop", 3000);
  CHECK(stop.num("x") == 107);
  const int64_t took = sim->now_ms() - t0;
  if (!(took >= 7 * 150 && took <= 7 * 150 + 80)) throw CheckFailure("took " + std::to_string(took));  // engine-core step timers, quantised by the 20 ms tick
  // b saw it start (the walking form of the unit entry) and stop
  auto bm = b.expect("ZC_NOTIFY_MOVEENTRY11", 100);
  CHECK(bm.num("gid") == a.gid() && bm.num("to_x") == 107);
  CHECK(b.expect("stop", 3000).num("x") == 107);
}

TEST("TL-SIM-013", chat_reaches_players_in_view_and_echoes_to_the_speaker_only) {
  auto sim = simtest::make_sim({"rathena.base"});
  auto& a = simtest::add(*sim, "alice", 100, 100);
  auto& b = simtest::add(*sim, "bob", 104, 100);
  auto& far = simtest::add(*sim, "carol", 100, 140);
  a.drain_received(); b.drain_received(); far.drain_received();
  a.say("hello there");
  auto heard = b.expect("chat", 200);
  CHECK(heard.num("gid") == a.gid() && heard.text("text") == "alice : hello there");
  CHECK(a.expect("chat_self", 200).text("text") == "alice : hello there");
  CHECK(far.expect_none("chat", 200));
  CHECK(a.count("chat") == 0);  // the speaker gets the PLAYERCHAT echo, not the area packet
}

TEST("BR-MAP-059", a_packet_outside_its_session_phase_disconnects_and_unknown_opcodes_disconnect) {
  auto sim = simtest::make_sim({"rathena.base"});
  {
    // in-world packet before the handshake
    SimConfig dummy;
    Sim::PlayerSpec ps;
    ps.name = "early";
    ps.connect = false;
    auto& p = sim->add_player(ps);
    p.send_raw(wire::cz_request_move(10, 10, 0));
    sim->run_for(60);
    CHECK(p.disconnected() && p.disconnect_reason().find("not valid in this session phase") != std::string::npos);
  }
  {
    auto& p = simtest::add(*sim, "late", 60, 60);
    p.send_raw(wire::cz_enter(p.aid(), p.aid(), 1, 0, 1));  // a second connect packet in-world
    sim->run_for(60);
    CHECK(p.disconnected());
  }
  {
    auto& p = simtest::add(*sim, "weird", 70, 70);
    wire::Bytes junk = {0xff, 0x0f, 0x00, 0x00};  // 0x0fff: not in the table
    p.send_raw(junk);
    sim->run_for(60);
    CHECK(p.disconnected() && p.disconnect_reason() == "unknown opcode");
  }
  {
    auto& p = simtest::add(*sim, "liar", 80, 80);
    wire::Bytes f = wire::cz_request_chat("liar : x");
    wire::put16(f, 2, 2);  // declared length below the 4-byte header
    p.send_raw(f);
    sim->run_for(60);
    CHECK(p.disconnected() && p.disconnect_reason() == "invalid declared length");
  }
}

TEST("BR-MAP-059", at_most_three_packets_per_socket_per_cycle_the_rest_is_delayed_not_dropped) {
  auto sim = simtest::make_sim({"rathena.base"});
  auto& a = simtest::add(*sim, "alice", 100, 100);
  a.drain_received();
  for (int i = 0; i < 7; ++i) a.ping();  // 7 frames in one cycle
  sim->run_for(sim->config().tick_ms);   // one cycle: 3 are parsed
  CHECK(a.drain_received().size() == 3);
  sim->run_for(sim->config().tick_ms);
  CHECK(a.drain_received().size() == 3);
  sim->run_for(sim->config().tick_ms);
  CHECK(a.drain_received().size() == 1);
  CHECK(a.connected());
}

TEST("BR-MAP-060", the_connect_packet_is_validated_before_anything_else) {
  auto sim = simtest::make_sim({"rathena.base"});
  Sim::PlayerSpec ps;
  ps.name = "badacct";
  ps.connect = false;
  auto& p = sim->add_player(ps);
  p.send_raw(wire::cz_enter(5, 5, 1, 0, 1));  // account id below the legal range
  auto ref = p.expect("refuse", 200);
  CHECK(ref.num("code") == 2);
  CHECK(p.disconnected());
  // a sex byte other than 0/1
  ps.name = "badsex";
  auto& q = sim->add_player(ps);
  q.send_raw(wire::cz_enter(q.aid(), q.aid(), 1, 0, 7));
  CHECK(q.expect("refuse", 200).num("code") == 6);
  // the same account twice: the second session gets no answer at all (legacy sends nothing)
  auto& first = simtest::add(*sim, "first", 20, 20);
  ps.name = "dup";
  auto& dup = sim->add_player(ps);
  dup.send_raw(wire::cz_enter(first.aid(), first.aid(), 1, 0, 1));
  sim->run_for(100);
  CHECK(dup.drain_received().empty() && dup.disconnected());
}

TEST("TL-SIM-014", wait_for_times_out_in_virtual_time_and_expect_names_the_inbox) {
  auto sim = simtest::make_sim({"rathena.base"});
  auto& a = simtest::add(*sim, "alice", 100, 100);
  a.drain_received();
  const int64_t t0 = sim->now_ms();
  CHECK(!a.wait_for("chat", 500).has_value());
  CHECK(sim->now_ms() - t0 == 500);  // virtual time advanced exactly by the timeout
  bool threw = false;
  try { a.expect("chat", 40); } catch (const SimError& e) { threw = std::string(e.what()).find("expected event 'chat'") != std::string::npos; }
  CHECK(threw);
}

TEST("TL-SIM-015", a_crowd_connects_in_the_same_ticks_and_visibility_is_symmetric_everywhere) {
  auto sim = simtest::make_sim({"rathena.base"});
  std::vector<SimPlayer*> ps;
  for (int i = 0; i < 60; ++i) {
    Sim::PlayerSpec s;
    s.name = "c" + std::to_string(i);
    s.x = 20 + (i % 10) * 6;  // a 10 x 6 grid with 6 cells between neighbours: view range 14
    s.y = 20 + (i / 10) * 6;
    s.connect = false;
    ps.push_back(&sim->add_player(s));
  }
  SimPlayer::connect_all(*sim, ps);
  for (auto* p : ps) CHECK(p->connected());
  CHECK(sim->now_ms() <= 200);  // all of them were admitted within a handful of ticks, not one after the other
  for (auto* a : ps) {
    auto pa = *a->position();
    for (auto* b : ps) {
      if (a == b) continue;
      auto pb = *b->position();
      const bool near = std::max(std::abs(pa.x - pb.x), std::abs(pa.y - pb.y)) <= sim->config().view_range;
      if (a->sees(b->gid()) != near) throw CheckFailure(a->name() + " vs " + b->name() + ": sees=" + std::to_string(a->sees(b->gid())) + " near=" + std::to_string(near));
      if (a->sees(b->gid()) != b->sees(a->gid())) throw CheckFailure("asymmetric view");
    }
  }
}

TEST("TL-SIM-016", a_disconnect_removes_the_unit_and_tells_everyone_who_saw_it) {
  auto sim = simtest::make_sim({"rathena.base"});
  auto& a = simtest::add(*sim, "alice", 100, 100);
  auto& b = simtest::add(*sim, "bob", 103, 100);
  a.drain_received();
  b.disconnect();
  auto v = a.expect("vanish", 100);
  CHECK(v.num("gid") == b.gid() && v.num("type") == 3);  // 3: logged out
  CHECK(!a.sees(b.gid()) && b.disconnected());
  CHECK(!sim->position_of(b.gid()).has_value());          // gone from the ECS and the spatial index
  CHECK(sim->spatial().size() == 1);
  // frames the dead client sends are ignored, not a crash
  b.walk(120, 100);
  sim->run_for(100);
  CHECK(a.position()->x == 100);
}

TEST("TL-SIM-017", npc_dialogue_input_and_persistent_variables_and_disconnect_cancels) {
  simtest::TempDir d("npc");
  d.write("counter.txt",
          "-\tscript\tCounter\t-1,{\n"
          "\tvisits += 1;\n\tmes \"visit \" + visits;\n\tnext;\n\tinput .@n;\n\tmes \"got \" + .@n;\n\tclose;\n}\n");
  auto sim = simtest::make_sim({"rathena.base", "legacy.npc"});
  auto names = sim->load_npc_file(d.path / "counter.txt");
  CHECK(names == std::vector<std::string>{"Counter"});
  sim->place_npc("Counter", 101, 100);
  auto& p = simtest::add(*sim, "neo", 100, 100);
  auto visits = [&] {
    auto v = sim->storage().get(script_host::Scope::Character, std::to_string(p.gid()), "legacy.npc", "legacy.npc", "legacy:char/visits");
    return v ? std::get<int64_t>(*v) : int64_t(0);
  };
  p.click_npc("Counter");
  CHECK(p.expect("say", 300).text("text") == "visit 1");
  p.expect("next", 100);
  p.next();
  p.expect("input", 300);              // ZC_OPEN_EDITDLGSTR
  p.input("42");
  CHECK(p.expect("say", 300).text("text") == "got 42");
  p.expect("next", 100);
  p.next();
  p.expect("close", 300);
  CHECK(visits() == 1);                // the character variable lives in the persistence-backed KV store
  // second visit: the counter continues; then the client hangs up in the middle of the dialogue
  p.click_npc("Counter");
  CHECK(p.expect("say", 300).text("text") == "visit 2");
  CHECK(sim->dialogue_of(p.gid()).active);
  const uint32_t gid = p.gid();
  p.disconnect();
  CHECK(!sim->dialogues().has_dialogue(gid));  // a disconnect cancels the coroutine
  // the cancelled dialogue wrote its variable before it blocked (script-host flushes at each blocking command)
  auto v = sim->storage().get(script_host::Scope::Character, std::to_string(gid), "legacy.npc", "legacy.npc", "legacy:char/visits");
  CHECK(v && std::get<int64_t>(*v) == 2);
}

TEST("TL-SIM-019", a_skill_cast_reaches_the_server_but_the_rulesets_have_no_skill_formulas_so_nothing_is_invented) {
  auto sim = simtest::make_sim({"rathena.base", "ruleset.prerenewal"}, 1, [](SimConfig& c) { c.stage_test_support = true; });
  auto& p = simtest::add(*sim, "knight", 100, 100);
  uint32_t mob = sim->spawn_dummy("poring", 101, 100, "{kind='mob',level=1}");
  p.drain_received();
  CHECK(sim->skill_client_id("ruleset.prerenewal:skill/sm_bash") == 5);  // from the package's data/skills
  p.cast("ruleset.prerenewal:skill/sm_bash", mob, 5);
  sim->run_for(100);
  CHECK(sim->unsupported_skill_casts() == 1);
  CHECK(p.count("damage") == 0);
  CHECK_THROWS(p.cast("ruleset.prerenewal:skill/no_such_skill", mob));
}

TEST("TL-SIM-018", clicks_and_attacks_outside_the_view_and_dialogue_replies_without_a_dialogue_are_ignored) {
  simtest::TempDir d("npc2");
  d.write("far.txt", "-\tscript\tFar\t-1,{\n\tmes \"hi\";\n\tclose;\n}\n");
  auto sim = simtest::make_sim({"rathena.base", "legacy.npc"});
  sim->load_npc_file(d.path / "far.txt");
  sim->place_npc("Far", 200, 200);
  auto& p = simtest::add(*sim, "neo", 100, 100);
  p.drain_received();
  p.click_npc("Far");                        // 100 cells away: not in the view
  CHECK(p.expect_none("say", 200));
  CHECK(!sim->dialogue_of(p.gid()).active);
  p.next();                                  // a reply with no dialogue open
  p.answer(1);
  p.input("x");
  sim->run_for(100);
  CHECK(p.connected() && p.drain_received().empty());
  uint32_t dummy = sim->spawn_dummy("far dummy", 250, 250, "{kind='mob'}");
  p.attack(dummy);                           // outside the view too
  sim->run_for(100);
  CHECK(p.drain_received().empty());
}
