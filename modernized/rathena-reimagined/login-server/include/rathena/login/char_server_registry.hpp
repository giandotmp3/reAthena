#pragma once
// Char-server connections (BR-LOGIN-020/021/025, BR-INTER-005/006/014).

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "rathena/login/account.hpp"
#include "rathena/login/config.hpp"
#include "rathena/login/types.hpp"

namespace rathena::login {

struct CharServerInfo {
  std::string name;
  Ipv4 ip = 0;
  std::uint16_t port = 0;
  std::uint16_t type = 0;
  std::uint16_t is_new = 0;
  int users = 0;
};

enum class RegistrationResult { Accepted, BadCredentials, NotServerAccount, IdOutOfRange, SlotTaken };

class CharServerRegistry {
 public:
  /// BR-LOGIN-025 / BR-INTER-014: credentials must equal the env-provided pair,
  /// the account must be sex 'S' with id < MAX_SERVERS (5), and the slot free.
  RegistrationResult try_register(const InterServerCredentials& expected,
                                  std::string_view presented_user,
                                  std::string_view presented_pass,
                                  const Account& server_account,
                                  const CharServerInfo& info);
  void unregister(int slot);
  int connected_count() const;
  std::vector<int> connected_slots() const;
  const std::optional<CharServerInfo>& slot(int i) const { return slots_.at(static_cast<std::size_t>(i)); }
  /// BR-INTER-006: char server answers 0x2735 with 0x2736 carrying its new IP.
  bool update_ip(int slot, Ipv4 new_ip);

 private:
  std::array<std::optional<CharServerInfo>, kMaxServers> slots_{};
};

/// BR-LOGIN-020: client in a configured LAN subnet gets the LAN char IP.
Ipv4 select_char_ip(const LoginConfig& cfg, Ipv4 client_ip, Ipv4 public_char_ip);

/// BR-LOGIN-021: population display value (PACKETVER >= 20170726).
/// 0 green, 1 yellow, 2 red, 3 purple, 4 hidden.
int population_display(const LoginConfig& cfg, int users);

}  // namespace rathena::login
