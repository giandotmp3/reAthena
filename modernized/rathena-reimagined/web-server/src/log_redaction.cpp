#include "rathena/web/log.hpp"

#include <array>
#include <cctype>
#include <iostream>

namespace rathena::web {
namespace {

bool iequals_at(std::string_view s, std::size_t pos, std::string_view key) {
    if (pos + key.size() > s.size()) return false;
    for (std::size_t i = 0; i < key.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s[pos + i])) !=
            std::tolower(static_cast<unsigned char>(key[i]))) {
            return false;
        }
    }
    return true;
}

bool is_value_char(char c) {
    return !(std::isspace(static_cast<unsigned char>(c)) || c == '&' || c == ',' || c == ';' ||
             c == '"' || c == '\'' || c == '}' || c == ')');
}

}  // namespace

std::string redact_secrets(std::string_view msg) {
    // Keys whose values must never reach a sink (case-insensitive).
    static constexpr std::array<std::string_view, 4> kKeys{"authtoken", "web_auth_token", "token", "password"};
    std::string out;
    out.reserve(msg.size());
    std::size_t i = 0;
    while (i < msg.size()) {
        bool matched = false;
        for (auto key : kKeys) {
            if (!iequals_at(msg, i, key)) continue;
            // Require a non-identifier char before the key (avoid "mytokenizer").
            if (i > 0 && (std::isalnum(static_cast<unsigned char>(msg[i - 1])) || msg[i - 1] == '_')) continue;
            std::size_t j = i + key.size();
            if (j < msg.size() && msg[j] == '"') ++j;  // JSON: "AuthToken":"v"
            while (j < msg.size() && msg[j] == ' ') ++j;
            if (j >= msg.size() || (msg[j] != '=' && msg[j] != ':')) continue;
            ++j;
            while (j < msg.size() && (msg[j] == ' ' || msg[j] == '"' || msg[j] == '\'')) ++j;
            std::size_t v = j;
            while (v < msg.size() && is_value_char(msg[v])) ++v;
            out.append(msg.substr(i, j - i));
            out.append(kRedacted);
            i = v;
            matched = true;
            break;
        }
        if (!matched) {
            out.push_back(msg[i]);
            ++i;
        }
    }
    return out;
}

void Logger::log(LogLevel level, std::string_view message) {
    write(level, redact_secrets(message));
}

void StderrLogger::write(LogLevel level, const std::string& redacted_message) {
    static constexpr std::array<const char*, 4> kNames{"debug", "info", "warning", "error"};
    std::cerr << "[web][" << kNames[static_cast<std::size_t>(level)] << "] " << redacted_message << '\n';
}

void CapturingLogger::write(LogLevel level, const std::string& redacted_message) {
    std::lock_guard lock(mu_);
    records_.push_back({level, redacted_message});
}

std::vector<CapturingLogger::Record> CapturingLogger::records() const {
    std::lock_guard lock(mu_);
    return records_;
}

bool CapturingLogger::contains(std::string_view needle) const {
    std::lock_guard lock(mu_);
    for (const auto& r : records_) {
        if (r.message.find(needle) != std::string::npos) return true;
    }
    return false;
}

}  // namespace rathena::web
