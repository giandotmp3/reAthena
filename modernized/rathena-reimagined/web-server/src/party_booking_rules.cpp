#include "rathena/web/party_booking_rules.hpp"

#include <algorithm>
#include <cctype>

namespace rathena::web {
namespace {

bool icontains(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    const auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                                [](char a, char b) {
                                    return std::tolower(static_cast<unsigned char>(a)) ==
                                           std::tolower(static_cast<unsigned char>(b));
                                });
    return it != haystack.end();
}

}  // namespace

std::string_view to_string(BookingVerdict v) noexcept {
    switch (v) {
        case BookingVerdict::Ok: return "ok";
        case BookingVerdict::WorldNameTooLong: return "world_name_too_long";
        case BookingVerdict::WorldNameEmpty: return "world_name_empty";
        case BookingVerdict::CharNameTooLong: return "char_name_too_long";
        case BookingVerdict::InvalidPurpose: return "invalid_purpose";
        case BookingVerdict::CommentTooLong: return "comment_too_long";
    }
    return "unknown";
}

BookingVerdict validate_world_name(std::string_view world_name) noexcept {
    if (world_name.empty()) return BookingVerdict::WorldNameEmpty;
    if (world_name.size() > kWorldNameLength) return BookingVerdict::WorldNameTooLong;
    return BookingVerdict::Ok;
}

BookingVerdict validate_booking(const PartyBooking& b) noexcept {
    if (auto v = validate_world_name(b.world_name); v != BookingVerdict::Ok) return v;
    if (b.char_name.size() > kCharNameLength) return BookingVerdict::CharNameTooLong;
    if (b.purpose >= static_cast<std::uint16_t>(BookingPurpose::Max)) return BookingVerdict::InvalidPurpose;
    if (b.comment.size() > kCommentLength) return BookingVerdict::CommentTooLong;
    return BookingVerdict::Ok;
}

BookingVerdict validate_search(const BookingSearch& s) noexcept {
    if (s.purpose >= static_cast<std::uint16_t>(BookingPurpose::Max)) return BookingVerdict::InvalidPurpose;
    if (s.memo.size() > kCommentLength) return BookingVerdict::CommentTooLong;
    return BookingVerdict::Ok;
}

bool booking_matches(const PartyBooking& b, const BookingSearch& s) {
    if (b.minimum_level != s.minimum_level || b.maximum_level != s.maximum_level) return false;
    if (s.purpose != static_cast<std::uint16_t>(BookingPurpose::All) && b.purpose != s.purpose) return false;
    const bool any_role = s.assist || s.damage_dealer || s.healer || s.tanker;
    if (any_role) {
        const bool role_hit = (s.assist && b.assist) || (s.damage_dealer && b.damage_dealer) ||
                              (s.healer && b.healer) || (s.tanker && b.tanker);
        if (!role_hit) return false;
    }
    return icontains(b.comment, s.memo);
}

void sort_newest_first(std::vector<PartyBooking>& bookings) {
    std::stable_sort(bookings.begin(), bookings.end(),
                     [](const PartyBooking& a, const PartyBooking& b) { return a.created > b.created; });
}

std::vector<PartyBooking> search_bookings(std::vector<PartyBooking> bookings, const BookingSearch& search) {
    std::erase_if(bookings, [&](const PartyBooking& b) { return !booking_matches(b, search); });
    sort_newest_first(bookings);
    return bookings;
}

nlohmann::ordered_json booking_to_json(const PartyBooking& b) {
    nlohmann::ordered_json j;
    j["AID"] = b.account_id;
    j["GID"] = b.char_id;
    j["CharName"] = b.char_name;
    j["WorldName"] = b.world_name;
    j["Tanker"] = b.tanker ? 1 : 0;
    j["Healer"] = b.healer ? 1 : 0;
    j["Dealer"] = b.damage_dealer ? 1 : 0;
    j["Assist"] = b.assist ? 1 : 0;
    j["MinLV"] = b.minimum_level;
    j["MaxLV"] = b.maximum_level;
    j["Memo"] = b.comment;
    j["Type"] = b.purpose;
    return j;
}

}  // namespace rathena::web
