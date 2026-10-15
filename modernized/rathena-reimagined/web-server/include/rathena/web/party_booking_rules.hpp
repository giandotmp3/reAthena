// rathena-web-server: party booking rules (BR-WEB-013, 014, 015).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "rathena/web/domain.hpp"

namespace rathena::web {

enum class BookingVerdict {
    Ok,
    WorldNameTooLong,  ///< > kWorldNameLength
    WorldNameEmpty,
    CharNameTooLong,   ///< > kCharNameLength (column is varchar(23))
    InvalidPurpose,    ///< >= BookingPurpose::Max
    CommentTooLong,    ///< > kCommentLength
};

std::string_view to_string(BookingVerdict v) noexcept;

BookingVerdict validate_world_name(std::string_view world_name) noexcept;

/// BR-WEB-013: field length limits and purpose range. (Leader check:
/// is_party_leader in auth_rules.hpp. Uniqueness per (world, AID, GID):
/// enforced by PartyBookingRepository::save_booking.)
BookingVerdict validate_booking(const PartyBooking& booking) noexcept;

/// BR-WEB-014 search criteria. Purpose All (0) means "any purpose".
struct BookingSearch {
    std::uint16_t minimum_level{0};
    std::uint16_t maximum_level{0};
    std::uint16_t purpose{static_cast<std::uint16_t>(BookingPurpose::All)};
    bool assist{false};
    bool damage_dealer{false};
    bool healer{false};
    bool tanker{false};
    std::string memo;
};

BookingVerdict validate_search(const BookingSearch& search) noexcept;

/// BR-WEB-014: exact min/max level match (SME open question #4: overlap?),
/// purpose filter unless All, requested roles OR-ed, memo substring
/// (ASCII case-insensitive, matching MySQL's default collation for LIKE).
bool booking_matches(const PartyBooking& booking, const BookingSearch& search);

/// BR-WEB-014 / BR-WEB-015: newest first (by `created`, stable).
void sort_newest_first(std::vector<PartyBooking>& bookings);

/// BR-WEB-014: filter + newest-first ordering.
std::vector<PartyBooking> search_bookings(std::vector<PartyBooking> bookings,
                                          const BookingSearch& search);

/// Wire shape of one booking (legacy key order kept). S3: serializer only.
nlohmann::ordered_json booking_to_json(const PartyBooking& booking);

}  // namespace rathena::web
