// Logging with a mandatory redaction filter (S2, BR-WEB-017).
//
// Every record passes through the Redactor before it reaches any sink; there is no
// unredacted path. The Redactor masks:
//   - values of credential-like keys in key=value / key: value / "key":"value" form
//     (password, passwd, pass, pwd, pin, secret, token, authtoken, auth_token,
//      web_auth_token, login_id1, login_id2, userid is NOT masked)
//   - the password part of URLs (scheme://user:***@host)
//   - any secret value registered at startup (e.g. ${INTER_SERVER_PASS}, DB passwords)
//
// Production wiring: an spdlog sink adapter implements LogSink (architecture §4); this
// unit ships StderrSink and MemorySink so it has no third-party runtime dependency.
#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "rathena/core/secrets.hpp"

namespace rathena::core {

enum class LogLevel { Debug, Info, Warn, Error };
std::string_view to_string(LogLevel level) noexcept;

struct LogRecord {
	LogLevel level;
	std::string component;
	std::string message; ///< already redacted
};

class LogSink {
public:
	virtual ~LogSink() = default;
	virtual void write(const LogRecord& record) = 0;
};

class StderrSink final : public LogSink {
public:
	void write(const LogRecord& record) override;

private:
	std::mutex mu_;
};

/// Captures records in memory (test fake, also used by the login dual-run harness).
class MemorySink final : public LogSink {
public:
	void write(const LogRecord& record) override;
	std::vector<LogRecord> records() const;

private:
	mutable std::mutex mu_;
	std::vector<LogRecord> records_;
};

class Redactor {
public:
	static constexpr std::string_view kMask = "***";
	/// Registered secrets shorter than this are ignored (they would mask ordinary text);
	/// startup policy already refuses such short credentials.
	static constexpr std::size_t kMinRegisteredSecretLength = 4;

	Redactor();
	void register_secret(std::string_view value);
	void register_secret(const Secret& secret) { register_secret(secret.reveal()); }
	/// Registers the inter-server password and every DB password.
	void register_startup_secrets(const InterServerCredentials& inter, const DatabaseUrls& db);

	std::string redact(std::string_view text) const;

private:
	std::vector<std::string> secrets_;
};

class Logger {
public:
	Logger(std::shared_ptr<LogSink> sink, std::shared_ptr<const Redactor> redactor, LogLevel min_level = LogLevel::Info);

	void log(LogLevel level, std::string_view component, std::string_view message) const;
	void debug(std::string_view c, std::string_view m) const { log(LogLevel::Debug, c, m); }
	void info(std::string_view c, std::string_view m) const { log(LogLevel::Info, c, m); }
	void warn(std::string_view c, std::string_view m) const { log(LogLevel::Warn, c, m); }
	void error(std::string_view c, std::string_view m) const { log(LogLevel::Error, c, m); }

private:
	std::shared_ptr<LogSink> sink_;
	std::shared_ptr<const Redactor> redactor_;
	LogLevel min_level_;
};

} // namespace rathena::core
