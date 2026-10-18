#pragma once
// Pure, side-effect-free rule functions (no I/O, no clock). Fully implemented.
#include <optional>
#include <string>

#include "gateway/types.hpp"

namespace gateway::rules {

enum class PopulationColour { Green = 0, Yellow = 1, Red = 2, Purple = 3, Hidden = 4 };
PopulationColour population_colour(int users, bool display_enabled);  // BR-LOGIN-021

// BR-LOGIN-031 / BR-CHAR-024
int account_char_slots(int account_slots, int chars_per_account, bool vip_active, int vip_increase, int max_chars);
int clamp_slots(int slots, int min_chars, int max_chars);

// BR-LOGIN-026 / BR-CHAR-016: extend ban from max(now, current).
// Returns nullopt when the result is <= now (login: ignored).
std::optional<Timestamp> extend_ban(Timestamp now, Timestamp current, Timestamp delta);
// BR-CHAR-016: character variant, a result below now becomes 0 (cleared).
Timestamp adjust_char_ban(Timestamp now, Timestamp current, Timestamp delta);

// BR-CHAR-020
bool pin_is_legal(const std::string& pin);

// BR-CHAR-005
struct StartStats { int str, agi, vit, int_, dex, luk; };
bool starting_stats_valid(const StartStats& s, bool new_client);

// BR-CHAR-007
struct HpSp { int hp; int sp; };
HpSp initial_hp_sp(int vit, int int_);

// BR-CHAR-008: internal create error (-1..-4) to wire byte.
int create_error_wire_code(int internal);

// BR-LOGIN-022: does `mask` ("a.b.c.d", "a.b.c.*", "a.b.*.*", "a.*.*.*") match `ip`?
bool ip_mask_matches(const std::string& mask, const std::string& ip);
// "10.1.2.77" -> "10.1.2.*"
std::string ip_to_class_c_mask(const std::string& ip);

// BR-CHAR-022 / S5: packet gate by session phase.
enum class SessionPhase { Unauthenticated, AwaitingPin, Ready };
enum class CharPacket { Connect, Ping, PinCheck, PinSet, CharList, SelectChar, MakeChar, DeleteChar, Other };
bool char_packet_allowed(SessionPhase phase, CharPacket p);

// S4: declared length must be >= header length (and <= 32768).
bool declared_length_ok(std::size_t declared, std::size_t header_len);

}  // namespace gateway::rules
