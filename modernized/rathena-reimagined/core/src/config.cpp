#include "rathena/core/config.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace rathena::core {

namespace fs = std::filesystem;

Config::Config(std::vector<ConfigEntry> entries, std::vector<std::string> warnings)
	: entries_(std::move(entries)), warnings_(std::move(warnings)) {}

const ConfigEntry* Config::find(std::string_view key) const {
	auto it = std::find_if(entries_.begin(), entries_.end(), [&](const ConfigEntry& e) { return e.key == key; });
	return it == entries_.end() ? nullptr : &*it;
}

std::optional<std::string_view> Config::get(std::string_view key) const {
	if (const auto* e = find(key)) {
		return std::string_view(e->value);
	}
	return std::nullopt;
}

Config Config::merged_with(const Config& overlay) const {
	auto entries = entries_;
	for (const auto& e : overlay.entries_) {
		auto it = std::find_if(entries.begin(), entries.end(), [&](const ConfigEntry& x) { return x.key == e.key; });
		if (it == entries.end()) {
			entries.push_back(e);
		} else {
			*it = e;
		}
	}
	auto warnings = warnings_;
	warnings.insert(warnings.end(), overlay.warnings_.begin(), overlay.warnings_.end());
	return Config(std::move(entries), std::move(warnings));
}

namespace {

std::string_view trim(std::string_view s) {
	const auto ws = " \t\r\n";
	const auto b = s.find_first_not_of(ws);
	if (b == std::string_view::npos) {
		return {};
	}
	const auto e = s.find_last_not_of(ws);
	return s.substr(b, e - b + 1);
}

// `//` starts a comment at line start or after whitespace, so `mysql://host` survives.
std::string_view strip_comment(std::string_view line) {
	for (std::size_t i = 0; i + 1 < line.size(); ++i) {
		if (line[i] == '/' && line[i + 1] == '/' && (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t')) {
			return line.substr(0, i);
		}
	}
	return line;
}

std::string location(const fs::path& file, std::uint32_t line) {
	return file.generic_string() + ":" + std::to_string(line);
}

class Parser {
public:
	Parser(const Environment& env, const ConfigLoader::Options& opts) : env_(env), opts_(opts) {}

	void parse_file(const fs::path& path, std::uint32_t depth) {
		std::ifstream in(path, std::ios::binary);
		if (!in) {
			throw ConfigError(ConfigErrc::FileNotFound, "config file not found: " + path.generic_string());
		}
		std::ostringstream ss;
		ss << in.rdbuf();
		parse_text(ss.str(), path, depth);
	}

	void parse_text(std::string_view text, const fs::path& path, std::uint32_t depth) {
		if (depth > opts_.max_import_depth) {
			throw ConfigError(ConfigErrc::ImportTooDeep, "import nesting too deep at " + path.generic_string());
		}
		const auto canonical = normalize(path);
		if (std::find(stack_.begin(), stack_.end(), canonical) != stack_.end()) {
			throw ConfigError(ConfigErrc::ImportCycle, "import cycle through " + path.generic_string());
		}
		stack_.push_back(canonical);

		std::uint32_t lineno = 0;
		std::size_t pos = 0;
		while (pos <= text.size()) {
			auto nl = text.find('\n', pos);
			if (nl == std::string_view::npos) {
				nl = text.size();
			}
			auto line = text.substr(pos, nl - pos);
			pos = nl + 1;
			++lineno;

			line = strip_comment(line);
			line = trim(line);
			if (line.empty()) {
				if (nl == text.size()) {
					break;
				}
				continue;
			}
			const auto colon = line.find(':');
			if (colon == std::string_view::npos) {
				throw ConfigError(ConfigErrc::Syntax, "expected 'key: value' at " + location(path, lineno));
			}
			const auto key = trim(line.substr(0, colon));
			const auto raw = trim(line.substr(colon + 1));
			if (key.empty()) {
				throw ConfigError(ConfigErrc::Syntax, "empty key at " + location(path, lineno));
			}

			Expansion exp;
			try {
				exp = expand_placeholders(raw, env_);
			} catch (const ConfigError& e) {
				throw ConfigError(e.code(), std::string(e.what()) + " (key '" + std::string(key) + "' at " + location(path, lineno) + ")");
			}

			if (key == "import") {
				handle_import(exp.value, path, depth);
			} else {
				define(ConfigEntry{std::string(key), std::move(exp.value), std::string(raw), path, lineno, exp.used_placeholder});
			}
			if (nl == text.size()) {
				break;
			}
		}
		stack_.pop_back();
	}

	Config finish() { return Config(std::move(entries_), std::move(warnings_)); }

private:
	static fs::path normalize(const fs::path& p) {
		std::error_code ec;
		auto c = fs::weakly_canonical(p, ec);
		return ec ? p.lexically_normal() : c;
	}

	void handle_import(const std::string& target, const fs::path& from, std::uint32_t depth) {
		fs::path p(target);
		if (p.is_relative()) {
			p = (opts_.import_base.empty() ? from.parent_path() : opts_.import_base) / p;
		}
		std::error_code ec;
		if (!fs::exists(p, ec)) {
			if (opts_.missing_import_is_error) {
				throw ConfigError(ConfigErrc::FileNotFound, "import not found: " + p.generic_string());
			}
			warnings_.push_back("import not found (skipped): " + p.generic_string());
			return;
		}
		parse_file(p, depth + 1);
	}

	void define(ConfigEntry e) {
		if (auto it = index_.find(e.key); it != index_.end()) {
			entries_[it->second] = std::move(e);
		} else {
			index_.emplace(e.key, entries_.size());
			entries_.push_back(std::move(e));
		}
	}

	const Environment& env_;
	const ConfigLoader::Options& opts_;
	std::vector<fs::path> stack_;
	std::vector<ConfigEntry> entries_;
	std::unordered_map<std::string, std::size_t> index_;
	std::vector<std::string> warnings_;
};

} // namespace

Config ConfigLoader::load_file(const fs::path& path) const {
	Parser p(env_, options_);
	p.parse_file(path, 0);
	return p.finish();
}

Config ConfigLoader::load_string(std::string_view text, const fs::path& virtual_path) const {
	Parser p(env_, options_);
	p.parse_text(text, virtual_path, 0);
	return p.finish();
}

} // namespace rathena::core
