#pragma once
// Zeny: checked currency value. Invariant 0 <= value <= INT32_MAX (BR-MAP-045; MAX_ZENY = INT_MAX).
// Storage stays a signed INT with CHECK (zeny >= 0) after M4 (architecture §5, no BIGINT rewrite).
#include <compare>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>

namespace rathena::persistence {

class ZenyRangeError : public std::out_of_range {
public:
	using std::out_of_range::out_of_range;
};

struct ZenyCredit;

class Zeny {
public:
	static constexpr std::int32_t kMax = std::numeric_limits<std::int32_t>::max();

	constexpr Zeny() = default;

	/// Throws ZenyRangeError unless 0 <= v <= kMax.
	static Zeny of(std::int64_t v);
	/// Value read from a DB column; nullopt for anything outside the invariant (corrupt row).
	static std::optional<Zeny> from_db(std::int64_t raw) noexcept;

	[[nodiscard]] constexpr std::int32_t value() const noexcept { return value_; }

	/// Adds a non-negative amount. Credits clamp at kMax (BR-MAP-045).
	/// Throws std::invalid_argument for a negative amount (use debit()).
	[[nodiscard]] ZenyCredit credit(std::int64_t amount) const;

	/// Subtracts a non-negative amount. nullopt if the balance would go negative or the amount
	/// is negative: zeny never goes below 0 (BR-MAP-045).
	[[nodiscard]] std::optional<Zeny> debit(std::int64_t amount) const noexcept;

	friend constexpr auto operator<=>(const Zeny&, const Zeny&) = default;

private:
	constexpr explicit Zeny(std::int32_t v) : value_(v) {}
	std::int32_t value_ = 0;
};

struct ZenyCredit {
	Zeny balance;            ///< new balance, clamped at Zeny::kMax
	std::int64_t clamped{};  ///< amount that did not fit (0 when nothing was lost)
};

} // namespace rathena::persistence
