// Config loader for the legacy `key: value` conf format with `import:` overlays (CAP-26).
//
// Format (upstream-compatible, K2):
//   - one `key: value` per line; text after `//` is a comment
//   - `import: <path>` loads another file at that point; later definitions override earlier ones
//   - values may contain ${ENV_VAR} placeholders, resolved from an injected Environment (S1)
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "rathena/core/env.hpp"

namespace rathena::core {

enum class ConfigErrc {
	FileNotFound,   ///< the root file is missing (missing imports are warnings)
	Syntax,         ///< malformed line or placeholder
	ImportCycle,    ///< a file imports itself (directly or transitively)
	ImportTooDeep,  ///< nesting beyond ConfigLoader::Options::max_import_depth
	MissingEnv,     ///< ${VAR} referenced but unset
	InvalidValue,   ///< typed conversion / range check failed
	UnknownKey,     ///< strict typed loading saw a key outside the schema
};

/// Error type for configuration problems. Messages carry file/line/key names,
/// never values, so a misconfigured secret cannot leak through an exception.
class ConfigError : public std::runtime_error {
public:
	ConfigError(ConfigErrc code, const std::string& what) : std::runtime_error(what), code_(code) {}
	ConfigErrc code() const noexcept { return code_; }

private:
	ConfigErrc code_;
};

struct ConfigEntry {
	std::string key;
	std::string value;           ///< after ${VAR} expansion
	std::string raw;             ///< as written in the file (placeholders unexpanded)
	std::filesystem::path file;  ///< where the winning definition came from
	std::uint32_t line = 0;
	bool from_env = false;       ///< value contained at least one ${VAR} placeholder
};

/// Immutable result of loading a config tree. Lookup returns the last definition (overlay wins).
class Config {
public:
	Config() = default;
	explicit Config(std::vector<ConfigEntry> entries, std::vector<std::string> warnings = {});

	const ConfigEntry* find(std::string_view key) const;
	std::optional<std::string_view> get(std::string_view key) const;
	bool contains(std::string_view key) const { return find(key) != nullptr; }

	/// Winning entries in first-definition order.
	const std::vector<ConfigEntry>& entries() const noexcept { return entries_; }
	/// Non-fatal diagnostics (e.g. optional import overlay not found).
	const std::vector<std::string>& warnings() const noexcept { return warnings_; }

	/// Returns a copy with `overlay`'s entries applied on top of this one.
	Config merged_with(const Config& overlay) const;

private:
	std::vector<ConfigEntry> entries_;
	std::vector<std::string> warnings_;
};

class ConfigLoader {
public:
	struct Options {
		/// Base for relative `import:` paths. Empty = directory of the importing file.
		/// Set to the server working directory for legacy `import: conf/import/x.txt` semantics.
		std::filesystem::path import_base;
		std::uint32_t max_import_depth = 16;
		/// Legacy behaviour: a missing import overlay is a warning, not an error.
		bool missing_import_is_error = false;
	};

	explicit ConfigLoader(const Environment& env) : ConfigLoader(env, Options{}) {}
	ConfigLoader(const Environment& env, Options options) : env_(env), options_(std::move(options)) {}

	Config load_file(const std::filesystem::path& path) const;
	/// Parses in-memory text; `virtual_path` is used for diagnostics and relative imports.
	Config load_string(std::string_view text, const std::filesystem::path& virtual_path) const;

private:
	const Environment& env_;
	Options options_;
};

} // namespace rathena::core
