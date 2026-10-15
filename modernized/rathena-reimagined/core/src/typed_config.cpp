#include "rathena/core/typed_config.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>

namespace rathena::core {

namespace detail {

std::int64_t parse_switch(std::string_view key, std::string_view value) {
	std::string v(value);
	std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	if (v == "yes" || v == "on" || v == "true") {
		return 1;
	}
	if (v == "no" || v == "off" || v == "false") {
		return 0;
	}
	std::int64_t out = 0;
	const auto* b = v.data();
	const auto* e = v.data() + v.size();
	if (b != e && *b == '+') {
		++b;
	}
	const auto [p, ec] = std::from_chars(b, e, out);
	if (b == e || ec != std::errc{} || p != e) {
		throw ConfigError(ConfigErrc::InvalidValue, "config key '" + std::string(key) + "' expects an integer or yes/no");
	}
	return out;
}

std::int32_t checked_i32(std::string_view key, std::int64_t v, std::int64_t lo, std::int64_t hi) {
	if (v < lo || v > hi) {
		throw ConfigError(ConfigErrc::InvalidValue, "config key '" + std::string(key) + "' out of range [" +
		                                                std::to_string(lo) + ", " + std::to_string(hi) + "]");
	}
	return static_cast<std::int32_t>(v);
}

bool checked_bool(std::string_view key, std::int64_t v) {
	if (v != 0 && v != 1) {
		throw ConfigError(ConfigErrc::InvalidValue, "config key '" + std::string(key) + "' expects yes/no");
	}
	return v == 1;
}

template <class T>
T convert(std::string_view key, std::string_view value, std::int64_t lo, std::int64_t hi) {
	const auto raw = parse_switch(key, value);
	if constexpr (std::is_same_v<T, bool>) {
		return checked_bool(key, raw);
	} else {
		return checked_i32(key, raw, lo, hi);
	}
}

} // namespace detail

#define RATHENA_KNOB_APPLY(T, name, def, lo, hi)                         \
	if (key == #name) {                                                  \
		name##_ = detail::convert<T>(key, value, (lo), (hi));            \
		return true;                                                     \
	}
#define RATHENA_KNOB_INFO(T, name, def, lo, hi) \
	KnobInfo{#name, std::is_same_v<T, bool>, static_cast<std::int64_t>(def), (lo), (hi)},

#define RATHENA_DEFINE_TYPED_CONFIG(ClassName, KNOBS)                                                       \
	bool ClassName::apply(std::string_view key, std::string_view value) {                                   \
		KNOBS(RATHENA_KNOB_APPLY)                                                                           \
		return false;                                                                                       \
	}                                                                                                       \
	const std::vector<KnobInfo>& ClassName::schema() {                                                      \
		static const std::vector<KnobInfo> s = {KNOBS(RATHENA_KNOB_INFO)};                                  \
		return s;                                                                                           \
	}                                                                                                       \
	ClassName ClassName::overlaid_with(const Config& config, UnknownKeyPolicy policy,                       \
	                                   std::vector<std::string>* unknown_keys) const {                      \
		ClassName out = *this;                                                                              \
		for (const auto& e : config.entries()) {                                                            \
			if (!out.apply(e.key, e.value)) {                                                               \
				if (policy == UnknownKeyPolicy::Reject) {                                                   \
					throw ConfigError(ConfigErrc::UnknownKey, "unknown " #ClassName " key '" + e.key + "'"); \
				}                                                                                           \
				if (unknown_keys != nullptr) {                                                              \
					unknown_keys->push_back(e.key);                                                         \
				}                                                                                           \
			}                                                                                               \
		}                                                                                                   \
		out.validate();                                                                                     \
		return out;                                                                                         \
	}                                                                                                       \
	ClassName ClassName::from_config(const Config& config, UnknownKeyPolicy policy,                         \
	                                 std::vector<std::string>* unknown_keys) {                              \
		return ClassName{}.overlaid_with(config, policy, unknown_keys);                                     \
	}

RATHENA_DEFINE_TYPED_CONFIG(BattleConfig, RATHENA_BATTLE_KNOBS)
RATHENA_DEFINE_TYPED_CONFIG(InterConfig, RATHENA_INTER_KNOBS)
RATHENA_DEFINE_TYPED_CONFIG(CharConfig, RATHENA_CHAR_KNOBS)
RATHENA_DEFINE_TYPED_CONFIG(LoginPolicyConfig, RATHENA_LOGIN_KNOBS)

// Cross-knob invariants.
void BattleConfig::validate() const {
	if (min_hitrate_ > max_hitrate_) {
		throw ConfigError(ConfigErrc::InvalidValue, "min_hitrate must be <= max_hitrate");
	}
	if (item_drop_common_min_ > item_drop_common_max_) {
		throw ConfigError(ConfigErrc::InvalidValue, "item_drop_common_min must be <= item_drop_common_max");
	}
	if (max_parameter_ > max_third_parameter_) {
		throw ConfigError(ConfigErrc::InvalidValue, "max_parameter must be <= max_third_parameter");
	}
}
void InterConfig::validate() const {}
void CharConfig::validate() const {}
void LoginPolicyConfig::validate() const {}

} // namespace rathena::core
