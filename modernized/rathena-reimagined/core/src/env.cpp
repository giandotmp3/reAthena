#include "rathena/core/env.hpp"

#include <cctype>
#include <cstdlib>

#include "rathena/core/config.hpp"

namespace rathena::core {

std::optional<std::string> ProcessEnvironment::get(std::string_view name) const {
	const std::string key(name);
	if (const char* v = std::getenv(key.c_str()); v != nullptr) {
		return std::string(v);
	}
	return std::nullopt;
}

void MapEnvironment::unset(std::string_view name) {
	if (auto it = vars_.find(name); it != vars_.end()) {
		vars_.erase(it);
	}
}

std::optional<std::string> MapEnvironment::get(std::string_view name) const {
	if (auto it = vars_.find(name); it != vars_.end()) {
		return it->second;
	}
	return std::nullopt;
}

namespace {
bool valid_env_name(std::string_view name) {
	if (name.empty()) {
		return false;
	}
	for (char c : name) {
		const auto uc = static_cast<unsigned char>(c);
		if (!(std::isalnum(uc) || c == '_')) {
			return false;
		}
	}
	return !std::isdigit(static_cast<unsigned char>(name.front()));
}
} // namespace

Expansion expand_placeholders(std::string_view input, const Environment& env) {
	Expansion out;
	out.value.reserve(input.size());
	for (std::size_t i = 0; i < input.size();) {
		if (input.compare(i, 3, "$${") == 0) {
			out.value += "${";
			i += 3;
			continue;
		}
		if (input.compare(i, 2, "${") == 0) {
			const auto close = input.find('}', i + 2);
			if (close == std::string_view::npos) {
				throw ConfigError(ConfigErrc::Syntax, "unterminated ${ placeholder");
			}
			const auto name = input.substr(i + 2, close - (i + 2));
			if (!valid_env_name(name)) {
				throw ConfigError(ConfigErrc::Syntax, "invalid placeholder name '" + std::string(name) + "'");
			}
			auto value = env.get(name);
			if (!value) {
				throw ConfigError(ConfigErrc::MissingEnv, "environment variable " + std::string(name) + " is not set");
			}
			out.value += *value;
			out.used_placeholder = true;
			i = close + 1;
			continue;
		}
		out.value += input[i++];
	}
	return out;
}

} // namespace rathena::core
