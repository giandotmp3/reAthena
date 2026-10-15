// Injectable randomness for map rules (architecture §3.2 step 1, §6 "Determinism").
//
// Shape-compatible with rathena::core::Rng (next_u32 + below + chance). This unit is scaffolded as
// a standalone CMake project, so it declares the minimal port it needs; when the unit moves into
// src/rules/ this header becomes `using Rng = rathena::core::Rng;`.
//
// Range mapping mirrors the legacy `rnd() % n` call sites (battle.cpp uses `rnd()%1000 < cri`),
// so golden vectors captured from an instrumented legacy build can replay raw values directly.
#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

namespace rathena::map_rules {

class Rng {
public:
	virtual ~Rng() = default;
	virtual std::uint32_t next_u32() = 0;

	/// Legacy `rnd() % bound`. bound == 0 returns 0.
	std::uint32_t below(std::uint32_t bound) { return bound == 0 ? 0u : next_u32() % bound; }
	/// Legacy `rnd() % denominator < numerator`.
	bool chance(std::uint32_t numerator, std::uint32_t denominator) {
		if (numerator >= denominator) {
			return true;
		}
		return below(denominator) < numerator;
	}
};

/// Deterministic implementation for property tests (std::mt19937 output is fixed by the standard).
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
	explicit SequenceRng(std::vector<std::uint32_t> values) : values_(std::move(values)) {}
	std::uint32_t next_u32() override {
		if (values_.empty()) {
			return 0;
		}
		const auto v = values_[calls_ % values_.size()];
		++calls_;
		return v;
	}
	std::size_t calls() const noexcept { return calls_; }

private:
	std::vector<std::uint32_t> values_;
	std::size_t calls_ = 0;
};

} // namespace rathena::map_rules
