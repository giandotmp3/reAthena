// Typed, immutable configuration values (architecture §2: "BattleConfig is an immutable typed
// value ... with the shipped values as code defaults ... passed explicitly to rules").
//
//   BattleConfig        conf/battle/*.conf      (consumed by rules/ via const BattleConfig&)
//   InterConfig         conf/inter_athena.conf
//   CharConfig          conf/char_athena.conf
//   LoginPolicyConfig   conf/login_athena.conf
//
// Default-constructed values hold the shipped defaults. The only way to obtain a different
// value is from_config(), which validates types and ranges and returns a new object; there
// are no setters.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "rathena/core/config.hpp"
#include "rathena/core/knobs.def.hpp"

namespace rathena::core {

struct KnobInfo {
	std::string_view name;
	bool is_bool;
	std::int64_t shipped_default;
	std::int64_t min;
	std::int64_t max;
};

enum class UnknownKeyPolicy {
	Collect, ///< ignore keys outside the schema, report them via `unknown_keys` (default: conf files hold more knobs)
	Reject,  ///< throw ConfigError(UnknownKey)
};

namespace detail {
/// Parses a legacy config switch: yes/no/on/off/true/false or an integer.
std::int64_t parse_switch(std::string_view key, std::string_view value);
std::int32_t checked_i32(std::string_view key, std::int64_t v, std::int64_t lo, std::int64_t hi);
bool checked_bool(std::string_view key, std::int64_t v);
} // namespace detail

#define RATHENA_KNOB_GETTER(T, name, def, lo, hi) \
	constexpr T name() const noexcept { return name##_; }
#define RATHENA_KNOB_MEMBER(T, name, def, lo, hi) T name##_ = def;

#define RATHENA_DECLARE_TYPED_CONFIG(ClassName, KNOBS)                                              \
	class ClassName {                                                                               \
	public:                                                                                         \
		constexpr ClassName() = default;                                                            \
		KNOBS(RATHENA_KNOB_GETTER)                                                                  \
		/** Applies matching keys from `config` on top of the shipped defaults. */                 \
		static ClassName from_config(const Config& config,                                          \
		                             UnknownKeyPolicy policy = UnknownKeyPolicy::Collect,           \
		                             std::vector<std::string>* unknown_keys = nullptr);             \
		/** Returns a copy with `config`'s matching keys applied on top of *this. */                \
		ClassName overlaid_with(const Config& config,                                               \
		                        UnknownKeyPolicy policy = UnknownKeyPolicy::Collect,                \
		                        std::vector<std::string>* unknown_keys = nullptr) const;            \
		static const std::vector<KnobInfo>& schema();                                               \
		friend bool operator==(const ClassName&, const ClassName&) = default;                       \
                                                                                                    \
	private:                                                                                        \
		bool apply(std::string_view key, std::string_view value);                                   \
		void validate() const;                                                                      \
		KNOBS(RATHENA_KNOB_MEMBER)                                                                  \
	};

RATHENA_DECLARE_TYPED_CONFIG(BattleConfig, RATHENA_BATTLE_KNOBS)
RATHENA_DECLARE_TYPED_CONFIG(InterConfig, RATHENA_INTER_KNOBS)
RATHENA_DECLARE_TYPED_CONFIG(CharConfig, RATHENA_CHAR_KNOBS)
RATHENA_DECLARE_TYPED_CONFIG(LoginPolicyConfig, RATHENA_LOGIN_KNOBS)

} // namespace rathena::core
