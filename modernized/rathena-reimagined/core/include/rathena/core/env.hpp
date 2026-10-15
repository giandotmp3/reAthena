// Injectable process environment (S1: secrets are read from the environment only).
#pragma once

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace rathena::core {

/// Read-only view of environment variables. Injected everywhere secrets or
/// ${VAR} placeholders are resolved so tests never touch the real process env.
class Environment {
public:
	virtual ~Environment() = default;
	virtual std::optional<std::string> get(std::string_view name) const = 0;
};

/// Production implementation backed by std::getenv.
class ProcessEnvironment final : public Environment {
public:
	std::optional<std::string> get(std::string_view name) const override;
};

/// In-memory implementation (test fake; also handy for tools).
class MapEnvironment final : public Environment {
public:
	MapEnvironment() = default;
	explicit MapEnvironment(std::map<std::string, std::string, std::less<>> vars) : vars_(std::move(vars)) {}

	void set(std::string name, std::string value) { vars_[std::move(name)] = std::move(value); }
	void unset(std::string_view name);
	std::optional<std::string> get(std::string_view name) const override;

private:
	std::map<std::string, std::string, std::less<>> vars_;
};

/// Result of expanding ${VAR} placeholders in a string.
struct Expansion {
	std::string value;
	bool used_placeholder = false; ///< true if at least one ${VAR} was substituted
};

/// Expands every `${NAME}` in `input` from `env`. `$${` is an escaped literal `${`.
/// Throws ConfigError(MissingEnv) naming the variable (never a value) when NAME is unset,
/// and ConfigError(Syntax) on an unterminated or empty placeholder.
Expansion expand_placeholders(std::string_view input, const Environment& env);

} // namespace rathena::core
