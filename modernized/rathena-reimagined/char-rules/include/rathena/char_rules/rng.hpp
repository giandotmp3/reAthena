// Injectable randomness for char rules (architecture §3.2 step 2, §6 "Determinism").
//
// Shape-compatible with rathena::core::Rng (next_u32 + below). This unit is scaffolded as a
// standalone CMake project, so it declares the minimal port it needs; when the unit moves into
// src/rules/ the alias below is pointed at core's Rng and this header shrinks to a using-decl.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace rathena::char_rules {

class Rng {
public:
	virtual ~Rng() = default;
	virtual std::uint32_t next_u32() = 0;

	/// Legacy `rnd() % bound` (char.cpp start-point selection). bound == 0 returns 0.
	std::uint32_t below(std::uint32_t bound) { return bound == 0 ? 0u : next_u32() % bound; }
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

} // namespace rathena::char_rules
