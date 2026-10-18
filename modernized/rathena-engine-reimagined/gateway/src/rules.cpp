#include "gateway/rules.hpp"

#include <algorithm>
#include <cctype>
#include <vector>

namespace gateway::rules {

PopulationColour population_colour(int users, bool display_enabled) {
  if (!display_enabled) return PopulationColour::Hidden;
  if (users <= 200) return PopulationColour::Green;
  if (users <= 500) return PopulationColour::Yellow;
  if (users <= 1000) return PopulationColour::Red;
  return PopulationColour::Purple;
}

int clamp_slots(int slots, int min_chars, int max_chars) {
  if (slots <= 0) return min_chars;
  return std::min(slots, max_chars);
}

int account_char_slots(int account_slots, int chars_per_account, bool vip_active, int vip_increase, int max_chars) {
  int s = account_slots > 0 ? account_slots : chars_per_account;
  if (vip_active) s += vip_increase;
  return std::min(s, max_chars);
}

std::optional<Timestamp> extend_ban(Timestamp now, Timestamp current, Timestamp delta) {
  Timestamp r = std::max(now, current) + delta;
  if (r <= now) return std::nullopt;
  return r;
}

Timestamp adjust_char_ban(Timestamp now, Timestamp current, Timestamp delta) {
  Timestamp r = std::max(now, current) + delta;
  return r < now ? 0 : r;
}

bool pin_is_legal(const std::string& pin) {
  if (pin.size() != 4) return false;
  for (char c : pin) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  int d0 = (pin[1] - pin[0] + 10) % 10;
  bool pattern = true;
  for (int i = 1; i < 3; ++i) pattern = pattern && ((pin[i + 1] - pin[i] + 10) % 10 == d0);
  return !(pattern && (d0 == 0 || d0 == 1 || d0 == 9));
}

bool starting_stats_valid(const StartStats& s, bool new_client) {
  if (new_client) {
    return s.str == 1 && s.agi == 1 && s.vit == 1 && s.int_ == 1 && s.dex == 1 && s.luk == 1;
  }
  const int v[] = {s.str, s.agi, s.vit, s.int_, s.dex, s.luk};
  int sum = 0;
  for (int x : v) { if (x < 1 || x > 9) return false; sum += x; }
  return sum == 30 && s.str + s.int_ == 10 && s.agi + s.luk == 10 && s.vit + s.dex == 10;
}

HpSp initial_hp_sp(int vit, int int_) { return {40 * (100 + vit) / 100, 11 * (100 + int_) / 100}; }

int create_error_wire_code(int internal) {
  switch (internal) {
    case -1: return 0x00;
    case -2: return 0xFF;
    case -3: return 0x01;
    case -4: return 0x03;
    default: return 0xFF;
  }
}

static std::vector<std::string> split_dots(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) { if (c == '.') { out.push_back(cur); cur.clear(); } else cur += c; }
  out.push_back(cur);
  return out;
}

bool ip_mask_matches(const std::string& mask, const std::string& ip) {
  auto m = split_dots(mask), a = split_dots(ip);
  if (m.size() != 4 || a.size() != 4) return false;
  for (int i = 0; i < 4; ++i) if (m[i] != "*" && m[i] != a[i]) return false;
  return true;
}

std::string ip_to_class_c_mask(const std::string& ip) {
  auto a = split_dots(ip);
  if (a.size() != 4) return ip;
  return a[0] + "." + a[1] + "." + a[2] + ".*";
}

bool char_packet_allowed(SessionPhase phase, CharPacket p) {
  switch (phase) {
    case SessionPhase::Unauthenticated: return p == CharPacket::Connect;
    case SessionPhase::AwaitingPin:
      return p == CharPacket::Connect || p == CharPacket::Ping || p == CharPacket::PinCheck ||
             p == CharPacket::PinSet || p == CharPacket::CharList;
    case SessionPhase::Ready: return true;
  }
  return false;
}

bool declared_length_ok(std::size_t declared, std::size_t header_len) {
  return declared >= header_len && declared <= 32768;
}

}  // namespace gateway::rules
