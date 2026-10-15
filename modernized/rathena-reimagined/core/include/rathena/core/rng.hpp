// Injectable random number source (architecture §3.2 step 1, §6 "Determinism").
//
// Every rule takes `Rng&` explicitly, so tests seed per call site instead of relying on
// global stream order. Range mapping is implemented here (not via std::*_distribution,
// whose output is implementation-defined) so a seed gives the same stream on MSVC, gcc,
// clang and Apple clang (K4).
#pragma once

#include <cstdint>
#include <initializer_list>
#include <random>
#include <vector>

namespace rathena::core {

class Rng {
public:
	virtual ~Rng() = default;

	/// Raw 32-bit output; the only primitive implementations provide.
	virtual std::uint32_t next_u32() = 0;

	/// Uniform integer in [0, bound). bound == 0 returns 0. Unbiased (rejection sampling).
	std::uint32_t below(std::uint32_t bound);
	/// Uniform integer in [min, max] (inclusive; arguments may be given in either order).
	std::int32_t range(std::int32_t min, std::int32_t max);
	/// True with probability numerator/denominator, e.g. chance(89, 1000) for an 8.9% crit
	/// (legacy `rnd() % 1000 < x` call sites). numerator >= denominator always succeeds.
	bool chance(std::uint32_t numerator, std::uint32_t denominator);
};

/// Deterministic implementation: std::mt19937 (output sequence is fixed by the standard).
class SeededRng final : public Rng {
public:
	explicit SeededRng(std::uint32_t seed) : engine_(seed) {}
	std::uint32_t next_u32() override { return static_cast<std::uint32_t>(engine_()); }

private:
	std::mt19937 engine_;
};

/// Test fake: replays a fixed sequence of raw values, cycling when exhausted.
class SequenceRng final : public Rng {
public:
	SequenceRng(std::initializer_list<std::uint32_t> values) : values_(values) {}
	explicit SequenceRng(std::vector<std::uint32_t> values) : values_(std::move(values)) {}
	std::uint32_t next_u32() override;
	std::size_t calls() const noexcept { return calls_; }

private:
	std::vector<std::uint32_t> values_;
	std::size_t calls_ = 0;
};

} // namespace rathena::core
