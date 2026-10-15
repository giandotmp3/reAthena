// rathena-web-server: identifiers, limits and time types shared by the rules.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace rathena::web {

using AccountId = std::uint32_t;
using CharId = std::uint32_t;
using GuildId = std::uint32_t;
using PartyId = std::uint32_t;

/// Wall-clock instant at second resolution (all TTLs in this service are seconds).
using Timestamp = std::chrono::sys_seconds;

/// BR-WEB-005: emblem payload limit (legacy MAX_EMBLEM_SIZE).
inline constexpr std::size_t kMaxEmblemSize = 50'000;
/// BR-WEB-007: first stored emblem version.
inline constexpr std::uint32_t kEmblemStartVersion = 1;
/// BR-WEB-006: shipped `emblem_transparency_limit` (percent); >= 100 disables the check.
inline constexpr int kDefaultEmblemTransparencyLimit = 80;

/// BR-WEB-013: column widths of `party_bookings` (world_name varchar(32),
/// char_name varchar(23), comment varchar(255)).
inline constexpr std::size_t kWorldNameLength = 32;
inline constexpr std::size_t kCharNameLength = 23;
inline constexpr std::size_t kCommentLength = 255;

/// BR-WEB-001 / BR-LOGIN-034: web auth tokens are 16 characters.
inline constexpr std::size_t kWebTokenLength = 16;
/// BR-WEB-003: a token stays valid for ~10 s after logout.
inline constexpr std::chrono::seconds kTokenLogoutGrace{10};

}  // namespace rathena::web
