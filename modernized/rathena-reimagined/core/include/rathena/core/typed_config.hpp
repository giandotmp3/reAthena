// Typed, immutable configuration values (architecture §2: "BattleConfig is an immutable typed
// value ... with the shipped values as code defaults ... passed explicitly to rules").
//
//   BattleConfig        conf/battle/*.conf      (consumed by rules/ via const BattleConfig&)
//   InterConfig         conf/inter_athena.conf
//   CharConfig          conf/char_athena.conf
//   LoginPolicyConfig   conf/login_athena.conf
//
// Default-constructed values hold the shipped defaults (renewal; shipped(GameMode) gives the
// pre-renewal ones, which differ for a few battle knobs that conf/battle leaves to the mode).
// The only way to obtain a different value is from_config(), which validates types and ranges
// and returns a new object; there are no setters.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "rathena/core/config.hpp"
#include "rathena/core/knobs.def.hpp"

namespace rathena::core {

struct KnobInfo {
	std::string_view name; ///< conf key
	bool is_bool;
	std::int64_t shipped_default; ///< renewal (the default mode)
	std::int64_t min;
	std::int64_t max;
	std::int64_t shipped_default_pre_renewal;
};

/// Game mode; the legacy server picks it at build time (src/config/renewal.hpp).
enum class GameMode : std::uint8_t { Renewal, PreRenewal };

enum class UnknownKeyPolicy {
	Collect, ///< ignore keys outside the schema, report them via `unknown_keys` (default: conf files hold more knobs)
	Reject,  ///< throw ConfigError(UnknownKey)
};

namespace detail {
/// Parses a legacy config switch: yes/no/on/off/true/false or an integer.
std::int64_t parse_switch(std::string_view key, std::string_view value);
std::int32_t checked_i32(std::string_view key, std::int64_t v, std::int64_t lo, std::int64_t hi);
bool checked_bool(std::string_view key, std::int64_t v);

/// Conf key of a knob: its name, except for keys that are not identifiers ("feature.banking").
constexpr std::string_view knob_conf_key(std::string_view name) noexcept {
#define RATHENA_KNOB_CONF_KEY_CASE(ident, key) \
	if (name == #ident) return key;
	RATHENA_BATTLE_KNOB_CONF_KEYS(RATHENA_KNOB_CONF_KEY_CASE)
#undef RATHENA_KNOB_CONF_KEY_CASE
	return name;
}
} // namespace detail

// Knob lists without mode-dependent defaults.
#define RATHENA_NO_MODE_DEFAULTS(P)

#define RATHENA_KNOB_GETTER(T, name, def, lo, hi) \
	constexpr T name() const noexcept { return name##_; }
#define RATHENA_KNOB_MEMBER(T, name, def, lo, hi) T name##_ = def;
#define RATHENA_KNOB_SET_PRE_RENEWAL(name, value) c.name##_ = value;

#define RATHENA_DECLARE_TYPED_CONFIG(ClassName, KNOBS, PRE_RENEWAL_DEFAULTS)                        \
	class ClassName {                                                                               \
	public:                                                                                         \
		constexpr ClassName() = default;                                                            \
		KNOBS(RATHENA_KNOB_GETTER)                                                                  \
		/** The shipped defaults for `mode` (default construction gives the renewal ones). */      \
		static constexpr ClassName shipped(GameMode mode) noexcept {                                \
			ClassName c;                                                                            \
			if (mode == GameMode::PreRenewal) {                                                     \
				PRE_RENEWAL_DEFAULTS(RATHENA_KNOB_SET_PRE_RENEWAL)                                  \
			}                                                                                       \
			return c;                                                                               \
		}                                                                                           \
		/** Applies matching keys from `config` on top of the shipped defaults. */                 \
		static ClassName from_config(const Config& config,                                          \
		                             UnknownKeyPolicy policy = UnknownKeyPolicy::Collect,           \
		                             std::vector<std::string>* unknown_keys = nullptr);             \
		/** Same, on top of the shipped defaults for `mode`. */                                    \
		static ClassName from_config(const Config& config, GameMode mode,                           \
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

RATHENA_DECLARE_TYPED_CONFIG(BattleConfig, RATHENA_BATTLE_KNOBS, RATHENA_BATTLE_PRE_RENEWAL_DEFAULTS)
RATHENA_DECLARE_TYPED_CONFIG(InterConfig, RATHENA_INTER_KNOBS, RATHENA_NO_MODE_DEFAULTS)
RATHENA_DECLARE_TYPED_CONFIG(CharConfig, RATHENA_CHAR_KNOBS, RATHENA_NO_MODE_DEFAULTS)
RATHENA_DECLARE_TYPED_CONFIG(LoginPolicyConfig, RATHENA_LOGIN_KNOBS, RATHENA_NO_MODE_DEFAULTS)

} // namespace rathena::core
