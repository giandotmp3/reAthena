#include "rathena/core/logging.hpp"

#include <algorithm>
#include <iostream>
#include <regex>

namespace rathena::core {

std::string_view to_string(LogLevel level) noexcept {
	switch (level) {
	case LogLevel::Debug: return "debug";
	case LogLevel::Info: return "info";
	case LogLevel::Warn: return "warn";
	case LogLevel::Error: return "error";
	}
	return "?";
}

void StderrSink::write(const LogRecord& r) {
	std::lock_guard lock(mu_);
	std::cerr << '[' << to_string(r.level) << "] " << r.component << ": " << r.message << '\n';
}

void MemorySink::write(const LogRecord& r) {
	std::lock_guard lock(mu_);
	records_.push_back(r);
}

std::vector<LogRecord> MemorySink::records() const {
	std::lock_guard lock(mu_);
	return records_;
}

namespace {

// key, separator (with optional quotes), value. Value stops at whitespace or a delimiter.
const std::regex& kv_pattern() {
	static const std::regex re(
		// Keys may carry a prefix (user_pass, log_db_pw, web_auth_token); over-masking is acceptable.
		R"(((?:^|[^A-Za-z0-9_])[A-Za-z0-9_]*(?:token|password|passwd|pass|pwd|_pw|pin|pincode|secret|login_id1|login_id2))(\s*["']?\s*[:=]\s*["']?)([^\s"'&,;}\]]+))",
		std::regex::ECMAScript | std::regex::icase);
	return re;
}

const std::regex& url_pattern() {
	static const std::regex re(R"(([A-Za-z][A-Za-z0-9+.\-]*://[^:/@\s]+:)([^@\s]+)@)");
	return re;
}

} // namespace

Redactor::Redactor() = default;

void Redactor::register_secret(std::string_view value) {
	if (value.size() < kMinRegisteredSecretLength) {
		return;
	}
	if (std::find(secrets_.begin(), secrets_.end(), value) == secrets_.end()) {
		secrets_.emplace_back(value);
		// Longest first so overlapping secrets are fully masked.
		std::sort(secrets_.begin(), secrets_.end(), [](const auto& a, const auto& b) { return a.size() > b.size(); });
	}
}

void Redactor::register_startup_secrets(const InterServerCredentials& inter, const DatabaseUrls& db) {
	register_secret(inter.pass);
	for (const auto* u : {&db.login, &db.char_db, &db.map, &db.web, &db.log}) {
		register_secret(u->password);
	}
}

std::string Redactor::redact(std::string_view text) const {
	std::string out(text);
	for (const auto& s : secrets_) {
		for (auto pos = out.find(s); pos != std::string::npos; pos = out.find(s, pos + kMask.size())) {
			out.replace(pos, s.size(), kMask);
		}
	}
	out = std::regex_replace(out, url_pattern(), "$1***@");
	out = std::regex_replace(out, kv_pattern(), "$1$2***");
	return out;
}

Logger::Logger(std::shared_ptr<LogSink> sink, std::shared_ptr<const Redactor> redactor, LogLevel min_level)
	: sink_(std::move(sink)), redactor_(redactor ? std::move(redactor) : std::make_shared<const Redactor>()),
	  min_level_(min_level) {}

void Logger::log(LogLevel level, std::string_view component, std::string_view message) const {
	if (level < min_level_ || !sink_) {
		return;
	}
	sink_->write(LogRecord{level, std::string(component), redactor_->redact(message)});
}

} // namespace rathena::core
