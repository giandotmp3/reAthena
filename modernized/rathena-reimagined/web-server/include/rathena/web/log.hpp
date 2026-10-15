// rathena-web-server: logging with a secret-redaction filter (BR-WEB-017 / S2).
//
// Handlers never pass tokens to the logger. The filter is defense in depth: any
// `AuthToken=<v>` / `token=<v>` / `"AuthToken":"<v>"` fragment is masked before
// a record reaches a sink.
#pragma once

#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace rathena::web {

enum class LogLevel { Debug, Info, Warning, Error };

inline constexpr std::string_view kRedacted = "[REDACTED]";

/// Masks credential-shaped fragments (see file comment). Pure.
std::string redact_secrets(std::string_view message);

class Logger {
public:
    virtual ~Logger() = default;
    /// Always redacts before forwarding to write().
    void log(LogLevel level, std::string_view message);

protected:
    virtual void write(LogLevel level, const std::string& redacted_message) = 0;
};

/// stderr sink.
class StderrLogger final : public Logger {
protected:
    void write(LogLevel level, const std::string& redacted_message) override;
};

/// Test sink: keeps every (already redacted) record.
class CapturingLogger final : public Logger {
public:
    struct Record {
        LogLevel level;
        std::string message;
    };
    std::vector<Record> records() const;
    bool contains(std::string_view needle) const;

protected:
    void write(LogLevel level, const std::string& redacted_message) override;

private:
    mutable std::mutex mu_;
    std::vector<Record> records_;
};

}  // namespace rathena::web
